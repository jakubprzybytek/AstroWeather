import { describe, expect, test, vi } from "vitest";
import { assembleForecast } from "./assemble";

describe("assembleForecast", () => {
  test("keeps six displays and available astronomy when weather fails", async () => {
    const log = vi.fn();
    const result = await assembleForecast("krakow", { lat: 50, lon: 20, tz: "Europe/Warsaw" }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: vi.fn().mockRejectedValue(new Error("Dynamo unavailable")),
      log
    });

    expect(result.displays).toHaveLength(6);
    expect(result.displays[0].nightId).toBe("2026-09-17");
    expect(result.displays[0].sunset).not.toBe("?");
    expect(result.displays[0].cloud).toBe("?");
    expect(result.lastWeatherFetch).toBeUndefined();
    expect(log).toHaveBeenCalledWith("Forecast weather failed", expect.objectContaining({ source: "weather" }));
  });

  test("keeps weather when astronomy fails", async () => {
    const item = {
      pk: "LOC#krakow", sk: "NIGHT#2026-09-17#WEATHER", configurationId: "krakow",
      nightId: "2026-09-17", service: "skyConditions" as const,
      coordinates: { latitude: 50, longitude: 20 }, fetchedAt: "2026-09-17T00:00:00Z",
      expireAt: 2_000_000_000,
      hours: [{ hour: 14, timestampUtc: "2026-09-17T12:00:00.000Z", temperatureC: 18.5, cloudCoverTotalPct: 20, precipitationProbabilityPct: 0, thunderstormRisk: false }]
    };
    const result = await assembleForecast("krakow", { lat: Number.NaN, lon: 20, tz: "Europe/Warsaw" }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: async () => new Map([["2026-09-17", item]]),
      log: vi.fn()
    });

    expect(result.displays[0].sun).toBe("?");
    expect(result.displays[0].maximumTemperature).toBe("18.5");
    expect(result.displays[0].cloud[0]).toBe("*");
  });

  test("reports the newest weather fetch among the nights used", async () => {
    const item = (nightId: string, fetchedAt: string) => ({
      pk: "LOC#krakow", sk: `NIGHT#${nightId}#WEATHER`, configurationId: "krakow",
      nightId, service: "skyConditions" as const,
      coordinates: { latitude: 50, longitude: 20 }, fetchedAt, expireAt: 2_000_000_000, hours: []
    });
    const result = await assembleForecast("krakow", { lat: 50, lon: 20, tz: "Europe/Warsaw" }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: async () => new Map([
        ["2026-09-17", item("2026-09-17", "2026-09-17T04:00:03.000Z")],
        ["2026-09-18", item("2026-09-18", "2026-09-16T22:00:05.000Z")],
        ["2026-09-19", item("2026-09-19", "not a date")]
      ]),
      log: vi.fn()
    });

    expect(result.lastWeatherFetch?.toISOString()).toBe("2026-09-17T04:00:03.000Z");
  });
});