import { fetchSourceDocument } from "./fetch";
import type { Span } from "./spans";

export const OVATION_URL = "https://services.swpc.noaa.gov/json/ovation_aurora_latest.json";

export type OvationValues = {
  probabilityPct: number;
  cell: {
    latitude: number;
    longitude: number;
  };
};

export type OvationSpan = Span<OvationValues>;

type OvationDocument = {
  "Observation Time"?: unknown;
  "Forecast Time"?: unknown;
  coordinates?: unknown;
};

function timestamp(value: unknown, name: string): string {
  const time = typeof value === "string" ? Date.parse(value) : Number.NaN;
  if (Number.isNaN(time)) {
    throw new Error(`OVATION file has an invalid ${name}`);
  }
  return new Date(time).toISOString();
}

// The grid is one degree, longitudes 0..359 and latitudes -90..90; the nearest
// cell is the value for a location.
export function ovationCell(latitude: number, longitude: number) {
  return {
    latitude: Math.round(latitude),
    longitude: ((Math.round(longitude) % 360) + 360) % 360
  };
}

// The nowcast is a single interval from the observation used to the time the
// probabilities apply to, so a location gets one span.
export function parseOvation(json: string, latitude: number, longitude: number): OvationSpan {
  let document: OvationDocument;
  try {
    document = JSON.parse(json);
  } catch {
    throw new Error("OVATION file is not valid JSON");
  }
  if (!document || typeof document !== "object" || !Array.isArray(document.coordinates)) {
    throw new Error("OVATION file is missing the coordinates grid");
  }

  const cell = ovationCell(latitude, longitude);
  const point = document.coordinates.find((entry) =>
    Array.isArray(entry) && entry[0] === cell.longitude && entry[1] === cell.latitude
  );
  if (!point || typeof point[2] !== "number" || !Number.isFinite(point[2])) {
    throw new Error(`OVATION file has no value for the cell ${cell.latitude}, ${cell.longitude}`);
  }

  return {
    start: timestamp(document["Observation Time"], "observation time"),
    end: timestamp(document["Forecast Time"], "forecast time"),
    probabilityPct: point[2],
    cell
  };
}

export async function loadOvation(latitude: number, longitude: number) {
  const document = await fetchSourceDocument(OVATION_URL, "OVATION");
  return {
    spans: [parseOvation(document.body, latitude, longitude)],
    lastModified: document.lastModified
  };
}
