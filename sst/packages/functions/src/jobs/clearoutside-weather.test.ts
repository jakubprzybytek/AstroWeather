import { describe, expect, test, vi } from "vitest";
import { ingestClearOutside, type ClearOutsideIngestionDependencies } from "./clearoutside-weather";

const locations = {
  first: { location: { lat: 50, lon: 20 } },
  second: { location: { lat: 51, lon: 17 } }
};

const night = {
  nightId: "2026-09-11",
  hours: [{
    hour: 12,
    timestampUtc: "2026-09-11T09:00:00.000Z",
    temperatureC: 20,
    cloudCoverTotalPct: 10,
    precipitationProbabilityPct: 0,
    thunderstormRisk: false
  }]
};

function dependencies(overrides: Partial<ClearOutsideIngestionDependencies> = {}) {
  return {
    configurations: locations,
    fetchHtml: vi.fn(async () => "html"),
    parse: vi.fn(() => [night]),
    store: { getNight: vi.fn(async () => undefined), putNight: vi.fn(async () => undefined) },
    now: vi.fn(() => new Date("2026-09-13T06:00:00.000Z")),
    waitBetweenLocations: vi.fn(async () => undefined),
    ...overrides
  } satisfies ClearOutsideIngestionDependencies;
}

describe("ingestClearOutside", () => {
  test("keeps the stored earlier hours of the first night", async () => {
    const stored = { ...night, pk: "LOC#first", sk: "NIGHT#2026-09-11#WEATHER", configurationId: "first", service: "skyConditions" as const,
      coordinates: { latitude: 50, longitude: 20 }, fetchedAt: "x", expireAt: 0,
      hours: [{ ...night.hours[0], timestampUtc: "2026-09-11T08:00:00.000Z", temperatureC: 18 }] };
    const deps = dependencies({
      configurations: { first: locations.first },
      store: { getNight: vi.fn(async () => stored), putNight: vi.fn(async () => undefined) }
    });

    await ingestClearOutside(deps);

    expect(deps.store.getNight).toHaveBeenCalledWith("first", "2026-09-11");
    expect(deps.store.putNight).toHaveBeenCalledWith(expect.objectContaining({
      hours: [expect.objectContaining({ temperatureC: 18 }), expect.objectContaining({ temperatureC: 20 })]
    }));
  });

  test("fetches and stores every configured location", async () => {
    const deps = dependencies();

    const summary = await ingestClearOutside(deps);

    expect(summary).toEqual({ succeeded: ["first", "second"], failed: [] });
    expect(deps.fetchHtml).toHaveBeenNthCalledWith(1, 50, 20);
    expect(deps.fetchHtml).toHaveBeenNthCalledWith(2, 51, 17);
    expect(deps.store.putNight).toHaveBeenCalledTimes(2);
    expect(deps.store.putNight).toHaveBeenCalledWith(expect.objectContaining({
      pk: "LOC#first",
      coordinates: { latitude: 50, longitude: 20 },
      fetchedAt: "2026-09-13T06:00:00.000Z"
    }));
    expect(deps.waitBetweenLocations).toHaveBeenCalledWith(1_000);
  });

  test("continues after one location fails and throws after all attempts", async () => {
    const fetchHtml = vi.fn()
      .mockRejectedValueOnce(new Error("upstream unavailable"))
      .mockResolvedValueOnce("html");
    const deps = dependencies({ fetchHtml });

    await expect(ingestClearOutside(deps)).rejects.toThrow("first");
    expect(fetchHtml).toHaveBeenCalledTimes(2);
    expect(deps.store.putNight).toHaveBeenCalledTimes(1);
    expect(deps.store.putNight).toHaveBeenCalledWith(expect.objectContaining({ pk: "LOC#second" }));
  });

  test("does not write when parsing fails or returns no nights", async () => {
    const parse = vi.fn()
      .mockImplementationOnce(() => { throw new Error("page changed"); })
      .mockReturnValueOnce([]);
    const deps = dependencies({ parse });

    await expect(ingestClearOutside(deps)).rejects.toThrow("first, second");
    expect(deps.store.putNight).not.toHaveBeenCalled();
  });
});