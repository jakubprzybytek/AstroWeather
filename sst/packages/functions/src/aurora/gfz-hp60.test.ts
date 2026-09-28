import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, test } from "vitest";
import { parseGfzHp60 } from "./gfz-hp60";

const fixture = readFileSync(fileURLToPath(new URL("./__fixtures__/gfz-hp60.json", import.meta.url)), "utf8");

describe("parseGfzHp60", () => {
  test("parses hourly ensemble rows into one-hour spans", () => {
    const spans = parseGfzHp60(fixture);

    expect(spans).toHaveLength(73);
    expect(spans[0]).toEqual({
      start: "2026-09-28T18:00:00.000Z",
      end: "2026-09-28T19:00:00.000Z",
      median: 1,
      quantile75: 1,
      maximum: 1,
      prob4to5: 0,
      prob5to6: 0,
      prob6to7: 0,
      prob7to8: 0,
      probAtLeast8: 0
    });
    expect(spans[72].start).toBe("2026-10-01T18:00:00.000Z");
  });

  test("takes the interval length from the file", () => {
    const threeHourly = JSON.parse(fixture);
    threeHourly["Time (UTC)"]["1"] = "28-09-2026 21:00";

    const spans = parseGfzHp60(JSON.stringify(threeHourly));

    expect(spans[0].end).toBe("2026-09-28T21:00:00.000Z");
  });

  test("fails closed when a column is missing", () => {
    const malformed = fixture.replace('"prob >= 8"', '"prob 8+"');

    expect(() => parseGfzHp60(malformed)).toThrow("GFZ Hp60 file is missing the prob >= 8 column");
  });

  test("fails closed on an invalid time stamp", () => {
    const malformed = fixture.replace("28-09-2026 18:00", "2026-09-28 18:00");

    expect(() => parseGfzHp60(malformed)).toThrow("invalid time at row 0");
  });
});
