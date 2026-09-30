import { fetchSourceDocument } from "./fetch";

export const NOAA_LIVE_KP_URL = "https://services.swpc.noaa.gov/json/planetary_k_index_1m.json";

// How far back the live estimate is taken: its maximum over this window, so a
// brief dip does not stop the sampling of a storm in progress.
const WINDOW_MS = 30 * 60 * 1000;

export type LiveKp = {
  kp: number;       // the highest estimated Kp in the window
  at: string;       // the minute it was estimated for
  latestAt: string; // the newest minute in the file
};

type Row = { time_tag?: unknown; estimated_kp?: unknown };

// NOAA's running estimate of the planetary Kp, one row per minute for the last
// few hours; `time_tag` is UTC without a zone suffix. It sees a storm as it
// happens, including one no forecast predicted.
export function parseLiveKp(json: string): LiveKp {
  let rows: unknown;
  try {
    rows = JSON.parse(json);
  } catch {
    throw new Error("NOAA live Kp is not valid JSON");
  }
  if (!Array.isArray(rows)) {
    throw new Error("NOAA live Kp is not an array");
  }

  const samples = rows.flatMap((row: Row) => {
    const time = typeof row.time_tag === "string" ? Date.parse(`${row.time_tag}Z`) : Number.NaN;
    return !Number.isNaN(time) && typeof row.estimated_kp === "number" && Number.isFinite(row.estimated_kp)
      ? [{ time, kp: row.estimated_kp }]
      : [];
  });
  if (samples.length === 0) {
    throw new Error("NOAA live Kp contains no estimates");
  }

  const latest = Math.max(...samples.map((sample) => sample.time));
  const best = samples
    .filter((sample) => sample.time > latest - WINDOW_MS)
    .reduce((top, sample) => sample.kp > top.kp ? sample : top);
  return { kp: best.kp, at: new Date(best.time).toISOString(), latestAt: new Date(latest).toISOString() };
}

export async function loadLiveKp(): Promise<LiveKp> {
  const document = await fetchSourceDocument(NOAA_LIVE_KP_URL, "NOAA live Kp");
  return parseLiveKp(document.body);
}
