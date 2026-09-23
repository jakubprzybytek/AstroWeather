import { describe, expect, test } from "vitest";
import { instantAtLocal, localDate, nextNightIds, observingSlots, utcOffset } from "./nights";

describe("observing nights", () => {
  const timezone = "Europe/Warsaw";

  test("uses the local noon boundary rather than UTC date", () => {
    expect(nextNightIds(new Date("2026-09-17T09:59:00Z"), timezone, 2))
      .toEqual(["2026-09-16", "2026-09-17"]);
    expect(nextNightIds(new Date("2026-09-17T10:00:00Z"), timezone, 2))
      .toEqual(["2026-09-17", "2026-09-18"]);
  });

  test("returns consecutive dates across month boundaries", () => {
    expect(nextNightIds(new Date("2026-12-31T12:00:00Z"), timezone, 3))
      .toEqual(["2026-12-31", "2027-01-01", "2027-01-02"]);
  });

  test("keeps 21 wall-clock slots across DST", () => {
    const slots = observingSlots("2026-10-24", timezone);
    expect(slots).toHaveLength(21);
    expect(slots[0]).toMatchObject({ date: "2026-10-24", hour: 14 });
    expect(slots[20]).toMatchObject({ date: "2026-10-25", hour: 10 });
    expect(localDate(slots[10].midpoint, timezone)).toBe("2026-10-25");
  });

  test("formats the zone's UTC offset at an instant", () => {
    expect(utcOffset(new Date("2026-09-17T12:30:00.678Z"), timezone)).toBe("+02:00");
    expect(utcOffset(new Date("2026-12-01T12:00:00Z"), timezone)).toBe("+01:00");
    expect(utcOffset(new Date("2026-10-25T00:59:59Z"), timezone)).toBe("+02:00");
    expect(utcOffset(new Date("2026-10-25T01:00:00Z"), timezone)).toBe("+01:00");
    expect(utcOffset(new Date("2026-09-17T12:00:00Z"), "UTC")).toBe("+00:00");
    expect(utcOffset(new Date("2026-09-17T12:00:00Z"), "America/St_Johns")).toBe("-02:30");
    expect(utcOffset(new Date("2026-09-17T12:00:00Z"), "Asia/Kathmandu")).toBe("+05:45");
  });

  test("maps local wall clock time to the configured zone", () => {
    expect(instantAtLocal("2026-09-17", 14, 30, timezone).toISOString())
      .toBe("2026-09-17T12:30:00.000Z");
  });
});