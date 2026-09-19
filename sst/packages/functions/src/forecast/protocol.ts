import type { ForecastDisplay } from "./types";

const BOARD = "num4x4_matrix5x21";
const DISPLAY_KEYS = ["display", "board", "nightId", "numerical_0", "numerical_1", "matrix_0", "matrix_1", "matrix_2", "matrix_3", "numerical_2", "numerical_3"] as const;

function validMatrix(value: string): boolean {
  return value === "?" || (value.length === 21 && /^[*.?]+$/.test(value));
}

function validate(display: ForecastDisplay): void {
  if (display.board !== BOARD || !/^\d{4}-\d{2}-\d{2}$/.test(display.nightId)) throw new Error("Invalid forecast display");
  if (![display.sun, display.moon, display.cloud, display.thunderstorm].every(validMatrix)) throw new Error("Invalid forecast matrix");
  if (![display.sunset, display.sunrise, display.maximumTemperature, display.minimumTemperature].every((value) => value === "?" || /^[0-9:.+-]+$/.test(value))) throw new Error("Invalid forecast value");
}

export function serializeForecast(configurationId: string, displays: ForecastDisplay[]): string {
  if (!/^[\x21-\x7e]+$/.test(configurationId) || displays.length !== 6) throw new Error("Invalid forecast response");
  displays.forEach((display, index) => {
    if (display.display !== index) throw new Error("Invalid display order");
    validate(display);
  });
  const lines = ["protocol=1", `configurationId=${configurationId}`, ""];
  for (const [index, display] of displays.entries()) {
    if (index > 0) lines.push("");
    const values: Record<typeof DISPLAY_KEYS[number], string | number> = {
      display: display.display, board: display.board, nightId: display.nightId,
      numerical_0: display.sunset, numerical_1: display.sunrise,
      matrix_0: display.sun, matrix_1: display.moon, matrix_2: display.cloud,
      matrix_3: display.thunderstorm, numerical_2: display.maximumTemperature,
      numerical_3: display.minimumTemperature
    };
    lines.push(...DISPLAY_KEYS.map((key) => `${key}=${values[key]}`));
  }
  return `${lines.join("\n")}\n`;
}

export function serializeError(error: "configuration_not_found" | "forecast_unavailable"): string {
  return `protocol=1\nerror=${error}\n`;
}