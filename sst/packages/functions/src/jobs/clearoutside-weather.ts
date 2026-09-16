import { configurations } from "../configurations";
import { fetchClearOutsideHtml, parseClearOutside } from "../weather/clearoutside";
import {
  createClearOutsideStore,
  toClearOutsideItem,
  type ClearOutsideStore
} from "../weather/clearoutside-storage";

type ConfigurationCollection = Record<string, {
  location: { lat: number; lon: number };
}>;

export type ClearOutsideIngestionDependencies = {
  configurations: ConfigurationCollection;
  fetchHtml: typeof fetchClearOutsideHtml;
  parse: typeof parseClearOutside;
  store: ClearOutsideStore;
  now: () => Date;
  waitBetweenLocations: (milliseconds: number) => Promise<void>;
  log?: (message: string, details: Record<string, unknown>) => void;
};

const LOCATION_REQUEST_GAP_MS = 1_000;

export type ClearOutsideIngestionSummary = {
  succeeded: string[];
  failed: string[];
};

function createProductionDependencies(): ClearOutsideIngestionDependencies {
  return {
    configurations,
    fetchHtml: fetchClearOutsideHtml,
    parse: parseClearOutside,
    store: createClearOutsideStore(),
    now: () => new Date(),
    waitBetweenLocations: (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds)),
    log: (message, details) => console.log(message, details)
  };
}

export async function ingestClearOutside(
  dependencies: ClearOutsideIngestionDependencies = createProductionDependencies()
): Promise<ClearOutsideIngestionSummary> {
  const succeeded: string[] = [];
  const failed: string[] = [];

  const entries = Object.entries(dependencies.configurations);

  for (const [index, [configurationId, configuration]] of entries.entries()) {
    if (index > 0) {
      await dependencies.waitBetweenLocations(LOCATION_REQUEST_GAP_MS);
    }

    const fetchedAt = dependencies.now().toISOString();

    try {
      const { lat: latitude, lon: longitude } = configuration.location;
      const html = await dependencies.fetchHtml(latitude, longitude);
      const nights = dependencies.parse(html);

      if (nights.length === 0) {
        throw new Error("Clear Outside returned no forecast nights");
      }

      const items = nights.map((night) => toClearOutsideItem(
        configurationId,
        { latitude, longitude },
        night,
        fetchedAt
      ));

      for (const item of items) {
        await dependencies.store.putNight(item);
      }

      succeeded.push(configurationId);
      dependencies.log?.("Clear Outside ingestion succeeded", {
        configurationId,
        nightCount: items.length,
        fetchedAt,
        status: "success"
      });
    } catch (cause) {
      failed.push(configurationId);
      dependencies.log?.("Clear Outside ingestion failed", {
        configurationId,
        error: cause instanceof Error ? cause.message : String(cause),
        status: "failed"
      });
    }
  }

  const summary = { succeeded, failed };
  dependencies.log?.("Clear Outside ingestion completed", summary);

  if (failed.length > 0) {
    throw new Error(`Clear Outside ingestion failed for: ${failed.join(", ")}`);
  }

  return summary;
}

export const handler = async () => ingestClearOutside();