import { describe, expect, test } from "vitest";
import { toClearOutsideItem } from "./clearoutside-storage";

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
      "krakow-home",
      { latitude: 50.0647, longitude: 19.945 },
      night,
      "2026-09-13T06:00:00.000Z"
    );

    expect(item).toEqual({
      pk: "LOC#krakow-home",
      sk: "NIGHT#2026-09-11#SKY_CONDITIONS",
      configurationId: "krakow-home",
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