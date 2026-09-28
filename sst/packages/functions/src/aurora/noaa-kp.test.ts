import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, test } from "vitest";
import { parseNoaaKpForecast } from "./noaa-kp";

const fixture = readFileSync(fileURLToPath(new URL("./__fixtures__/noaa-kp-forecast.json", import.meta.url)), "utf8");

describe("parseNoaaKpForecast", () => {
  test("keeps the estimated and predicted three-hour bins", () => {
    const spans = parseNoaaKpForecast(fixture);

    expect(spans).toHaveLength(18);
    expect(spans[0]).toEqual({
      start: "2026-09-28T21:00:00.000Z",
      end: "2026-09-29T00:00:00.000Z",
      kp: 1.67,
      status: "estimated",
      noaaScale: null
    });
    expect(spans[1].status).toBe("predicted");
    expect(spans[17].start).toBe("2026-10-01T00:00:00.000Z");
  });

  test("fails closed when the forecast has no forecast bins", () => {
    const observedOnly = JSON.stringify(JSON.parse(fixture).filter((row: { observed: string }) => row.observed === "observed"));

    expect(() => parseNoaaKpForecast(observedOnly)).toThrow("contains no forecast bins");
  });

  test("fails closed on an unknown status", () => {
    const malformed = fixture.replace('"predicted"', '"guessed"');

    expect(() => parseNoaaKpForecast(malformed)).toThrow("unknown status");
  });
});
