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
    const body = serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "2026-09-17T18:00:04+02:00", Array.from({ length: 6 }, (_, index) => display(index)));
    expect(body.split("\n").slice(0, 18)).toEqual([
      "protocol=1", "configurationId=krakow", "time=2026-09-17T23:22:45.678+02:00",
      "lastWeatherFetchTime=2026-09-17T18:00:04+02:00", "", "display=0", "board=num4x4_matrix5x21",
      "nightId=2026-09-17", "numeric_0=20:30", "numeric_1=05:59",
      "matrix_0=******........*******", "matrix_1=?", "matrix_2=.....................",
      "matrix_3=?", "numeric_2=18.5", "numeric_3=9.2", "", "display=1"
    ]);
    expect(body).not.toContain("displayCount");
    expect(body).toContain("lastWeatherFetchTime=2026-09-17T18:00:04+02:00\n\ndisplay=0");
    expect(body).toContain("numeric_3=9.2\n\ndisplay=1");
  });

  test.each(["2026-09-17 23:22:45.678+02:00", "2026-09-17T23:22:45+02:00", "2026-09-17T23:22:45.6+02:00", "2026-09-17T23:22:45.678", "2026-09-17T23:22:45.678Z", "?"])("rejects a malformed time %s", (time) => {
    expect(() => serializeForecast("krakow", time, "?", Array.from({ length: 6 }, (_, index) => display(index))))
      .toThrow("Invalid forecast time");
  });

  test("serializes an unavailable last weather fetch time", () => {
    const body = serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "?", Array.from({ length: 6 }, (_, index) => display(index)));
    expect(body).toContain("\nlastWeatherFetchTime=?\n");
  });

  test.each(["2026-09-17T18:00:04.000+02:00", "2026-09-17 18:00:04+02:00", "2026-09-17T18:00:04", "2026-09-17T18:00:04+0200", ""])("rejects a malformed last weather fetch time %s", (value) => {
    expect(() => serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", value, Array.from({ length: 6 }, (_, index) => display(index))))
      .toThrow("Invalid last weather fetch time");
  });

  test("serializes stable machine-readable errors", () => {
    expect(serializeError("configuration_not_found")).toBe("protocol=1\nerror=configuration_not_found\n");
  });
});