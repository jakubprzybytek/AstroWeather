import { describe, expect, test } from "vitest";
import { toClearOutsideItem, withEarlierHours } from "./clearoutside-storage";

const night = {
  nightId: "2026-09-11",
  hours: [
    {
      hour: 12,
      timestampUtc: "2026-09-11T09:00:00.000Z",
      temperatureC: 36,
      cloudCoverTotalPct: 0,
      precipitationProbabilityPct: 0,
      thunderstormRisk: false
    },
    {
      hour: 11,
      timestampUtc: "2026-09-12T08:00:00.000Z",
      temperatureC: 25,
      cloudCoverTotalPct: 10,
      precipitationProbabilityPct: 5,
      thunderstormRisk: null
    }
  ]
};

describe("toClearOutsideItem", () => {
  test("maps a night to a deterministic item with a TTL", () => {
    const item = toClearOutsideItem(
      "krakow",
      { latitude: 50.0647, longitude: 19.945 },
      night,
      "2026-09-13T06:00:00.000Z"
    );

    expect(item).toEqual({
      pk: "LOC#krakow",
      sk: "NIGHT#2026-09-11#WEATHER",
      configurationId: "krakow",
      nightId: "2026-09-11",
      service: "skyConditions",
      coordinates: { latitude: 50.0647, longitude: 19.945 },
      hours: night.hours,
      fetchedAt: "2026-09-13T06:00:00.000Z",
      expireAt: Math.floor(Date.parse("2026-09-12T08:00:00.000Z") / 1000) + 72 * 60 * 60
    });
  });

  test("rejects a night without hourly data", () => {
    expect(() => toClearOutsideItem("wroclaw", { latitude: 51, longitude: 17 }, { nightId: "2026-09-11", hours: [] }, "2026-09-13T06:00:00.000Z"))
      .toThrow("without hourly data");
  });

  test("rejects invalid hourly timestamps", () => {
    expect(() => toClearOutsideItem(
      "wroclaw",
      { latitude: 51, longitude: 17 },
      { nightId: "2026-09-11", hours: [{ ...night.hours[0], timestampUtc: "invalid" }] },
      "2026-09-13T06:00:00.000Z"
    )).toThrow("invalid timestamp");
  });
});
describe("withEarlierHours", () => {
  const hour = (timestampUtc: string, temperatureC: number) => ({
    hour: 0, timestampUtc, temperatureC, cloudCoverTotalPct: 0, precipitationProbabilityPct: 0, thunderstormRisk: false
  });
  const item = (hours: ReturnType<typeof hour>[]) =>
    toClearOutsideItem("krakow", { latitude: 50, longitude: 20 }, { nightId: "2026-11-16", hours }, "2026-11-16T17:05:00.000Z");

  test("keeps the stored hours before the first new hour, and replaces the rest", () => {
    const stored = item([hour("2026-11-16T11:00:00.000Z", 5), hour("2026-11-16T16:00:00.000Z", 3), hour("2026-11-16T17:00:00.000Z", 2)]);
    const fresh = item([hour("2026-11-16T17:00:00.000Z", 1), hour("2026-11-16T18:00:00.000Z", 0)]);

    expect(withEarlierHours(fresh, stored).hours.map((h) => h.temperatureC)).toEqual([5, 3, 1, 0]);
  });

  test("leaves the item as it is without a stored night", () => {
    const fresh = item([hour("2026-11-16T17:00:00.000Z", 1)]);
    expect(withEarlierHours(fresh, undefined)).toBe(fresh);
  });
});
