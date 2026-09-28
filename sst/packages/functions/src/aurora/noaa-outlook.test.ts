import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, test } from "vitest";
import { parseNoaa27DayOutlook } from "./noaa-outlook";

const fixture = readFileSync(fileURLToPath(new URL("./__fixtures__/noaa-27-day-outlook.txt", import.meta.url)), "utf8");

describe("parseNoaa27DayOutlook", () => {
  test("parses the issue time and one UTC-day span per row", () => {
    const outlook = parseNoaa27DayOutlook(fixture);

    expect(outlook.issuedAt).toBe("2026-09-28T02:21:00.000Z");
    expect(outlook.spans).toHaveLength(27);
    expect(outlook.spans[0]).toEqual({
      start: "2026-09-28T00:00:00.000Z",
      end: "2026-09-29T00:00:00.000Z",
      f107: 98,
      ap: 5,
      largestKp: 2
    });
    expect(outlook.spans[26]).toMatchObject({ start: "2026-10-24T00:00:00.000Z", largestKp: 4 });
  });

  test("fails closed without an issued line", () => {
    expect(() => parseNoaa27DayOutlook(fixture.replace(":Issued:", ":Published:"))).toThrow("missing the issued line");
  });

  test("fails closed without daily rows", () => {
    const headerOnly = fixture.split("\n").filter((line) => !/^\d{4} /.test(line)).join("\n");

    expect(() => parseNoaa27DayOutlook(headerOnly)).toThrow("contains no daily rows");
  });
});
