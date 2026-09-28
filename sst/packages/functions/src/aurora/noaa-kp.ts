import { fetchSourceDocument } from "./fetch";
import { utcSpan, type Span } from "./spans";

export const NOAA_KP_FORECAST_URL = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json";

const THREE_HOURS_MS = 3 * 60 * 60 * 1000;

export type NoaaKpValues = {
  kp: number;
  status: "estimated" | "predicted";
  noaaScale: string | null;
};

export type NoaaKpSpan = Span<NoaaKpValues>;

type Row = {
  time_tag?: unknown;
  kp?: unknown;
  observed?: unknown;
  noaa_scale?: unknown;
};

// The file carries a week of observed bins before the forecast; only the
// estimated and predicted bins are the forecast. `time_tag` is the UTC start
// of a three-hour bin, written without a zone suffix.
export function parseNoaaKpForecast(json: string): NoaaKpSpan[] {
  let rows: unknown;
  try {
    rows = JSON.parse(json);
  } catch {
    throw new Error("NOAA Kp forecast is not valid JSON");
  }
  if (!Array.isArray(rows)) {
    throw new Error("NOAA Kp forecast is not an array");
  }

  const spans = rows.flatMap((row: Row, index): NoaaKpSpan[] => {
    if (row.observed === "observed") return [];
    const status = row.observed;
    if (status !== "estimated" && status !== "predicted") {
      throw new Error(`NOAA Kp forecast row ${index} has an unknown status`);
    }

    const start = typeof row.time_tag === "string" ? Date.parse(`${row.time_tag}Z`) : Number.NaN;
    if (Number.isNaN(start)) {
      throw new Error(`NOAA Kp forecast row ${index} has an invalid time`);
    }
    if (typeof row.kp !== "number" || !Number.isFinite(row.kp)) {
      throw new Error(`NOAA Kp forecast row ${index} has an invalid Kp`);
    }

    return [{
      ...utcSpan(new Date(start), THREE_HOURS_MS),
      kp: row.kp,
      status,
      noaaScale: typeof row.noaa_scale === "string" ? row.noaa_scale : null
    }];
  });

  if (spans.length === 0) {
    throw new Error("NOAA Kp forecast contains no forecast bins");
  }
  return spans;
}

export async function loadNoaaKpForecast() {
  const document = await fetchSourceDocument(NOAA_KP_FORECAST_URL, "NOAA Kp forecast");
  return {
    spans: parseNoaaKpForecast(document.body),
    lastModified: document.lastModified
  };
}
