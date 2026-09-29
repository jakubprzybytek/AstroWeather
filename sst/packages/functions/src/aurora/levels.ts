import type { GfzHp60Values } from "./gfz-hp60";

// Levels of the aurora row; see docs/aurora-forecast-supplier.md#levels.
// `kpMain` is the location's naked-eye threshold (configurations.ts): level 1
// one Kp below it, level 2 at it, level 3 one above.
export type AuroraLevel = 0 | 1 | 2 | 3;

export function levelForKp(kp: number, kpMain: number): AuroraLevel {
  if (kp >= kpMain + 1) return 3;
  if (kp >= kpMain) return 2;
  if (kp >= kpMain - 1) return 1;
  return 0;
}

// The storm-night flag fires one Kp step below level 1, so that it errs eager.
export function flagKp(kpMain: number): number {
  return kpMain - 2;
}

// Placeholder OVATION % thresholds until calibration; see
// docs/aurora-forecast-supplier.md#calibrating-the-nowcast.
export const NOWCAST_THRESHOLDS_PCT = { 1: 5, 2: 15, 3: 40 } as const;

export function levelForNowcast(percent: number): AuroraLevel {
  if (percent >= NOWCAST_THRESHOLDS_PCT[3]) return 3;
  if (percent >= NOWCAST_THRESHOLDS_PCT[2]) return 2;
  if (percent >= NOWCAST_THRESHOLDS_PCT[1]) return 1;
  return 0;
}

// The ensemble's probability of reaching at least `kp`. GFZ publishes whole
// bands only, so a fractional threshold is rounded down, which errs eager.
export function probabilityAtLeast(values: GfzHp60Values, kp: number): number {
  const bands: Array<[number, number]> = [
    [4, values.prob4to5], [5, values.prob5to6], [6, values.prob6to7],
    [7, values.prob7to8], [8, values.probAtLeast8]
  ];
  const floor = Math.floor(kp);
  return bands.filter(([lower]) => lower >= floor).reduce((sum, [, probability]) => sum + probability, 0);
}

// Whether a GFZ hour or a NOAA bin makes a night a storm night: the
// ensemble's 0.75-quantile at the flag Kp, a 25 % chance of level 1, or a
// NOAA bin at the flag Kp.
export function gfzTriggers(values: GfzHp60Values, kpMain: number): boolean {
  return values.quantile75 >= flagKp(kpMain) || probabilityAtLeast(values, kpMain - 1) >= 0.25;
}

export function noaaTriggers(kp: number, kpMain: number): boolean {
  return kp >= flagKp(kpMain);
}
