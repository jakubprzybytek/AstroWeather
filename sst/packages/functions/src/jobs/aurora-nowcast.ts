import SunCalc from "suncalc";
import { levelForNowcast, probabilityAtLeast } from "../aurora/levels";
import { mergeAurora } from "../aurora/merge";
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
  isDark?: (instant: Date, latitude: number, longitude: number) => boolean;
  log?: (message: string, details: Record<string, unknown>) => void;
};

export type AuroraNowcastSummary = {
  active: string[];   // locations sampled this run
  stored: string[];   // `configurationId/slotStart` updated
};

function sunIsDown(instant: Date, latitude: number, longitude: number): boolean {
  return SunCalc.getPosition(instant, latitude, longitude).altitude < DARK_ALTITUDE_RAD;
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
    log
  };
}

export async function sampleAuroraNowcast(
  dependencies: AuroraNowcastDependencies = createProductionDependencies()
): Promise<AuroraNowcastSummary> {
  const now = dependencies.now();
  const isDark = dependencies.isDark ?? sunIsDown;
  const active: Array<[string, ConfigurationCollection[string], string]> = [];

  for (const [configurationId, configuration] of Object.entries(dependencies.configurations)) {
    const { lat, lon, tz } = configuration.location;
    const nightId = nightIdFor(now, tz);
    if (!isDark(now, lat, lon)) continue;
    if (!await dependencies.store.get(configurationId, nightId, "FLAG")) continue;
    active.push([configurationId, configuration, nightId]);
  }

  const summary: AuroraNowcastSummary = { active: active.map(([id]) => id), stored: [] };
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

    await logCalibrationSample(dependencies, configurationId, configuration, nightId, grid, cellPct, now, isDark);
  }

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
  isDark: (instant: Date, latitude: number, longitude: number) => boolean
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
  const estimated = noaa3?.spans.find((span) => span.status === "estimated" && within(span));
  // The row's forecast level for that slot, without the nowcast.
  const forecast = mergeAurora(nightId, tz, kpMain, { gfz, noaa3 }, now)
    .find((slot) => slot.start.getTime() <= validAt && validAt < slot.start.getTime() + 3_600_000);

  dependencies.log?.("aurora-calibration", {
    configurationId,
    observedAt: grid.observedAt,
    validAt: grid.validAt,
    cellPct,
    nowcastLevel: levelForNowcast(cellPct),
    northPct: Array.from({ length: NORTH_PROFILE_DEGREES }, (_, index) => grid.probability(lat + index + 1, lon)),
    hp60Median: hp60?.median ?? null,
    hp60Max: hp60?.maximum ?? null,
    probLevel1: hp60 ? probabilityAtLeast(hp60, kpMain - 1) : null,
    kpEstimated: estimated?.kp ?? null,
    level: forecast?.level ?? null,
    dark: isDark(new Date(validAt), lat, lon)
  });
}

export const handler = async () => sampleAuroraNowcast();
