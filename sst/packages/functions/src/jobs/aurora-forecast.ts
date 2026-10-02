import { loadGfzHp60, type GfzHp60Span, type GfzHp60Values } from "../aurora/gfz-hp60";
import { flagKp, gfzTriggers, noaaTriggers, probabilityAtLeast } from "../aurora/levels";
import { loadNoaaKpForecast, type NoaaKpSpan, type NoaaKpValues } from "../aurora/noaa-kp";
import { loadNoaa27DayOutlook, type NoaaOutlookValues } from "../aurora/noaa-outlook";
import { groupSpansByNight, type Span } from "../aurora/spans";
import {
  auroraExpireAt,
  auroraKey,
  createAuroraStore,
  type AuroraSourceKey,
  type AuroraStore
} from "../aurora/storage";
import { configurations } from "../configurations";
import { nextNightIds } from "../forecast/nights";

// Fetches the global aurora forecasts once and stores them per location and
// night; decides the storm-night flag. See
// docs/aurora-forecast-supplier.md#fetch-cadence.

type ConfigurationCollection = Record<string, {
  location: { lat: number; lon: number; tz: string };
  aurora: { kpMain: number };
}>;

type Loaded<T> = { spans: Span<T>[]; lastModified: string | null; issuedAt?: string };

export type AuroraForecastDependencies = {
  configurations: ConfigurationCollection;
  loadGfz: () => Promise<Loaded<GfzHp60Values>>;
  loadNoaa3: () => Promise<Loaded<NoaaKpValues>>;
  loadNoaa27: () => Promise<Loaded<NoaaOutlookValues>>;
  store: AuroraStore;
  now: () => Date;
  log?: (message: string, details: Record<string, unknown>) => void;
};

export type AuroraForecastSummary = {
  stored: AuroraSourceKey[];
  failed: AuroraSourceKey[];
  flagged: string[];  // `configurationId/nightId` newly flagged
};

// The six displayed nights, as the payload shows them.
const NIGHT_COUNT = 6;

function createProductionDependencies(): AuroraForecastDependencies {
  return {
    configurations,
    loadGfz: () => loadGfzHp60(),
    loadNoaa3: () => loadNoaaKpForecast({ includeObserved: true }),
    loadNoaa27: () => loadNoaa27DayOutlook(),
    store: createAuroraStore(),
    now: () => new Date(),
    log: (message, details) => console.log(message, details)
  };
}

export async function ingestAuroraForecast(
  dependencies: AuroraForecastDependencies = createProductionDependencies(),
  sources: AuroraSourceKey[] = ["GFZ", "NOAA3", "NOAA27"]
): Promise<AuroraForecastSummary> {
  const now = dependencies.now();
  const fetchedAt = now.toISOString();
  const stored: AuroraSourceKey[] = [];
  const failed: AuroraSourceKey[] = [];
  const loaders = { GFZ: dependencies.loadGfz, NOAA3: dependencies.loadNoaa3, NOAA27: dependencies.loadNoaa27 };
  const loaded: Partial<Record<AuroraSourceKey, Loaded<unknown>>> = {};

  for (const source of sources) {
    try {
      const result = await loaders[source]() as Loaded<unknown>;
      loaded[source] = result;
      for (const [configurationId, configuration] of Object.entries(dependencies.configurations)) {
        const { tz } = configuration.location;
        const wanted = nextNightIds(now, tz, NIGHT_COUNT);
        for (const night of groupSpansByNight(result.spans, tz)) {
          if (!wanted.includes(night.nightId)) continue;
          await dependencies.store.put({
            ...auroraKey(configurationId, night.nightId, source),
            configurationId,
            nightId: night.nightId,
            source,
            spans: night.spans,
            lastModified: result.lastModified,
            ...(result.issuedAt ? { issuedAt: result.issuedAt } : {}),
            fetchedAt,
            expireAt: auroraExpireAt(night.nightId, tz)
          } as Parameters<AuroraStore["put"]>[0]);
        }
      }
      stored.push(source);
      dependencies.log?.("Aurora forecast source stored", {
        source,
        lastModified: result.lastModified,
        ...(result.issuedAt ? { issuedAt: result.issuedAt } : {}),
        spans: result.spans.length,
        from: result.spans[0]?.start ?? null,
        to: result.spans[result.spans.length - 1]?.end ?? null
      });
    } catch (cause) {
      failed.push(source);
      dependencies.log?.("Aurora forecast source failed", {
        source,
        error: cause instanceof Error ? cause.message : String(cause),
        status: "failed"
      });
    }
  }

  const flagged = await flagStormNights(dependencies, now, fetchedAt,
    loaded.GFZ?.spans as GfzHp60Span[] | undefined, loaded.NOAA3?.spans as NoaaKpSpan[] | undefined);

  const summary = { stored, failed, flagged };
  dependencies.log?.("Aurora forecast ingestion completed", summary);
  if (failed.length > 0) {
    throw new Error(`Aurora forecast ingestion failed for: ${failed.join(", ")}`);
  }
  return summary;
}

// A night is flagged once, when any hour of it triggers, and stays flagged.
async function flagStormNights(
  dependencies: AuroraForecastDependencies,
  now: Date,
  fetchedAt: string,
  gfz: GfzHp60Span[] | undefined,
  noaa3: NoaaKpSpan[] | undefined
): Promise<string[]> {
  const flagged: string[] = [];
  for (const [configurationId, configuration] of Object.entries(dependencies.configurations)) {
    const { tz } = configuration.location;
    const { kpMain } = configuration.aurora;
    const gfzNights = new Map(groupSpansByNight(gfz ?? [], tz).map((night) => [night.nightId, night.spans]));
    const noaaNights = new Map(groupSpansByNight(noaa3 ?? [], tz).map((night) => [night.nightId, night.spans]));

    // How close each night came to the flag, one line per location, so a
    // quiet week can be told from a broken check.
    const outlook: Array<Record<string, unknown>> = [];
    for (const nightId of nextNightIds(now, tz, NIGHT_COUNT)) {
      const gfzSpans = gfzNights.get(nightId) ?? [];
      const noaaSpans = (noaaNights.get(nightId) ?? []).filter((span) => span.status !== "observed");
      const max = (values: number[]) => values.length ? Math.round(Math.max(...values) * 100) / 100 : null;
      const gfzHit = gfzSpans.find((span) => gfzTriggers(span, kpMain));
      const noaaHit = noaaSpans.find((span) => noaaTriggers(span.kp, kpMain));
      // The flag is sticky, so a night can stay flagged after the forecast drops.
      const alreadyFlagged = !!await dependencies.store.get(configurationId, nightId, "FLAG");
      outlook.push({
        nightId,
        gfzMedianMax: max(gfzSpans.map((span) => span.median)),
        gfzQ75Max: max(gfzSpans.map((span) => span.quantile75)),
        gfzProbLevel1Max: max(gfzSpans.map((span) => probabilityAtLeast(span, kpMain - 1))),
        noaaKpMax: max(noaaSpans.map((span) => span.kp)),
        flag: alreadyFlagged ? "kept" : (gfzHit || noaaHit ? "new" : "no")
      });
      if (!gfzHit && !noaaHit) continue;
      if (alreadyFlagged) continue;

      const reason = gfzHit
        ? `gfz ${gfzHit.start}: q75=${gfzHit.quantile75} median=${gfzHit.median}`
        : `noaa ${noaaHit!.start}: kp=${noaaHit!.kp}`;
      await dependencies.store.put({
        ...auroraKey(configurationId, nightId, "FLAG"),
        configurationId,
        nightId,
        source: "FLAG",
        reason,
        fetchedAt,
        expireAt: auroraExpireAt(nightId, tz)
      });
      flagged.push(`${configurationId}/${nightId}`);
      dependencies.log?.("Aurora storm night flagged", { configurationId, nightId, reason });
    }
    dependencies.log?.("Aurora forecast outlook", {
      configurationId,
      kpMain,
      flagAt: { kp: flagKp(kpMain), probLevel1: 0.25 },
      nights: outlook
    });
  }
  return flagged;
}

export const handler = async () => ingestAuroraForecast();
