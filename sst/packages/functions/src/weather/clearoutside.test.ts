import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, test } from "vitest";
import { parseClearOutside } from "./clearoutside.js";

const fixture = readFileSync(
  fileURLToPath(new URL("./__fixtures__/clearoutside.html", import.meta.url)),
  "utf8"
);

describe("parseClearOutside", () => {
  test("parses a noon-to-noon night into hourly weather samples", () => {
    const [night] = parseClearOutside(fixture);

    expect(night.nightId).toBe("2026-09-11");
    expect(night.hours).toHaveLength(24);
    expect(night.hours[0]).toEqual({
      hour: 12,
      timestampUtc: "2026-09-11T09:00:00.000Z",
      temperatureC: 36,
      cloudCoverTotalPct: 0,
      precipitationProbabilityPct: 0,
      thunderstormRisk: false
    });
    expect(night.hours[12].timestampUtc).toBe("2026-09-11T21:00:00.000Z");
    expect(night.hours[6].thunderstormRisk).toBe(true);
    expect(night.hours[6].precipitationProbabilityPct).toBe(40);
  });

  test("fails closed when a required weather row is missing", () => {
    const malformed = fixture.replace("Total Clouds (% Sky Obscured)", "Clouds");

    expect(() => parseClearOutside(malformed)).toThrow(
      "Clear Outside page is missing the Total Clouds row"
    );
  });
});
