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

export type OvationGrid = {
  observedAt: string;
  validAt: string;
  // The probability for the cell nearest to a location, or null off the grid.
  probability(latitude: number, longitude: number): number | null;
};

// Parses the whole grid once, so one download serves every location and the
// cells north of each.
export function parseOvationGrid(json: string): OvationGrid {
  let document: OvationDocument;
  try {
    document = JSON.parse(json);
  } catch {
    throw new Error("OVATION file is not valid JSON");
  }
  if (!document || typeof document !== "object" || !Array.isArray(document.coordinates)) {
    throw new Error("OVATION file is missing the coordinates grid");
  }

  const values = new Map<string, number>();
  for (const entry of document.coordinates) {
    if (Array.isArray(entry) && typeof entry[2] === "number" && Number.isFinite(entry[2])) {
      values.set(`${entry[0]},${entry[1]}`, entry[2]);
    }
  }

  return {
    observedAt: timestamp(document["Observation Time"], "observation time"),
    validAt: timestamp(document["Forecast Time"], "forecast time"),
    probability(latitude, longitude) {
      const cell = ovationCell(latitude, longitude);
      return values.get(`${cell.longitude},${cell.latitude}`) ?? null;
    }
  };
}

// The nowcast is a single interval from the observation used to the time the
// probabilities apply to, so a location gets one span.
export function parseOvation(json: string, latitude: number, longitude: number): OvationSpan {
  const grid = parseOvationGrid(json);
  const cell = ovationCell(latitude, longitude);
  const probabilityPct = grid.probability(latitude, longitude);
  if (probabilityPct === null) {
    throw new Error(`OVATION file has no value for the cell ${cell.latitude}, ${cell.longitude}`);
  }

  return { start: grid.observedAt, end: grid.validAt, probabilityPct, cell };
}

export async function loadOvationGrid() {
  const document = await fetchSourceDocument(OVATION_URL, "OVATION");
  return parseOvationGrid(document.body);
}

export async function loadOvation(latitude: number, longitude: number) {
  const document = await fetchSourceDocument(OVATION_URL, "OVATION");
  return {
    spans: [parseOvation(document.body, latitude, longitude)],
    lastModified: document.lastModified
  };
}
