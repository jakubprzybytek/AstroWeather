import { encodeMatrix, minutesLevel, type MatrixCell } from "./matrix";
import { nextNightIds } from "./nights";
import type { AssembledForecast, ForecastDisplay } from "./types";

// The `test` configuration: not listed by GET /configurations, but served by
// GET /astro/test with made-up data that shows every display variant, for
// trying the boards out ('astro test' on the device). Only `time` is real, as
// the device sets its clock from it.
export const DEMO_CONFIGURATION_ID = "test";
export const DEMO_TIMEZONE = "Europe/Warsaw";

// One night per board, each a theme: the rows must be 21 cells. Together they
// use every cell character in every weather and aurora row, steady and
// changing, and the temperatures cover one and two digits, both signs, zero
// and unavailable.
type DemoNight = {
  theme: string;
  cloud: string;
  precipitation: string;
  aurora: string;
  maximumTemperature: string;
  minimumTemperature: string;
  moonUnavailable?: boolean;
};

export const DEMO_NIGHTS: DemoNight[] = [
  {
    theme: "clear sky, aurora storm, blinking where it is now",
    cloud: "000000000000000000000",
    precipitation: "000000000000000000000",
    aurora: "000112233bcc332211000",
    maximumTemperature: "4", minimumTemperature: "-3"
  },
  {
    theme: "overcast, steady then heavy rain",
    cloud: "333333333333333333333",
    precipitation: "112233332222333322111",
    aurora: "000000000000000000000",
    maximumTemperature: "17", minimumTemperature: "11"
  },
  {
    theme: "thunderstorms, then clearing; aurora possible all night",
    cloud: "333333322211100000000",
    precipitation: "01223***c**2110000000",
    aurora: "111111111111111111111",
    maximumTemperature: "28", minimumTemperature: "19"
  },
  {
    theme: "clouding over after a clear frosty evening; a short aurora burst",
    cloud: "000000011112222233333",
    precipitation: "000000000011122233333",
    aurora: "0000abcba000000123000",
    maximumTemperature: "-2", minimumTemperature: "-14"
  },
  {
    theme: "changing sky: every level and blink in turn",
    cloud: "012301230123012301230",
    precipitation: "0a0b0c0*0a0b0c0*01230",
    aurora: "0123012301230123abc*0",
    maximumTemperature: "0", minimumTemperature: "-8"
  },
  {
    theme: "unavailable data: gaps, whole rows and temperatures missing",
    cloud: "3322????????????11000",
    precipitation: "?",
    aurora: "?",
    maximumTemperature: "?", minimumTemperature: "?",
    moonUnavailable: true
  }
];

const DAY_MINUTES = 24 * 60;
// Slot i covers local 14:00 + i hours: minutes 120 + 60i from local noon.
const FIRST_SLOT_FROM_NOON = 120;

function randomInt(random: () => number, low: number, high: number): number {
  return low + Math.floor(random() * (high - low + 1));
}

function clockTime(minutesOfDay: number): string {
  const minutes = ((Math.round(minutesOfDay) % DAY_MINUTES) + DAY_MINUTES) % DAY_MINUTES;
  return `${String(Math.floor(minutes / 60)).padStart(2, "0")}:${String(minutes % 60).padStart(2, "0")}`;
}

// Minutes of each slot inside any of `intervals` (minutes from local noon).
function slotLevels(intervals: Array<[number, number]>): MatrixCell[] {
  return Array.from({ length: 21 }, (_, index) => {
    const start = FIRST_SLOT_FROM_NOON + index * 60;
    const up = intervals.reduce((sum, [from, to]) =>
      sum + Math.max(0, Math.min(to, start + 60) - Math.max(from, start)), 0);
    return minutesLevel(Math.min(60, up));
  });
}

// Six nights of demo data. Sunset and sunrise start at random times and move by
// a few minutes a night, as in autumn; the moon rises about 50 minutes later
// each night. The sun and moon rows follow those times.
export function demoForecast(now: Date, random: () => number = Math.random): AssembledForecast {
  const nightIds = nextNightIds(now, DEMO_TIMEZONE);
  let sunset = randomInt(random, 17 * 60, 20 * 60 + 30);     // minutes of the day
  let sunrise = randomInt(random, 5 * 60 + 30, 7 * 60 + 30);
  let moonrise = randomInt(random, 13 * 60, 20 * 60);
  const moonHours = randomInt(random, 10, 13) * 60;

  const displays: ForecastDisplay[] = nightIds.map((nightId, display) => {
    const night = DEMO_NIGHTS[display];
    if (display > 0) {
      sunset -= randomInt(random, 1, 3);
      sunrise += randomInt(random, 1, 3);
      moonrise += randomInt(random, 40, 60);
    }
    // From local noon: the sun is up until sunset and again from sunrise the
    // next morning; the moon from its rise for `moonHours`, and the previous
    // day's moon until it sets.
    const sunsetFromNoon = sunset - 12 * 60;
    const sunriseFromNoon = sunrise + 12 * 60;
    const moonriseFromNoon = moonrise - 12 * 60;
    const sun = slotLevels([[0, sunsetFromNoon], [sunriseFromNoon, DAY_MINUTES]]);
    const moon = slotLevels([
      [moonriseFromNoon, moonriseFromNoon + moonHours],
      [moonriseFromNoon - DAY_MINUTES - 50, moonriseFromNoon - DAY_MINUTES - 50 + moonHours]
    ]);

    return {
      display,
      board: "num4x4_matrix5x21",
      nightId,
      sunset: clockTime(sunset),
      sunrise: clockTime(sunrise),
      sun: encodeMatrix(sun),
      moon: night.moonUnavailable ? "?" : encodeMatrix(moon),
      cloud: night.cloud,
      precipitation: night.precipitation,
      aurora: night.aurora,
      maximumTemperature: night.maximumTemperature,
      minimumTemperature: night.minimumTemperature
    };
  });

  return { displays, lastAuroraFetch: {}, refreshIntervalMinutes: 360 };
}
