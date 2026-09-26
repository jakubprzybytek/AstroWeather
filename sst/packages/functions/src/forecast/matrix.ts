// Matrix cells of the num4x4_matrix5x21 board, protocol 2: each of the 21
// hourly slots is a brightness level 0-3, `*` for the brightest level with
// blinking, or `?` when unavailable. See docs/api-payload.md.

export type MatrixCell = 0 | 1 | 2 | 3 | "*";

// How much of an hour a body is above the horizon: none, under half an hour,
// half an hour or more, the whole hour.
export function minutesLevel(minutesUp: number): MatrixCell {
  if (minutesUp <= 0) return 0;
  if (minutesUp < 30) return 1;
  if (minutesUp < 60) return 2;
  return 3;
}

// A percentage in quarters: 0-24, 25-49, 50-74, 75-100.
export function quartileLevel(percent: number): MatrixCell {
  if (percent >= 75) return 3;
  if (percent >= 50) return 2;
  if (percent >= 25) return 1;
  return 0;
}

// The row as sent: `?` when no slot is available, otherwise one character
// per slot with `?` for an unavailable one.
export function encodeMatrix(cells: Array<MatrixCell | null>): string {
  if (!cells.some((cell) => cell !== null)) return "?";
  return cells.map((cell) => cell === null ? "?" : String(cell)).join("");
}
