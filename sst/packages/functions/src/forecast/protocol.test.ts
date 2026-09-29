import { describe, expect, test } from "vitest";
import { serializeError, serializeForecast } from "./protocol";
import type { ForecastDisplay } from "./types";

function display(index: number): ForecastDisplay {
  return {
    display: index,
    board: "num4x4_matrix5x21",
    nightId: `2026-09-${String(17 + index).padStart(2, "0")}`,
    sunset: "20:30", sunrise: "05:59",
    sun: "333320000000000002333", moon: "?", cloud: "000000000000000000000",
    precipitation: "?", aurora: "000000000000000000000", maximumTemperature: "18", minimumTemperature: "9"
  };
}

function displays(): ForecastDisplay[] {
  return Array.from({ length: 6 }, (_, index) => display(index));
}

describe("forecast protocol", () => {
  test("serializes six displays in the documented fixed order", () => {
    const body = serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "2026-09-17T18:00:04+02:00", 360, displays());
    expect(body.split("\n").slice(0, 20)).toEqual([
      "protocol=3", "configurationId=krakow", "time=2026-09-17T23:22:45.678+02:00",
      "lastWeatherFetchTime=2026-09-17T18:00:04+02:00", "refreshIntervalMinutes=360", "", "display=0",
      "board=num4x4_matrix5x21", "nightId=2026-09-17", "numeric_0=20:30", "numeric_1=05:59",
      "matrix_0=333320000000000002333", "matrix_1=?", "matrix_2=000000000000000000000",
      "matrix_3=?", "matrix_4=000000000000000000000", "numeric_2=18", "numeric_3=9", "", "display=1"
    ]);
    expect(body).not.toContain("displayCount");
    expect(body).toContain("refreshIntervalMinutes=360\n\ndisplay=0");
    expect(body).toContain("numeric_3=9\n\ndisplay=1");
  });

  test("accepts every matrix cell of version 3", () => {
    const all = displays();
    all[0].aurora = "0123abc*?0123abc*?012";
    const body = serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "?", 60, all);
    expect(body).toContain("\nmatrix_4=0123abc*?0123abc*?012\n");
    expect(body).toContain("\nrefreshIntervalMinutes=60\n");
  });

  test("rejects a refresh interval other than hourly or six-hourly", () => {
    expect(() => serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "?", 30, displays()))
      .toThrow("Invalid refresh interval");
  });

  test.each(["*****................", "4444444444444444444444", "dddddddddddddddddddd0", "33332000000000000233", "..................... "])("rejects the matrix row %s", (row) => {
    const all = displays();
    all[2].cloud = row;
    expect(() => serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "?", 360, all))
      .toThrow("Invalid forecast matrix");
  });

  test.each(["2026-09-17 23:22:45.678+02:00", "2026-09-17T23:22:45+02:00", "2026-09-17T23:22:45.6+02:00", "2026-09-17T23:22:45.678", "2026-09-17T23:22:45.678Z", "?"])("rejects a malformed time %s", (time) => {
    expect(() => serializeForecast("krakow", time, "?", 360, displays()))
      .toThrow("Invalid forecast time");
  });

  test("serializes an unavailable last weather fetch time", () => {
    const body = serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "?", 360, displays());
    expect(body).toContain("\nlastWeatherFetchTime=?\n");
  });

  test.each(["2026-09-17T18:00:04.000+02:00", "2026-09-17 18:00:04+02:00", "2026-09-17T18:00:04", "2026-09-17T18:00:04+0200", ""])("rejects a malformed last weather fetch time %s", (value) => {
    expect(() => serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", value, 360, displays()))
      .toThrow("Invalid last weather fetch time");
  });

  test("serializes stable machine-readable errors", () => {
    expect(serializeError("configuration_not_found")).toBe("protocol=3\nerror=configuration_not_found\n");
  });
});
