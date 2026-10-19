import { describe, expect, test, vi } from "vitest";
import type { Noaa27Item, OvationItem } from "../aurora/storage";
import { assembleForecast } from "./assemble";

describe("assembleForecast", () => {
  test("keeps six displays and available astronomy when weather fails", async () => {
    const log = vi.fn();
    const result = await assembleForecast("krakow", { lat: 50, lon: 20, tz: "Europe/Warsaw" }, { kpMain: 7.5 }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: vi.fn().mockRejectedValue(new Error("Dynamo unavailable")),
      readAurora: async () => new Map(),
      log
    });

    expect(result.displays).toHaveLength(6);
    expect(result.displays[0].nightId).toBe("2026-09-17");
    expect(result.displays[0].astronomicalDusk).not.toBe("?");
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
      hours: [{ hour: 14, timestampUtc: "2026-09-17T12:00:00.000Z", temperatureC: 18.5, cloudCoverTotalPct: 60, precipitationProbabilityPct: 30, thunderstormRisk: true }]
    };
    const result = await assembleForecast("krakow", { lat: Number.NaN, lon: 20, tz: "Europe/Warsaw" }, { kpMain: 7.5 }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: async () => new Map([["2026-09-17", item]]),
      readAurora: async () => new Map(),
      log: vi.fn()
    });

    expect(result.displays[0].sun).toBe("?");
    expect(result.displays[0].maximumTemperature).toBe("19");
    expect(result.displays[0].cloud).toBe("2????????????????????");
    expect(result.displays[0].precipitation).toBe("*????????????????????");
  });

  test("grades the sun row by twilight, and gives astronomical dusk and dawn", async () => {
    const krakow = (now: string) => assembleForecast("krakow", { lat: 50.06, lon: 19.94, tz: "Europe/Warsaw" }, { kpMain: 7.5 }, {
      now: () => new Date(now),
      readWeather: async () => new Map(),
      readAurora: async () => new Map(),
      log: vi.fn()
    });
    // 2026-10-19 in Krakow: sunset 17:43, astronomical dusk 19:31, astronomical
    // dawn 05:22, sunrise 07:01. 17:00 is mostly up, 18:00 mostly civil and
    // nautical twilight, 19:00 just mostly astronomical twilight; 05:00 too.
    const october = (await krakow("2026-10-19T10:00:00Z")).displays[0];
    expect(october.astronomicalDusk).toBe("19:31");
    expect(october.astronomicalDawn).toBe("05:22");
    expect(october.sun).toBe("333321000000000123333");
    expect(october.moon).toMatch(/^[0-3]{21}$/);

    // 2026-06-21: the sun gets no lower than about -16.6 degrees, so there is
    // no astronomical night: no dusk or dawn, and the darkest hours are 1.
    const june = (await krakow("2026-06-21T10:00:00Z")).displays[0];
    expect(june.astronomicalDusk).toBe("?");
    expect(june.astronomicalDawn).toBe("?");
    expect(june.sun).toBe("333333322111122333333");
  });

  test("reports the newest weather fetch among the nights used", async () => {
    const item = (nightId: string, fetchedAt: string) => ({
      pk: "LOC#krakow", sk: `NIGHT#${nightId}#WEATHER`, configurationId: "krakow",
      nightId, service: "skyConditions" as const,
      coordinates: { latitude: 50, longitude: 20 }, fetchedAt, expireAt: 2_000_000_000, hours: []
    });
    const result = await assembleForecast("krakow", { lat: 50, lon: 20, tz: "Europe/Warsaw" }, { kpMain: 7.5 }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: async () => new Map([
        ["2026-09-17", item("2026-09-17", "2026-09-17T04:00:03.000Z")],
        ["2026-09-18", item("2026-09-18", "2026-09-16T22:00:05.000Z")],
        ["2026-09-19", item("2026-09-19", "not a date")]
      ]),
      readAurora: async () => new Map(),
      log: vi.fn()
    });

    expect(result.lastWeatherFetch?.toISOString()).toBe("2026-09-17T04:00:03.000Z");
  });

  test("reports the newest fetch of each aurora feed among the nights read", async () => {
    const base = (nightId: string, source: string, fetchedAt: string) => ({
      pk: "LOC#krakow", sk: `NIGHT#${nightId}#AURORA#${source}`, configurationId: "krakow",
      nightId, fetchedAt, expireAt: 2_000_000_000
    });
    const noaa27 = (nightId: string, fetchedAt: string): Noaa27Item =>
      ({ ...base(nightId, "NOAA27", fetchedAt), source: "NOAA27", lastModified: null, spans: [] });
    const ovation = (nightId: string, fetchedAt: string): OvationItem =>
      ({ ...base(nightId, "OVATION", fetchedAt), source: "OVATION", slots: {} });
    const result = await assembleForecast("krakow", { lat: 50, lon: 20, tz: "Europe/Warsaw" }, { kpMain: 7.5 }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: async () => new Map(),
      readAurora: async () => new Map([
        ["2026-09-17", { noaa27: noaa27("2026-09-17", "2026-09-16T12:03:41.000Z"), ovation: ovation("2026-09-17", "not a date") }],
        ["2026-09-20", { noaa27: noaa27("2026-09-20", "2026-09-17T06:03:41.000Z") }]
      ]),
      log: vi.fn()
    });

    expect(result.lastAuroraFetch.noaa27?.toISOString()).toBe("2026-09-17T06:03:41.000Z");
    expect(result.lastAuroraFetch.gfz).toBeUndefined();
    expect(result.lastAuroraFetch.noaa3).toBeUndefined();
    expect(result.lastAuroraFetch.ovation).toBeUndefined();
  });

  test("reports no aurora fetch when the aurora read fails", async () => {
    const result = await assembleForecast("krakow", { lat: 50, lon: 20, tz: "Europe/Warsaw" }, { kpMain: 7.5 }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: async () => new Map(),
      readAurora: async () => { throw new Error("DynamoDB down"); },
      log: vi.fn()
    });

    expect(result.lastAuroraFetch).toEqual({});
  });
});