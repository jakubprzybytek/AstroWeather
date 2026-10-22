import { describe, expect, test } from "vitest";
import { observingNight } from "./astronomy";
import { moistureMark, nightTemperatures, temperatureAt, temperatureText } from "./weather-reader";

const HOUR = 3_600_000;
const T0 = Date.UTC(2026, 9, 19, 17);  // a whole hour

function readings(...celsius: Array<number | null>) {
  return celsius.flatMap((value, index) => value === null ? [] : [{ time: T0 + index * HOUR, celsius: value }]);
}

function night(fromHours: number, toHours: number) {
  return { start: new Date(T0 + fromHours * HOUR), end: new Date(T0 + toHours * HOUR) };
}

describe("night temperatures", () => {
  test("interpolates between the hours on either side", () => {
    expect(temperatureAt(readings(10, 8), T0)).toBe(10);
    expect(temperatureAt(readings(10, 8), T0 + HOUR / 4)).toBe(9.5);
    expect(temperatureAt(readings(10, 8), T0 + HOUR)).toBe(8);
  });

  test("uses the one hour within reach when the other is missing", () => {
    expect(temperatureAt(readings(10, null, 6), T0 + HOUR / 2)).toBe(10);
    expect(temperatureAt(readings(null, 8), T0 + HOUR / 2)).toBe(8);
    expect(temperatureAt(readings(10, null, null), T0 + 1.5 * HOUR)).toBeNull();
  });

  test("gives the dusk temperature and the lowest reading of the night", () => {
    expect(nightTemperatures(readings(12, 10, 7, 5, 6, 8), night(0.5, 4.5))).toEqual({
      duskTemperature: "11", nightMinimumTemperature: "5"
    });
  });

  test("counts the interpolated ends, so a night coldest at dusk or dawn is right", () => {
    // Warming all night: the minimum is at dusk.
    expect(nightTemperatures(readings(4, 6, 8, 10), night(0.5, 2.5)).nightMinimumTemperature).toBe("5");
    // Still falling at dawn: the minimum is at dawn, between the last two hours.
    expect(nightTemperatures(readings(10, 8, 6, 4), night(0.5, 2.5)).nightMinimumTemperature).toBe("5");
    // Readings outside the night do not count.
    expect(nightTemperatures(readings(-5, 8, 7, -9), night(1, 2)).nightMinimumTemperature).toBe("7");
  });

  test("gives ? without a night or without readings for it", () => {
    expect(nightTemperatures(readings(10, 8), null)).toEqual({ duskTemperature: "?", nightMinimumTemperature: "?" });
    expect(nightTemperatures([], night(0.5, 4.5))).toEqual({ duskTemperature: "?", nightMinimumTemperature: "?" });
    expect(nightTemperatures(readings(null, null, null, 5), night(0.5, 4.5))).toEqual({ duskTemperature: "?", nightMinimumTemperature: "5" });
  });
});

describe("observing night", () => {
  const krakow = { lat: 50.06, lon: 19.94, tz: "Europe/Warsaw" };
  const minutes = (window: { start: Date; end: Date } | null) => window && Math.round((window.end.getTime() - window.start.getTime()) / 60_000);

  test("is astronomical dusk to dawn when there is an astronomical night", () => {
    const window = observingNight("2026-10-19", krakow);
    expect(window?.start.toISOString().slice(11, 16)).toBe("17:31");
    expect(window?.end.toISOString().slice(11, 16)).toBe("03:22");
  });

  test("falls back to nautical, then to sunset and sunrise", () => {
    // Krakow at midsummer: no astronomical night, a nautical one of about 3 h.
    const nautical = minutes(observingNight("2026-06-21", krakow));
    expect(nautical).toBeGreaterThan(120);
    expect(nautical).toBeLessThan(300);
    // 60 degrees north at midsummer: not even a nautical night; sunset to sunrise.
    const sunsetToSunrise = minutes(observingNight("2026-06-21", { lat: 60, lon: 25, tz: "Europe/Helsinki" }));
    expect(sunsetToSunrise).toBeGreaterThan(300);
    expect(sunsetToSunrise).toBeLessThan(420);
    // 70 degrees north: the midnight sun, no night at all.
    expect(observingNight("2026-06-21", { lat: 70, lon: 25, tz: "Europe/Oslo" })).toBeNull();
  });
});

describe("dew and frost marks", () => {
  // Readings with a dew point: [celsius, dewPointC] per hour from T0.
  function moist(...pairs: Array<[number, number | null]>) {
    return pairs.map(([celsius, dewPointC], index) => ({ time: T0 + index * HOUR, celsius, dewPointC }));
  }

  test("marks dew within 2 degrees of the dew point, frost at or below 1 degree", () => {
    expect(moistureMark(12, 9)).toBe("");
    expect(moistureMark(12, 10)).toBe("d");
    expect(moistureMark(12, 12)).toBe("d");
    expect(moistureMark(2, 0)).toBe("d");
    expect(moistureMark(1, 0)).toBe("F");
    expect(moistureMark(-5, -6)).toBe("F");
    expect(moistureMark(-5, -9)).toBe("");
    expect(moistureMark(12, null)).toBe("");
  });

  test("puts the mark in the first digit and right-aligns the degrees after it", () => {
    expect(temperatureText(12.4, "")).toBe("12");
    expect(temperatureText(12.4, "d")).toBe("d 12");
    expect(temperatureText(-3, "F")).toBe("F -3");
    expect(temperatureText(-14, "F")).toBe("F-14");
    expect(temperatureText(5, "d")).toBe("d  5");
  });

  test("marks the dusk temperature and the minimum by the dew point at their own times", () => {
    // Dry at dusk (12 against 5), damp by the coldest hour (6 against 5).
    expect(nightTemperatures(moist([12, 5], [10, 5], [8, 5], [6, 5], [7, 5]), night(0, 4))).toEqual({
      duskTemperature: "12", nightMinimumTemperature: "d  6"
    });
    // Damp from the start, freezing by the end.
    expect(nightTemperatures(moist([9, 8], [5, 4], [1, 0], [0, -1]), night(0, 3))).toEqual({
      duskTemperature: "d  9", nightMinimumTemperature: "F  0"
    });
  });

  test("interpolates the dew point like the temperature, and leaves no mark without one", () => {
    // At 0.5 h: 11 degrees, dew point halfway from 8 to 10, 9: within 2.
    expect(nightTemperatures(moist([12, 8], [10, 10], [11, 4]), night(0.5, 1.5)).duskTemperature).toBe("d 11");
    expect(nightTemperatures(moist([6, null], [6, null]), night(0, 1))).toEqual({
      duskTemperature: "6", nightMinimumTemperature: "6"
    });
    expect(nightTemperatures(readings(6, 6), night(0, 1)).duskTemperature).toBe("6");
  });
});
