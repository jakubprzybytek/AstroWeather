import { describe, expect, test } from "vitest";
import type { GfzHp60Values } from "./gfz-hp60";
import { gfzTriggers, levelForKp, levelForNowcast, noaaTriggers, probabilityAtLeast } from "./levels";

const quiet: GfzHp60Values = {
  median: 2, quantile75: 3, maximum: 4,
  prob4to5: 0, prob5to6: 0, prob6to7: 0, prob7to8: 0, probAtLeast8: 0
};

describe("aurora levels", () => {
  test("places levels one Kp below, at and above the location's main Kp", () => {
    expect([5.9, 6, 6.9, 7, 7.9, 8, 9].map((kp) => levelForKp(kp, 7))).toEqual([0, 1, 1, 2, 2, 3, 3]);
    expect([6.4, 6.5, 7.5, 8.5].map((kp) => levelForKp(kp, 7.5))).toEqual([0, 1, 2, 3]);
  });

  test("grades the nowcast by the placeholder percentages", () => {
    expect([0, 4, 5, 14, 15, 39, 40, 90].map(levelForNowcast)).toEqual([0, 0, 1, 1, 2, 2, 3, 3]);
  });

  test("sums the ensemble bands from the threshold's whole Kp up", () => {
    const values = { ...quiet, prob5to6: 0.1, prob6to7: 0.2, prob7to8: 0.05, probAtLeast8: 0.05 };
    expect(probabilityAtLeast(values, 6)).toBeCloseTo(0.3);
    expect(probabilityAtLeast(values, 6.5)).toBeCloseTo(0.3);
    expect(probabilityAtLeast(values, 5)).toBeCloseTo(0.4);
  });

  test("flags a storm night one Kp step below level 1", () => {
    expect(gfzTriggers(quiet, 7)).toBe(false);
    expect(gfzTriggers({ ...quiet, quantile75: 5 }, 7)).toBe(true);
    expect(gfzTriggers({ ...quiet, prob6to7: 0.25 }, 7)).toBe(true);
    expect(gfzTriggers({ ...quiet, quantile75: 5 }, 7.5)).toBe(false);
    expect(noaaTriggers(4.67, 7)).toBe(false);
    expect(noaaTriggers(5, 7)).toBe(true);
  });
});
