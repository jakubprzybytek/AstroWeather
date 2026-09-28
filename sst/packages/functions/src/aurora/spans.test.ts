import { describe, expect, test } from "vitest";
import { groupSpansByNight, utcSpan } from "./spans";

const HOUR = 60 * 60 * 1000;

describe("groupSpansByNight", () => {
  test("keeps hourly spans inside the local-noon night they start in", () => {
    // 10:00 UTC is local noon in Europe/Warsaw during summer time.
    const spans = [0, 1, 2].map((offset) => ({
      ...utcSpan(new Date(Date.UTC(2026, 8, 28, 10 + offset)), HOUR),
      value: offset
    }));

    const nights = groupSpansByNight(spans, "Europe/Warsaw");

    expect(nights).toHaveLength(1);
    expect(nights[0].nightId).toBe("2026-09-28");
    expect(nights[0].spans.map((span) => span.value)).toEqual([0, 1, 2]);
  });

  test("puts a UTC-day span into the two nights it overlaps, keeping its boundaries", () => {
    const day = { ...utcSpan(new Date(Date.UTC(2026, 9, 4)), 24 * HOUR), largestKp: 4 };
    const nextDay = { ...utcSpan(new Date(Date.UTC(2026, 9, 5)), 24 * HOUR), largestKp: 3 };

    const nights = groupSpansByNight([nextDay, day], "Europe/Warsaw");

    expect(nights.map((night) => night.nightId)).toEqual(["2026-10-03", "2026-10-04", "2026-10-05"]);
    expect(nights[1].spans).toEqual([day, nextDay]);
    expect(nights[1].spans[0].start).toBe("2026-10-04T00:00:00.000Z");
    expect(nights[1].spans[0].end).toBe("2026-10-05T00:00:00.000Z");
  });

  test("puts a three-hour bin crossing local noon into both nights", () => {
    const bin = { ...utcSpan(new Date(Date.UTC(2026, 8, 28, 9)), 3 * HOUR), kp: 1 };

    const nights = groupSpansByNight([bin], "Europe/Warsaw");

    expect(nights.map((night) => night.nightId)).toEqual(["2026-09-27", "2026-09-28"]);
  });

  test("sorts nights and their spans by time", () => {
    const later = { ...utcSpan(new Date(Date.UTC(2026, 8, 29, 20)), HOUR), value: "later" };
    const earlier = { ...utcSpan(new Date(Date.UTC(2026, 8, 28, 20)), HOUR), value: "earlier" };
    const earliest = { ...utcSpan(new Date(Date.UTC(2026, 8, 28, 19)), HOUR), value: "earliest" };

    const nights = groupSpansByNight([later, earlier, earliest], "Europe/Warsaw");

    expect(nights.map((night) => night.nightId)).toEqual(["2026-09-28", "2026-09-29"]);
    expect(nights[0].spans.map((span) => span.value)).toEqual(["earliest", "earlier"]);
  });
});
