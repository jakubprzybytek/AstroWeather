import { describe, expect, test } from "vitest";
import { serializeError, serializeForecast } from "./protocol";
import type { ForecastDisplay } from "./types";

function display(index: number): ForecastDisplay {
  return {
    display: index,
    board: "num4x4_matrix5x21",
    nightId: `2026-09-${String(17 + index).padStart(2, "0")}`,
    sunset: "20:30", sunrise: "05:59",
    sun: "******........*******", moon: "?", cloud: ".....................",
    thunderstorm: "?", maximumTemperature: "18.5", minimumTemperature: "9.2"
  };
}

describe("forecast protocol", () => {
  test("serializes six displays in the documented fixed order", () => {
    const body = serializeForecast("krakow", Array.from({ length: 6 }, (_, index) => display(index)));
    expect(body.split("\n").slice(0, 16)).toEqual([
      "protocol=1", "configurationId=krakow", "", "display=0", "board=num4x4_matrix5x21",
      "nightId=2026-09-17", "numeric_0=20:30", "numeric_1=05:59",
      "matrix_0=******........*******", "matrix_1=?", "matrix_2=.....................",
      "matrix_3=?", "numeric_2=18.5", "numeric_3=9.2", "", "display=1"
    ]);
    expect(body).not.toContain("displayCount");
    expect(body).toContain("configurationId=krakow\n\ndisplay=0");
    expect(body).toContain("numeric_3=9.2\n\ndisplay=1");
  });

  test("serializes stable machine-readable errors", () => {
    expect(serializeError("configuration_not_found")).toBe("protocol=1\nerror=configuration_not_found\n");
  });
});