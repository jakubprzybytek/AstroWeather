import type { ForecastDisplay } from "./types";

// Version 3: matrix cells are levels 0-3, `a`/`b`/`c` (levels 1-3 blinking
// down), `*` (level 3 blinking, as `c`) or `?`; adds `matrix_4` (aurora) and
// the `refreshIntervalMinutes` header record. A device reading version 2
// rejects this, and one reading version 3 rejects version 2.
export const PROTOCOL = 3;
const BOARD = "num4x4_matrix5x21";
const DISPLAY_KEYS = ["display", "board", "nightId", "numeric_0", "numeric_1", "matrix_0", "matrix_1", "matrix_2", "matrix_3", "matrix_4", "numeric_2", "numeric_3"] as const;

function validMatrix(value: string): boolean {
  return value === "?" || (value.length === 21 && /^[0-3abc*?]+$/.test(value));
}

function validate(display: ForecastDisplay): void {
  if (display.board !== BOARD || !/^\d{4}-\d{2}-\d{2}$/.test(display.nightId)) throw new Error("Invalid forecast display");
  if (![display.sun, display.moon, display.cloud, display.precipitation, display.aurora].every(validMatrix)) throw new Error("Invalid forecast matrix");
  if (![display.sunset, display.sunrise, display.maximumTemperature, display.minimumTemperature].every((value) => value === "?" || /^[0-9:.+-]+$/.test(value))) throw new Error("Invalid forecast value");
}

// `time` is local `YYYY-MM-DDTHH:MM:SS.mmm+HH:MM`; `lastWeatherFetchTime` is local
// `YYYY-MM-DDTHH:MM:SS+HH:MM`, or `?` without weather. The offset names the zone.
// `refreshIntervalMinutes` tells the device how often to refresh.
export function serializeForecast(configurationId: string, time: string, lastWeatherFetchTime: string, refreshIntervalMinutes: number, displays: ForecastDisplay[]): string {
  if (!/^[\x21-\x7e]+$/.test(configurationId) || displays.length !== 6) throw new Error("Invalid forecast response");
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}[+-]\d{2}:\d{2}$/.test(time)) throw new Error("Invalid forecast time");
  if (lastWeatherFetchTime !== "?" && !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}$/.test(lastWeatherFetchTime)) {
    throw new Error("Invalid last weather fetch time");
  }
  if (refreshIntervalMinutes !== 60 && refreshIntervalMinutes !== 360) throw new Error("Invalid refresh interval");
  displays.forEach((display, index) => {
    if (display.display !== index) throw new Error("Invalid display order");
    validate(display);
  });
  const lines = [`protocol=${PROTOCOL}`, `configurationId=${configurationId}`, `time=${time}`,
    `lastWeatherFetchTime=${lastWeatherFetchTime}`, `refreshIntervalMinutes=${refreshIntervalMinutes}`, ""];
  for (const [index, display] of displays.entries()) {
    if (index > 0) lines.push("");
    const values: Record<typeof DISPLAY_KEYS[number], string | number> = {
      display: display.display, board: display.board, nightId: display.nightId,
      numeric_0: display.sunset, numeric_1: display.sunrise,
      matrix_0: display.sun, matrix_1: display.moon, matrix_2: display.cloud,
      matrix_3: display.precipitation, matrix_4: display.aurora, numeric_2: display.maximumTemperature,
      numeric_3: display.minimumTemperature
    };
    lines.push(...DISPLAY_KEYS.map((key) => `${key}=${values[key]}`));
  }
  return `${lines.join("\n")}\n`;
}

export function serializeError(error: "configuration_not_found" | "forecast_unavailable"): string {
  return `protocol=${PROTOCOL}\nerror=${error}\n`;
}
