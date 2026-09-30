import { describe, expect, test } from "vitest";
import { parseLiveKp } from "./noaa-live";

function feed(values: Array<[string, number]>): string {
  return JSON.stringify(values.map(([time, kp]) => ({ time_tag: time, kp_index: Math.floor(kp), estimated_kp: kp, kp: `${Math.floor(kp)}Z` })));
}

describe("parseLiveKp", () => {
  test("takes the highest estimate of the last 30 minutes", () => {
    const live = parseLiveKp(feed([
      ["2026-09-30T20:00:00", 7.33],   // older than the window
      ["2026-09-30T20:40:00", 5.67],
      ["2026-09-30T20:55:00", 6.33],
      ["2026-09-30T21:05:00", 4.67]
    ]));

    expect(live).toEqual({ kp: 6.33, at: "2026-09-30T20:55:00.000Z", latestAt: "2026-09-30T21:05:00.000Z" });
  });

  test("skips malformed rows and fails without any estimate", () => {
    expect(parseLiveKp(JSON.stringify([{ time_tag: "bad", estimated_kp: 9 }, { time_tag: "2026-09-30T21:00:00", estimated_kp: 1.33 }])).kp).toBe(1.33);
    expect(() => parseLiveKp("[]")).toThrow("contains no estimates");
    expect(() => parseLiveKp("{}")).toThrow("not an array");
  });
});
