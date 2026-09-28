import { describe, expect, test } from "vitest";
import { minutesAboveHorizon } from "./astronomy";
import { cloudLevel, encodeMatrix, minutesLevel, quartileLevel } from "./matrix";
import { observingSlots } from "./nights";
import { precipitationCell, wholeDegrees } from "./weather-reader";

describe("matrix cells", () => {
  test.each([
    [0, 0], [1, 1], [29, 1], [30, 2], [59, 2], [60, 3]
  ])("%i minutes above the horizon is level %i", (minutes, level) => {
    expect(minutesLevel(minutes)).toBe(level);
  });

  test.each([
    [0, 0], [24, 0], [25, 1], [49, 1], [50, 2], [74, 2], [75, 3], [100, 3]
  ])("%i percent is level %i", (percent, level) => {
    expect(quartileLevel(percent)).toBe(level);
  });

  test.each([
    [0, 0], [1, 1], [33, 1], [33.4, 2], [34, 2], [66, 2], [66.7, 3], [67, 3], [100, 3]
  ])("%d percent cloud coverage is level %i", (percent, level) => {
    expect(cloudLevel(percent)).toBe(level);
  });

  test.each([
    [18, "18"], [18.4, "18"], [18.5, "19"], [-3, "-3"], [-0.4, "0"], [0, "0"]
  ])("%d degrees is sent as %s", (celsius, text) => {
    expect(wholeDegrees(celsius)).toBe(text);
  });

  test("encodes a row, with ? for an unavailable slot and for an empty row", () => {
    expect(encodeMatrix([0, 1, 2, 3, "*", null])).toBe("0123*?");
    expect(encodeMatrix([null, null])).toBe("?");
  });

  test("a thunderstorm risk blinks whatever the probability", () => {
    expect(precipitationCell(10, true)).toBe("*");
    expect(precipitationCell(null, true)).toBe("*");
    expect(precipitationCell(80, false)).toBe(3);
    expect(precipitationCell(10, null)).toBe(0);
    expect(precipitationCell(null, false)).toBeNull();
    expect(precipitationCell(undefined, undefined)).toBeNull();
  });

  test("counts the minutes of a slot the body is up, sampled mid-minute", () => {
    const slot = observingSlots("2026-09-17", "Europe/Warsaw")[0];
    const risesAt = (minute: number) => (instant: Date) =>
      (instant.getTime() - slot.start.getTime()) / 60_000 >= minute ? 1 : -1;
    expect(minutesAboveHorizon(slot, () => 1)).toBe(60);
    expect(minutesAboveHorizon(slot, () => -1)).toBe(0);
    expect(minutesAboveHorizon(slot, risesAt(20))).toBe(40);
    expect(minutesAboveHorizon(slot, risesAt(59.4))).toBe(1);
    expect(minutesAboveHorizon(slot, risesAt(59.6))).toBe(0);
  });
});
