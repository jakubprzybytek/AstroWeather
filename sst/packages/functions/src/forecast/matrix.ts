// Matrix cells of the num4x4_matrix5x21 board, protocol 3: each of the 21
// hourly slots is a brightness level 0-3, `a`/`b`/`c` for levels 1-3 blinking
// down (alternating with off), `*` for level 3 blinking (as `c`), or `?` when
// unavailable. See docs/api-payload.md.

export type SteadyLevel = 0 | 1 | 2 | 3;
export type MatrixCell = SteadyLevel | "a" | "b" | "c" | "*";

// How much of an hour a body is above the horizon: none, under half an hour,
// half an hour or more, the whole hour.
export function minutesLevel(minutesUp: number): MatrixCell {
  if (minutesUp <= 0) return 0;
  if (minutesUp < 30) return 1;
  if (minutesUp < 60) return 2;
  return 3;
}

// How dark the sky is for a sun altitude in degrees: 3 the sun up (above
// -0.833 degrees, suncalc's sunrise and sunset), 2 civil or nautical twilight
// (down to -12), 1 astronomical twilight (down to -18), 0 astronomical night.
export function skyLevel(sunAltitudeDegrees: number): SteadyLevel {
  if (sunAltitudeDegrees > -0.833) return 3;
  if (sunAltitudeDegrees > -12) return 2;
  if (sunAltitudeDegrees > -18) return 1;
  return 0;
}

// The level held for the most minutes of an hour; a tie goes to the brighter
// level, so that a darker level means the darker sky really prevails.
export function majorityLevel(levels: SteadyLevel[]): SteadyLevel {
  const counts = [0, 0, 0, 0];
  for (const level of levels) counts[level] += 1;
  let best = 3;
  for (let level = 2; level >= 0; level -= 1) if (counts[level] > counts[best]) best = level;
  return best as SteadyLevel;
}

// A percentage in quarters: 0-24, 25-49, 50-74, 75-100.
export function quartileLevel(percent: number): MatrixCell {
  if (percent >= 75) return 3;
  if (percent >= 50) return 2;
  if (percent >= 25) return 1;
  return 0;
}

// Cloud coverage: clear sky alone is 0, the rest in thirds: 1-33, 34-66,
// 67-100.
export function cloudLevel(percent: number): MatrixCell {
  if (percent <= 0) return 0;
  if (percent * 3 <= 100) return 1;
  if (percent * 3 <= 200) return 2;
  return 3;
}

// The row as sent: `?` when no slot is available, otherwise one character
// per slot with `?` for an unavailable one.
export function encodeMatrix(cells: Array<MatrixCell | null>): string {
  if (!cells.some((cell) => cell !== null)) return "?";
  return cells.map((cell) => cell === null ? "?" : String(cell)).join("");
}
