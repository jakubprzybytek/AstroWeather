import { observingSlots } from "../forecast/nights";
import type { MatrixCell } from "../forecast/matrix";
import { levelForKp, levelForNowcast, type AuroraLevel } from "./levels";
import type { AuroraNight } from "./storage";
import type { Span } from "./spans";

// Merges the aurora sources of one night into the 21 slots of the row; see
// docs/aurora-forecast-supplier.md#merging-sources and #showing-the-row-on-the-device.

export type AuroraSlotSource = "noaa-observed" | "gfz-hp60" | "noaa-3day" | "noaa-27day";

export type AuroraSlot = {
  start: Date;
  level: AuroraLevel | null;          // null when no source covers the slot
  source: AuroraSlotSource | null;
  kp: number | null;
  nowcastPct: number | null;          // the hour's maximum, when the nowcast targeted it
  blink: boolean;                     // blink down: the nowcast, now or next hour
};

// GFZ is used while the file was fresh when fetched, and the fetch is recent
// enough to have missed at most one six-hourly run.
const GFZ_FILE_AGE_LIMIT_MS = 3 * 60 * 60 * 1000;
const GFZ_FETCH_AGE_LIMIT_MS = 7 * 60 * 60 * 1000;

const HOUR_MS = 60 * 60 * 1000;

export function gfzUsable(item: AuroraNight["gfz"], now: Date): boolean {
  if (!item) return false;
  const fetchedAt = Date.parse(item.fetchedAt);
  if (Number.isNaN(fetchedAt) || now.getTime() - fetchedAt > GFZ_FETCH_AGE_LIMIT_MS) return false;
  const modified = item.lastModified ? Date.parse(item.lastModified) : fetchedAt;
  return !Number.isNaN(modified) && fetchedAt - modified <= GFZ_FILE_AGE_LIMIT_MS;
}

function covering<T>(spans: Span<T>[] | undefined, instant: number): Span<T> | undefined {
  return spans?.find((span) => Date.parse(span.start) <= instant && instant < Date.parse(span.end));
}

export function mergeAurora(nightId: string, timezone: string, kpMain: number, night: AuroraNight, now: Date): AuroraSlot[] {
  const observed = night.noaa3?.spans.filter((span) => span.status !== "predicted");
  const predicted = night.noaa3?.spans.filter((span) => span.status === "predicted");
  const gfz = gfzUsable(night.gfz, now) ? night.gfz?.spans : undefined;

  return observingSlots(nightId, timezone).map((slot) => {
    const midpoint = slot.midpoint.getTime();
    let level: AuroraLevel | null = null;
    let source: AuroraSlotSource | null = null;
    let kp: number | null = null;

    const observedSpan = covering(observed, midpoint);
    const gfzSpan = observedSpan ? undefined : covering(gfz, midpoint);
    const predictedSpan = observedSpan || gfzSpan ? undefined : covering(predicted, midpoint);
    if (observedSpan) {
      [source, kp] = ["noaa-observed", observedSpan.kp];
    } else if (gfzSpan) {
      [source, kp] = ["gfz-hp60", gfzSpan.median];
    } else if (predictedSpan) {
      [source, kp] = ["noaa-3day", predictedSpan.kp];
    }
    if (kp !== null) {
      level = levelForKp(kp, kpMain);
    } else {
      // A daily maximum cannot carry an hour's intensity: "possible" at most.
      const outlook = covering(night.noaa27?.spans, midpoint);
      if (outlook) {
        [source, kp] = ["noaa-27day", outlook.largestKp];
        level = levelForKp(outlook.largestKp, kpMain) > 0 ? 1 : 0;
      }
    }

    // The nowcast only raises a slot until it is calibrated, and blinks for the
    // hour it is fetched in and the next, the hours it can describe.
    const nowcast = night.ovation?.slots[slot.start.toISOString()];
    const nowcastPct = nowcast?.maxPct ?? null;
    let blink = false;
    if (nowcastPct !== null) {
      const nowcastLevel = levelForNowcast(nowcastPct);
      if (level === null || nowcastLevel > level) level = nowcastLevel;
      const offset = slot.start.getTime() - now.getTime();
      blink = nowcastLevel > 0 && offset > -HOUR_MS && offset < HOUR_MS;
    }

    return { start: slot.start, level, source, kp, nowcastPct, blink };
  });
}

const BLINK_DOWN: Record<1 | 2 | 3, MatrixCell> = { 1: "a", 2: "b", 3: "c" };

export function auroraCell(slot: AuroraSlot): MatrixCell | null {
  if (slot.level === null) return null;
  return slot.blink && slot.level > 0 ? BLINK_DOWN[slot.level as 1 | 2 | 3] : slot.level;
}
