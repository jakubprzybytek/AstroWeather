import { describe, expect, test } from "vitest";
import { ovationCell, parseOvation } from "./ovation";

const document = JSON.stringify({
  "Observation Time": "2026-09-28T20:03:00Z",
  "Forecast Time": "2026-09-28T21:27:00Z",
  "Data Format": "[Longitude, Latitude, Aurora]",
  coordinates: [[16, 51, 3], [17, 51, 7], [17, 52, 12], [343, 51, 9]]
});

describe("parseOvation", () => {
  test("returns the nearest grid cell as a single observation-to-forecast span", () => {
    expect(parseOvation(document, 51.1079, 17.0385)).toEqual({
      start: "2026-09-28T20:03:00.000Z",
      end: "2026-09-28T21:27:00.000Z",
      probabilityPct: 7,
      cell: { latitude: 51, longitude: 17 }
    });
  });

  test("maps western longitudes onto the 0..359 grid", () => {
    expect(ovationCell(51.4, -17.2)).toEqual({ latitude: 51, longitude: 343 });
    expect(parseOvation(document, 51.4, -17.2).probabilityPct).toBe(9);
  });

  test("fails closed when the cell is missing", () => {
    expect(() => parseOvation(document, 10, 10)).toThrow("no value for the cell 10, 10");
  });

  test("fails closed without a grid", () => {
    expect(() => parseOvation("{}", 51, 17)).toThrow("missing the coordinates grid");
  });
});
