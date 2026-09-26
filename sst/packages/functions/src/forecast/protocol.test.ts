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
    precipitation: "?", maximumTemperature: "18.5", minimumTemperature: "9.2"
  };
}

function displays(): ForecastDisplay[] {
  return Array.from({ length: 6 }, (_, index) => display(index));
}

describe("forecast protocol", () => {
  test("serializes six displays in the documented fixed order", () => {
    const body = serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "2026-09-17T18:00:04+02:00", displays());
    expect(body.split("\n").slice(0, 18)).toEqual([
      "protocol=2", "configurationId=krakow", "time=2026-09-17T23:22:45.678+02:00",
      "lastWeatherFetchTime=2026-09-17T18:00:04+02:00", "", "display=0", "board=num4x4_matrix5x21",
      "nightId=2026-09-17", "numeric_0=20:30", "numeric_1=05:59",
      "matrix_0=333320000000000002333", "matrix_1=?", "matrix_2=000000000000000000000",
      "matrix_3=?", "numeric_2=18.5", "numeric_3=9.2", "", "display=1"
    ]);
    expect(body).not.toContain("displayCount");
    expect(body).toContain("lastWeatherFetchTime=2026-09-17T18:00:04+02:00\n\ndisplay=0");
    expect(body).toContain("numeric_3=9.2\n\ndisplay=1");
  });

  test("accepts every matrix cell of version 2", () => {
    const all = displays();
    all[0].precipitation = "0123*?0123*?0123*?012";
    const body = serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "?", all);
    expect(body).toContain("\nmatrix_3=0123*?0123*?0123*?012\n");
  });

  test.each(["*****................", "4444444444444444444444", "33332000000000000233", "..................... "])("rejects the matrix row %s", (row) => {
    const all = displays();
    all[2].cloud = row;
    expect(() => serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "?", all))
      .toThrow("Invalid forecast matrix");
  });

  test.each(["2026-09-17 23:22:45.678+02:00", "2026-09-17T23:22:45+02:00", "2026-09-17T23:22:45.6+02:00", "2026-09-17T23:22:45.678", "2026-09-17T23:22:45.678Z", "?"])("rejects a malformed time %s", (time) => {
    expect(() => serializeForecast("krakow", time, "?", displays()))
      .toThrow("Invalid forecast time");
  });

  test("serializes an unavailable last weather fetch time", () => {
    const body = serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", "?", displays());
    expect(body).toContain("\nlastWeatherFetchTime=?\n");
  });

  test.each(["2026-09-17T18:00:04.000+02:00", "2026-09-17 18:00:04+02:00", "2026-09-17T18:00:04", "2026-09-17T18:00:04+0200", ""])("rejects a malformed last weather fetch time %s", (value) => {
    expect(() => serializeForecast("krakow", "2026-09-17T23:22:45.678+02:00", value, displays()))
      .toThrow("Invalid last weather fetch time");
  });

  test("serializes stable machine-readable errors", () => {
    expect(serializeError("configuration_not_found")).toBe("protocol=2\nerror=configuration_not_found\n");
  });
});
