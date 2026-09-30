import { describe, expect, test, vi } from "vitest";
import type { GfzHp60Values } from "../aurora/gfz-hp60";
import type { OvationGrid } from "../aurora/ovation";
import { utcSpan } from "../aurora/spans";
import { addToNight, type AuroraItem, type AuroraNight, type AuroraStore, type CalibrationSample } from "../aurora/storage";
import { ingestAuroraForecast, type AuroraForecastDependencies } from "./aurora-forecast";
import { sampleAuroraNowcast, type AuroraNowcastDependencies } from "./aurora-nowcast";

const HOUR = 3_600_000;
const locations = {
  wroclaw: { location: { lat: 51.1, lon: 17.0, tz: "Europe/Warsaw" }, aurora: { kpMain: 7 } }
};

function memoryStore() {
  const items = new Map<string, AuroraItem>();
  const samples: CalibrationSample[] = [];
  const store: AuroraStore = {
    put: vi.fn(async (item: AuroraItem) => { items.set(`${item.pk}|${item.sk}`, item); }),
    putCalibration: vi.fn(async (sample: CalibrationSample) => { samples.push(sample); }),
    get: vi.fn(async (configurationId: string, nightId: string, kind: AuroraItem["source"]) =>
      items.get(`LOC#${configurationId}|NIGHT#${nightId}#AURORA#${kind}`)) as AuroraStore["get"],
    readNights: vi.fn(async () => {
      const nights = new Map<string, AuroraNight>();
      for (const item of items.values()) {
        const night = nights.get(item.nightId) ?? {};
        addToNight(night, item);
        nights.set(item.nightId, night);
      }
      return nights;
    })
  };
  return { store, items, samples };
}

function gfzHour(start: Date, values: Partial<GfzHp60Values> = {}) {
  return {
    ...utcSpan(start, HOUR), median: 1, quantile75: 2, maximum: 3,
    prob4to5: 0, prob5to6: 0, prob6to7: 0, prob7to8: 0, probAtLeast8: 0, ...values
  };
}

// 2026-10-01 10:00Z is 12:00 in Warsaw: night 2026-10-01 starts.
const NOW = new Date("2026-10-01T10:00:00Z");

function forecastDependencies(overrides: Partial<AuroraForecastDependencies> = {}) {
  const { store, items } = memoryStore();
  const dependencies: AuroraForecastDependencies = {
    configurations: locations,
    loadGfz: vi.fn(async () => ({
      spans: [gfzHour(new Date("2026-10-01T20:00:00Z")), gfzHour(new Date("2026-10-02T20:00:00Z"), { quantile75: 5.3 })],
      lastModified: "Thu, 01 Oct 2026 09:05:00 GMT"
    })),
    loadNoaa3: vi.fn(async () => ({
      spans: [{ ...utcSpan(new Date("2026-09-24T00:00:00Z"), 3 * HOUR), kp: 9, status: "observed" as const, noaaScale: null },
        { ...utcSpan(new Date("2026-10-01T21:00:00Z"), 3 * HOUR), kp: 2, status: "predicted" as const, noaaScale: null }],
      lastModified: null
    })),
    loadNoaa27: vi.fn(async () => ({
      spans: [{ ...utcSpan(new Date("2026-10-20T00:00:00Z"), 24 * HOUR), largestKp: 3, ap: 5, f107: 100 },
        { ...utcSpan(new Date("2026-10-02T00:00:00Z"), 24 * HOUR), largestKp: 3, ap: 5, f107: 100 }],
      lastModified: null, issuedAt: "2026-09-28T02:21:00.000Z"
    })),
    store,
    now: () => NOW,
    log: vi.fn(),
    ...overrides
  };
  return { dependencies, items };
}

describe("ingestAuroraForecast", () => {
  test("stores each source per displayed night and flags the triggering night once", async () => {
    const { dependencies, items } = forecastDependencies();

    const summary = await ingestAuroraForecast(dependencies);

    expect(summary).toEqual({ stored: ["GFZ", "NOAA3", "NOAA27"], failed: [], flagged: ["wroclaw/2026-10-02"] });
    expect([...items.keys()].sort()).toEqual([
      "LOC#wroclaw|NIGHT#2026-10-01#AURORA#GFZ",
      "LOC#wroclaw|NIGHT#2026-10-01#AURORA#NOAA27",
      "LOC#wroclaw|NIGHT#2026-10-01#AURORA#NOAA3",
      "LOC#wroclaw|NIGHT#2026-10-02#AURORA#FLAG",
      "LOC#wroclaw|NIGHT#2026-10-02#AURORA#GFZ",
      "LOC#wroclaw|NIGHT#2026-10-02#AURORA#NOAA27"
    ]);
    // Old observed bins and outlook days beyond the six nights are not stored.
    expect(items.get("LOC#wroclaw|NIGHT#2026-10-01#AURORA#NOAA27")).toMatchObject({ issuedAt: "2026-09-28T02:21:00.000Z" });

    expect(dependencies.log).toHaveBeenCalledWith("Aurora forecast source stored", expect.objectContaining({
      source: "GFZ", lastModified: "Thu, 01 Oct 2026 09:05:00 GMT", spans: 2
    }));
    expect(dependencies.log).toHaveBeenCalledWith("Aurora forecast outlook", expect.objectContaining({
      configurationId: "wroclaw", kpMain: 7,
      nights: expect.arrayContaining([
        expect.objectContaining({ nightId: "2026-10-01", gfzQ75Max: 2, noaaKpMax: 2, flag: "no" }),
        expect.objectContaining({ nightId: "2026-10-02", gfzQ75Max: 5.3, flag: "new" })
      ])
    }));

    expect((await ingestAuroraForecast(dependencies)).flagged).toEqual([]);
    expect(dependencies.log).toHaveBeenLastCalledWith("Aurora forecast ingestion completed", expect.anything());
    expect(dependencies.log).toHaveBeenCalledWith("Aurora forecast outlook", expect.objectContaining({
      nights: expect.arrayContaining([expect.objectContaining({ nightId: "2026-10-02", flag: "kept" })])
    }));
  });

  test("stores the other sources when one fails, then reports the failure", async () => {
    const { dependencies, items } = forecastDependencies({ loadGfz: vi.fn().mockRejectedValue(new Error("HTTP 503")) });

    await expect(ingestAuroraForecast(dependencies)).rejects.toThrow("GFZ");
    expect([...items.keys()].some((key) => key.endsWith("#GFZ"))).toBe(false);
    expect(items.has("LOC#wroclaw|NIGHT#2026-10-01#AURORA#NOAA3")).toBe(true);
  });
});

// 20:10Z on 1 October; the nowcast is valid at 21:20Z, in the slot from 21:00Z.
const DARK_NOW = new Date("2026-10-01T20:10:00Z");

function grid(pct: number): OvationGrid {
  return {
    observedAt: "2026-10-01T20:03:00.000Z",
    validAt: "2026-10-01T21:20:00.000Z",
    probability: (latitude) => latitude < 52 ? pct : pct + 10
  };
}

function nowcastDependencies(flagged: boolean, overrides: Partial<AuroraNowcastDependencies> = {}) {
  const { store, items, samples } = memoryStore();
  if (flagged) {
    items.set("LOC#wroclaw|NIGHT#2026-10-01#AURORA#FLAG", {
      pk: "LOC#wroclaw", sk: "NIGHT#2026-10-01#AURORA#FLAG", configurationId: "wroclaw",
      nightId: "2026-10-01", source: "FLAG", reason: "test", fetchedAt: NOW.toISOString(), expireAt: 2_000_000_000
    });
  }
  const dependencies: AuroraNowcastDependencies = {
    configurations: locations,
    store,
    now: () => DARK_NOW,
    loadOvation: vi.fn(async () => grid(12)),
    refreshForecasts: vi.fn(async () => undefined),
    loadLiveKp: vi.fn(async () => ({ kp: 2, at: "2026-10-01T20:05:00.000Z", latestAt: "2026-10-01T20:09:00.000Z" })),
    isDark: () => true,
    log: vi.fn(),
    ...overrides
  };
  return { dependencies, items, samples };
}

describe("sampleAuroraNowcast", () => {
  test("does nothing on a night that is not flagged", async () => {
    const { dependencies } = nowcastDependencies(false);

    expect(await sampleAuroraNowcast(dependencies)).toEqual({ active: [], stored: [] });
    expect(dependencies.loadOvation).not.toHaveBeenCalled();
    expect(dependencies.log).toHaveBeenCalledWith("Aurora nowcast skipped", {
      locations: [expect.objectContaining({ configurationId: "wroclaw", nightId: "2026-10-01", flagged: false, dark: true })]
    });
    expect(dependencies.refreshForecasts).not.toHaveBeenCalled();
  });

  test("does nothing in daylight", async () => {
    const { dependencies } = nowcastDependencies(true, { isDark: () => false, sunAltitude: () => 12.5 });

    expect((await sampleAuroraNowcast(dependencies)).active).toEqual([]);
    expect(dependencies.log).toHaveBeenCalledWith("Aurora nowcast skipped", {
      locations: [expect.objectContaining({ flagged: true, dark: false, sunAltitudeDeg: 12.5 })]
    });
    expect(dependencies.loadOvation).not.toHaveBeenCalled();
    expect(dependencies.loadLiveKp).not.toHaveBeenCalled();
  });

  test("flags and samples a storm the forecasts missed, from NOAA's live Kp", async () => {
    const { dependencies, items } = nowcastDependencies(false, {
      loadLiveKp: vi.fn(async () => ({ kp: 5.33, at: "2026-10-01T20:02:00.000Z", latestAt: "2026-10-01T20:09:00.000Z" }))
    });

    const summary = await sampleAuroraNowcast(dependencies);

    expect(summary.active).toEqual(["wroclaw"]);
    expect(items.get("LOC#wroclaw|NIGHT#2026-10-01#AURORA#FLAG")).toMatchObject({ reason: "live kp=5.33 at 2026-10-01T20:02:00.000Z" });
    expect(dependencies.log).toHaveBeenCalledWith("Aurora nowcast sampling", {
      locations: [expect.objectContaining({ flagged: true, liveKp: 5.33, liveTriggers: true })]
    });
  });

  test("keeps to the flag when the live Kp cannot be read", async () => {
    const { dependencies } = nowcastDependencies(false, { loadLiveKp: vi.fn().mockRejectedValue(new Error("HTTP 503")) });

    expect((await sampleAuroraNowcast(dependencies)).active).toEqual([]);
    expect(dependencies.log).toHaveBeenCalledWith("Aurora nowcast live Kp failed", { error: "HTTP 503" });
  });

  test("stores the nowcast in the slot it describes, keeping the hour's maximum, and logs a sample", async () => {
    const { dependencies, items, samples } = nowcastDependencies(true);

    await sampleAuroraNowcast(dependencies);
    dependencies.loadOvation = vi.fn(async () => grid(4));
    const summary = await sampleAuroraNowcast(dependencies);

    expect(summary).toEqual({ active: ["wroclaw"], stored: ["wroclaw/2026-10-01T21:00:00.000Z"] });
    const item = items.get("LOC#wroclaw|NIGHT#2026-10-01#AURORA#OVATION");
    expect(item?.source === "OVATION" && item.slots["2026-10-01T21:00:00.000Z"]).toEqual({
      latestPct: 4, maxPct: 12, latestValidAt: "2026-10-01T21:20:00.000Z", samples: 2
    });
    expect(dependencies.refreshForecasts).toHaveBeenCalledTimes(2);
    expect(dependencies.log).toHaveBeenCalledWith("aurora-calibration", expect.objectContaining({
      configurationId: "wroclaw", cellPct: 4, validAt: "2026-10-01T21:20:00.000Z",
      northPct: [14, 14, 14, 14, 14, 14, 14, 14], liveKp: 2
    }));
    // Kept for good: one sample per run, without an expiry.
    expect(samples).toHaveLength(2);
    expect(samples[1]).toMatchObject({ configurationId: "wroclaw", observedAt: "2026-10-01T20:03:00.000Z", cellPct: 4 });
    expect(samples[1]).not.toHaveProperty("expireAt");
  });
});
