import SunCalc from "suncalc";
import { flagKp, levelForNowcast, probabilityAtLeast } from "../aurora/levels";
import { mergeAurora } from "../aurora/merge";
import { loadLiveKp, type LiveKp } from "../aurora/noaa-live";
import { loadOvationGrid, type OvationGrid } from "../aurora/ovation";
import { auroraExpireAt, auroraKey, createAuroraStore, type AuroraStore, type OvationItem } from "../aurora/storage";
import { configurations } from "../configurations";
import { nightIdFor, observingSlots } from "../forecast/nights";
import { ingestAuroraForecast, type AuroraForecastDependencies } from "./aurora-forecast";

// Every ten minutes: on a flagged storm night, while it is dark, refreshes
// the forecasts and stores the OVATION nowcast for the slot it describes,
// logging a calibration sample. Otherwise does nothing. See
// docs/aurora-forecast-supplier.md#fetch-cadence and #calibrating-the-nowcast.

// Sun below -6°: civil twilight is over.
const DARK_ALTITUDE_RAD = -6 * Math.PI / 180;
// The cells north of the location logged for calibration.
const NORTH_PROFILE_DEGREES = 8;

type ConfigurationCollection = AuroraForecastDependencies["configurations"];

export type AuroraNowcastDependencies = {
  configurations: ConfigurationCollection;
  store: AuroraStore;
  now: () => Date;
  loadOvation: () => Promise<OvationGrid>;
  refreshForecasts: () => Promise<unknown>;
  // NOAA's live Kp estimate: catches a storm no forecast flagged.
  loadLiveKp: () => Promise<LiveKp>;
  isDark?: (instant: Date, latitude: number, longitude: number) => boolean;
  sunAltitude?: (instant: Date, latitude: number, longitude: number) => number;  // degrees, for the log
  log?: (message: string, details: Record<string, unknown>) => void;
};

export type AuroraNowcastSummary = {
  active: string[];   // locations sampled this run
  stored: string[];   // `configurationId/slotStart` updated
};

function sunIsDown(instant: Date, latitude: number, longitude: number): boolean {
  return SunCalc.getPosition(instant, latitude, longitude).altitude < DARK_ALTITUDE_RAD;
}

function sunAltitudeDegrees(instant: Date, latitude: number, longitude: number): number {
  return Math.round(SunCalc.getPosition(instant, latitude, longitude).altitude * 180 / Math.PI * 10) / 10;
}

function createProductionDependencies(): AuroraNowcastDependencies {
  const store = createAuroraStore();
  const log = (message: string, details: Record<string, unknown>) => console.log(message, details);
  return {
    configurations,
    store,
    now: () => new Date(),
    loadOvation: loadOvationGrid,
    // The forecast sources the calibration sample compares with; the outlook
    // does not change within a night.
    refreshForecasts: () => ingestAuroraForecast(undefined, ["GFZ", "NOAA3"]),
    loadLiveKp,
    log
  };
}

export async function sampleAuroraNowcast(
  dependencies: AuroraNowcastDependencies = createProductionDependencies()
): Promise<AuroraNowcastSummary> {
  const now = dependencies.now();
  const isDark = dependencies.isDark ?? sunIsDown;
  const sunAltitude = dependencies.sunAltitude ?? sunAltitudeDegrees;
  const active: Array<[string, ConfigurationCollection[string], string]> = [];
  const checks: Array<Record<string, unknown>> = [];

  // Every condition is checked and logged for every location, so each run
  // says why it sampled or skipped. The live Kp is fetched only after dark.
  const locations = Object.entries(dependencies.configurations).map(([configurationId, configuration]) => {
    const { lat, lon, tz } = configuration.location;
    return { configurationId, configuration, nightId: nightIdFor(now, tz), dark: isDark(now, lat, lon) };
  });
  let live: LiveKp | null = null;
  if (locations.some((location) => location.dark)) {
    try {
      live = await dependencies.loadLiveKp();
    } catch (cause) {
      dependencies.log?.("Aurora nowcast live Kp failed", {
        error: cause instanceof Error ? cause.message : String(cause)
      });
    }
  }

  for (const { configurationId, configuration, nightId, dark } of locations) {
    const { lat, lon, tz } = configuration.location;
    let flagged = !!await dependencies.store.get(configurationId, nightId, "FLAG");
    const liveTriggers = live !== null && live.kp >= flagKp(configuration.aurora.kpMain);
    if (dark && !flagged && live && liveTriggers) {
      // A storm the forecasts missed: flag the night, which also switches the
      // device to hourly refreshes from its next pull.
      const reason = `live kp=${live.kp} at ${live.at}`;
      await dependencies.store.put({
        ...auroraKey(configurationId, nightId, "FLAG"),
        configurationId,
        nightId,
        source: "FLAG",
        reason,
        fetchedAt: now.toISOString(),
        expireAt: auroraExpireAt(nightId, tz)
      });
      flagged = true;
      dependencies.log?.("Aurora storm night flagged", { configurationId, nightId, reason });
    }
    checks.push({
      configurationId, nightId, flagged, dark, sunAltitudeDeg: sunAltitude(now, lat, lon),
      liveKp: live?.kp ?? null, liveTriggers
    });
    if (dark && flagged) active.push([configurationId, configuration, nightId]);
  }

  const summary: AuroraNowcastSummary = { active: active.map(([id]) => id), stored: [] };
  dependencies.log?.(active.length ? "Aurora nowcast sampling" : "Aurora nowcast skipped", { locations: checks });
  if (active.length === 0) return summary;

  try {
    await dependencies.refreshForecasts();
  } catch (cause) {
    // The nowcast does not depend on the forecasts; the sample just compares with older ones.
    dependencies.log?.("Aurora nowcast forecast refresh failed", {
      error: cause instanceof Error ? cause.message : String(cause)
    });
  }

  const grid = await dependencies.loadOvation();
  const validAt = new Date(grid.validAt);

  for (const [configurationId, configuration, nightId] of active) {
    const { lat, lon, tz } = configuration.location;
    const cellPct = grid.probability(lat, lon);
    if (cellPct === null) {
      dependencies.log?.("Aurora nowcast cell missing", { configurationId, latitude: lat, longitude: lon });
      continue;
    }

    // The slot the nowcast describes, usually the next hour; none when it
    // falls in the unshown 11:00-12:00 hour or in the next night.
    const slot = observingSlots(nightId, tz).find((candidate) =>
      candidate.start.getTime() <= validAt.getTime() && validAt.getTime() < candidate.start.getTime() + 3_600_000);

    if (slot) {
      const existing = await dependencies.store.get(configurationId, nightId, "OVATION");
      const key = slot.start.toISOString();
      const previous = existing?.slots[key];
      const item: OvationItem = {
        ...auroraKey(configurationId, nightId, "OVATION"),
        configurationId,
        nightId,
        source: "OVATION",
        slots: {
          ...existing?.slots,
          [key]: {
            latestPct: cellPct,
            maxPct: Math.max(cellPct, previous?.maxPct ?? 0),
            latestValidAt: grid.validAt,
            samples: (previous?.samples ?? 0) + 1
          }
        },
        fetchedAt: now.toISOString(),
        expireAt: auroraExpireAt(nightId, tz)
      };
      await dependencies.store.put(item);
      summary.stored.push(`${configurationId}/${key}`);
    }

    await logCalibrationSample(dependencies, configurationId, configuration, nightId, grid, cellPct, now, isDark, live);
  }

  dependencies.log?.("Aurora nowcast sampled", { validAt: grid.validAt, stored: summary.stored });
  return summary;
}

async function logCalibrationSample(
  dependencies: AuroraNowcastDependencies,
  configurationId: string,
  configuration: ConfigurationCollection[string],
  nightId: string,
  grid: OvationGrid,
  cellPct: number,
  now: Date,
  isDark: (instant: Date, latitude: number, longitude: number) => boolean,
  live: LiveKp | null
): Promise<void> {
  const { lat, lon, tz } = configuration.location;
  const { kpMain } = configuration.aurora;
  const validAt = Date.parse(grid.validAt);
  const [gfz, noaa3] = await Promise.all([
    dependencies.store.get(configurationId, nightId, "GFZ"),
    dependencies.store.get(configurationId, nightId, "NOAA3")
  ]);
  const within = (span: { start: string; end: string }) => Date.parse(span.start) <= validAt && validAt < Date.parse(span.end);
  const hp60 = gfz?.spans.find(within);
  // NOAA's running estimate is the `estimated` bin in progress now; the bin
  // covering `validAt` has usually not started and is then still a forecast.
  const current = noaa3?.spans.find((span) => span.status !== "predicted"
    && Date.parse(span.start) <= now.getTime() && now.getTime() < Date.parse(span.end));
  const noaaAtValid = noaa3?.spans.find(within);
  // The row's forecast level for that slot, without the nowcast.
  const forecast = mergeAurora(nightId, tz, kpMain, { gfz, noaa3 }, now)
    .find((slot) => slot.start.getTime() <= validAt && validAt < slot.start.getTime() + 3_600_000);

  const sample = {
    configurationId,
    observedAt: grid.observedAt,
    validAt: grid.validAt,
    cellPct,
    nowcastLevel: levelForNowcast(cellPct),
    northPct: Array.from({ length: NORTH_PROFILE_DEGREES }, (_, index) => grid.probability(lat + index + 1, lon)),
    hp60Median: hp60?.median ?? null,
    hp60Max: hp60?.maximum ?? null,
    probLevel1: hp60 ? probabilityAtLeast(hp60, kpMain - 1) : null,
    kpEstimated: current?.kp ?? null,
    kpNoaaAtValid: noaaAtValid?.kp ?? null,
    kpNoaaAtValidStatus: noaaAtValid?.status ?? null,
    liveKp: live?.kp ?? null,
    liveKpAt: live?.at ?? null,
    level: forecast?.level ?? null,
    dark: isDark(new Date(validAt), lat, lon),
    sampledAt: now.toISOString()
  };
  dependencies.log?.("aurora-calibration", sample);
  try {
    await dependencies.store.putCalibration(sample);
  } catch (cause) {
    // The log line still carries the sample.
    dependencies.log?.("Aurora calibration sample not stored", {
      configurationId,
      error: cause instanceof Error ? cause.message : String(cause)
    });
  }
}

export const handler = async () => sampleAuroraNowcast();
