import { describe, expect, test } from "vitest";
import { createHandler } from "../astro";
import { handler as configurationsHandler } from "../configurations-handler";
import { DEMO_NIGHTS, demoForecast } from "./demo";
import { NO_AURORA_FETCH, serializeForecast } from "./protocol";

const NOW = new Date("2026-10-01T18:00:00Z");

function minutes(time: string): number {
  return Number(time.slice(0, 2)) * 60 + Number(time.slice(3, 5));
}

describe("demo forecast", () => {
  test("gives six consecutive nights that serialize as a valid payload", () => {
    const { displays, refreshIntervalMinutes } = demoForecast(NOW);

    expect(displays.map((display) => display.nightId)).toEqual([
      "2026-10-01", "2026-10-02", "2026-10-03", "2026-10-04", "2026-10-05", "2026-10-06"
    ]);
    expect(refreshIntervalMinutes).toBe(360);
    expect(() => serializeForecast("test", "2026-10-01T20:00:00.000+02:00", "?", NO_AURORA_FETCH, 360, displays)).not.toThrow();
  });

  test("moves sunset and sunrise by a few minutes a night, and the sun row follows them", () => {
    const { displays } = demoForecast(NOW);

    for (let index = 1; index < displays.length; index += 1) {
      const sunsetStep = minutes(displays[index - 1].sunset) - minutes(displays[index].sunset);
      const sunriseStep = minutes(displays[index].sunrise) - minutes(displays[index - 1].sunrise);
      expect(sunsetStep).toBeGreaterThanOrEqual(1);
      expect(sunsetStep).toBeLessThanOrEqual(3);
      expect(sunriseStep).toBeGreaterThanOrEqual(1);
      expect(sunriseStep).toBeLessThanOrEqual(3);
    }
    // Sunset 18:30, sunrise 06:50: up to 18:00 fully, half of 18:00, then down
    // until 06:00, 10 minutes of it, full from 07:00.
    const fixed = demoForecast(NOW, sequence([(18 * 60 + 30 - 17 * 60) / (3 * 60 + 31), (6 * 60 + 50 - 5 * 60 - 30) / 121, 0, 0]));
    expect(fixed.displays[0].sunset).toBe("18:30");
    expect(fixed.displays[0].sunrise).toBe("06:50");
    expect(fixed.displays[0].sun).toBe("333320000000000013333");
  });

  test("uses every cell character in the weather and aurora rows, and every kind of temperature", () => {
    const cells = (row: "cloud" | "precipitation" | "aurora") => new Set(DEMO_NIGHTS.flatMap((night) => [...night[row]]));
    for (const cell of "0123?") expect(cells("cloud")).toContain(cell);
    for (const cell of "0123abc*?") {
      expect(cells("precipitation")).toContain(cell);
      expect(cells("aurora")).toContain(cell);
    }
    for (const night of DEMO_NIGHTS) {
      for (const row of [night.cloud, night.precipitation, night.aurora]) {
        expect(row === "?" || row.length === 21).toBe(true);
      }
    }
    const temperatures = DEMO_NIGHTS.flatMap((night) => [night.maximumTemperature, night.minimumTemperature]);
    expect(temperatures).toEqual(expect.arrayContaining(["4", "-3", "17", "-14", "0", "?"]));
  });

  test("is served as GET /astro/test with the real time, and is not listed as a configuration", async () => {
    const handler = createHandler({ now: () => NOW, readWeather: async () => new Map(), readAurora: async () => new Map() });

    const response = await handler({ pathParameters: { configurationId: "test" } });

    expect(response.statusCode).toBe(200);
    expect(response.body.split("\n").slice(0, 9)).toEqual([
      "protocol=3", "configurationId=test", "time=2026-10-01T20:00:00.000+02:00",
      "lastWeatherFetchTime=?", "lastGfzFetchTime=?", "lastNoaaKpFetchTime=?",
      "lastNoaaOutlookFetchTime=?", "lastOvationFetchTime=?", "refreshIntervalMinutes=360"
    ]);
    const listed = JSON.parse((await configurationsHandler()).body) as Array<{ id: string }>;
    expect(listed.map((configuration) => configuration.id)).not.toContain("test");
  });
});

function sequence(values: number[]): () => number {
  let index = 0;
  return () => values[index++ % values.length];
}
