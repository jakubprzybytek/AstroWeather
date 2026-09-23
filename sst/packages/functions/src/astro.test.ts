import { describe, expect, test, vi } from "vitest";
import { createHandler } from "./astro";

function handlerAt(instant: string) {
  return createHandler({
    now: () => new Date(instant),
    readWeather: async () => new Map(),
    log: vi.fn()
  });
}

describe("astro handler", () => {
  test("renders the response time in the configuration's local timezone", async () => {
    const response = await handlerAt("2026-09-22T21:22:45.678Z")({ pathParameters: { configurationId: "krakow" } });

    expect(response.statusCode).toBe(200);
    expect(response.headers["content-type"]).toBe("text/plain; charset=utf-8");
    expect(response.body.split("\n").slice(0, 5)).toEqual([
      "protocol=1", "configurationId=krakow", "time=2026-09-22T23:22:45.678+02:00",
      "lastWeatherFetchTime=?", ""
    ]);
  });

  test("renders the last weather fetch time in local time with its offset, to the second", async () => {
    const handler = createHandler({
      now: () => new Date("2026-09-22T21:22:45.678Z"),
      readWeather: async (_configurationId, nightIds) => new Map([[nightIds[0], {
        pk: "LOC#krakow", sk: `NIGHT#${nightIds[0]}#WEATHER`, configurationId: "krakow",
        nightId: nightIds[0], service: "skyConditions" as const,
        coordinates: { latitude: 50, longitude: 20 }, fetchedAt: "2026-09-22T16:00:04.321Z",
        expireAt: 2_000_000_000, hours: []
      }]]),
      log: vi.fn()
    });
    const response = await handler({ pathParameters: { configurationId: "krakow" } });

    expect(response.body).toContain("\nlastWeatherFetchTime=2026-09-22T18:00:04+02:00\n");
  });

  test("pads the milliseconds to three digits", async () => {
    const response = await handlerAt("2026-09-22T21:22:45.007Z")({ pathParameters: { configurationId: "krakow" } });

    expect(response.body).toContain("\ntime=2026-09-22T23:22:45.007+02:00\n");
  });

  test("uses the winter offset after the DST change", async () => {
    const response = await handlerAt("2026-12-01T23:30:00Z")({ pathParameters: { configurationId: "wroclaw" } });

    expect(response.body).toContain("\ntime=2026-12-02T00:30:00.000+01:00\n");
  });

  test("gives each time record the offset in force at its own instant", async () => {
    const handler = createHandler({
      now: () => new Date("2026-10-25T01:30:00Z"),
      readWeather: async (_configurationId, nightIds) => new Map([[nightIds[0], {
        pk: "LOC#krakow", sk: `NIGHT#${nightIds[0]}#WEATHER`, configurationId: "krakow",
        nightId: nightIds[0], service: "skyConditions" as const,
        coordinates: { latitude: 50, longitude: 20 }, fetchedAt: "2026-10-25T00:30:00.000Z",
        expireAt: 2_000_000_000, hours: []
      }]]),
      log: vi.fn()
    });
    const response = await handler({ pathParameters: { configurationId: "krakow" } });

    // 02:30 happens twice in Poland on 2026-10-25; only the offsets tell them apart.
    expect(response.body).toContain("\ntime=2026-10-25T02:30:00.000+01:00\n");
    expect(response.body).toContain("\nlastWeatherFetchTime=2026-10-25T02:30:00+02:00\n");
  });

  test("returns the versioned error without a time for an unknown configuration", async () => {
    const response = await handlerAt("2026-09-22T21:22:45Z")({ pathParameters: { configurationId: "unknown" } });

    expect(response.statusCode).toBe(404);
    expect(response.body).toBe("protocol=1\nerror=configuration_not_found\n");
  });
});
