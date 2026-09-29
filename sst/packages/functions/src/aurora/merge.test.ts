import { describe, expect, test } from "vitest";
import { encodeMatrix } from "../forecast/matrix";
import type { GfzHp60Values } from "./gfz-hp60";
import { auroraCell, gfzUsable, mergeAurora } from "./merge";
import type { NoaaKpValues } from "./noaa-kp";
import { utcSpan, type Span } from "./spans";
import type { AuroraNight, GfzItem, Noaa27Item, Noaa3Item, OvationItem } from "./storage";

// Night 2026-10-01 in Warsaw, CEST: slot i covers 12:00Z + i hours, so slot 12
// starts at 00:00Z on 2 October, when the next UTC day begins.
const NIGHT = "2026-10-01";
const TZ = "Europe/Warsaw";
const HOUR = 3_600_000;

function at(utcHour: number, day = 1): Date {
  return new Date(Date.UTC(2026, 9, day, utcHour));
}

function base(source: string) {
  return { pk: "LOC#wroclaw", sk: `NIGHT#${NIGHT}#AURORA#${source}`, configurationId: "wroclaw", nightId: NIGHT, expireAt: 2_000_000_000 };
}

function gfz(medians: Array<[number, number]>, fetchedAt: Date, lastModified: Date | null = fetchedAt): GfzItem {
  const spans: Span<GfzHp60Values>[] = medians.map(([hour, median]) => ({
    ...utcSpan(at(hour), HOUR), median, quantile75: median, maximum: median,
    prob4to5: 0, prob5to6: 0, prob6to7: 0, prob7to8: 0, probAtLeast8: 0
  }));
  return { ...base("GFZ"), source: "GFZ", spans, lastModified: lastModified?.toUTCString() ?? null, fetchedAt: fetchedAt.toISOString() };
}

function noaa3(bins: Array<[number, number, NoaaKpValues["status"]]>): Noaa3Item {
  return {
    ...base("NOAA3"), source: "NOAA3", lastModified: null, fetchedAt: at(10).toISOString(),
    spans: bins.map(([hour, kp, status]) => ({ ...utcSpan(at(hour), 3 * HOUR), kp, status, noaaScale: null }))
  };
}

function noaa27(days: Array<[number, number]>): Noaa27Item {
  return {
    ...base("NOAA27"), source: "NOAA27", lastModified: null, fetchedAt: at(10).toISOString(),
    spans: days.map(([day, largestKp]) => ({ ...utcSpan(at(0, day), 24 * HOUR), largestKp, ap: 5, f107: 100 }))
  };
}

function ovation(slots: Array<[number, number]>): OvationItem {
  return {
    ...base("OVATION"), source: "OVATION", fetchedAt: at(10).toISOString(),
    slots: Object.fromEntries(slots.map(([hour, maxPct]) => [at(hour).toISOString(),
      { latestPct: maxPct, maxPct, latestValidAt: at(hour).toISOString(), samples: 1 }]))
  };
}

function row(night: AuroraNight, now: Date, kpMain = 7): string {
  return encodeMatrix(mergeAurora(NIGHT, TZ, kpMain, night, now).map(auroraCell));
}

const hours = (from: number, to: number, value: number): Array<[number, number]> =>
  Array.from({ length: to - from }, (_, index) => [from + index, value]);

describe("mergeAurora", () => {
  test("takes each slot from the best source covering it: GFZ, then NOAA 3-day, then the outlook", () => {
    const now = at(10);
    const night = {
      gfz: gfz(hours(12, 18, 7), at(9), at(8)),
      noaa3: noaa3([[18, 6, "predicted"], [21, 8, "predicted"]]),
      noaa27: noaa27([[1, 2], [2, 7]])
    };

    // GFZ 12-17Z at Kp 7, NOAA 18-20Z at 6 and 21-23Z at 8, then 2 October's
    // outlook: Kp 7, capped at "possible".
    expect(row(night, now)).toBe("222222111333111111111");
  });

  test("splits a night covered only by the outlook at the UTC day boundary", () => {
    expect(row({ noaa27: noaa27([[1, 2], [2, 6]]) }, at(10))).toBe("000000000000111111111");
  });

  test("leaves the row unavailable without any source", () => {
    expect(row({}, at(10))).toBe("?");
  });

  test("skips GFZ when the fetch is old or the file was stale when fetched", () => {
    const night = (item: GfzItem) => ({ gfz: item, noaa3: noaa3([[12, 6, "predicted"]]) });
    const now = at(10);

    expect(row(night(gfz(hours(12, 15, 8), at(9), at(8))), now).slice(0, 3)).toBe("333");
    expect(gfzUsable(gfz([], new Date(now.getTime() - 8 * HOUR)), now)).toBe(false);
    expect(row(night(gfz(hours(12, 15, 8), new Date(now.getTime() - 8 * HOUR))), now).slice(0, 3)).toBe("111");
    expect(row(night(gfz(hours(12, 15, 8), at(9), at(5))), now).slice(0, 3)).toBe("111");
  });

  test("shows past hours from NOAA's observed and estimated bins", () => {
    const now = at(20, 1);
    const night = {
      gfz: gfz(hours(20, 24, 1), at(19)),
      noaa3: noaa3([[12, 7, "observed"], [15, 6, "estimated"], [18, 2, "estimated"]])
    };

    expect(row(night, now).slice(0, 12)).toBe("222111000000");
  });

  test("raises slots with the nowcast and blinks the current and next hour", () => {
    const now = new Date(at(20).getTime() + 10 * 60_000);
    const night = {
      gfz: gfz([...hours(18, 24, 0), [16, 8]], at(20)),
      noaa3: noaa3([[12, 8, "observed"]]),
      ovation: ovation([[16, 0], [18, 50], [20, 3], [21, 20]])
    };

    const cells = row(night, now);
    expect(cells[4]).toBe("3");   // 16Z: forecast level 3, the nowcast never lowers it
    expect(cells[6]).toBe("3");   // 18Z: raised by the nowcast, past, steady
    expect(cells[8]).toBe("0");   // 20Z: current, nowcast level 0, no blink
    expect(cells[9]).toBe("b");   // 21Z: next hour, raised to 2 and blinking
    expect(cells[10]).toBe("0");
  });
});
