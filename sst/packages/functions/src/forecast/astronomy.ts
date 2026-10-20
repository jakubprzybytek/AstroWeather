import SunCalc from "suncalc";
import { encodeMatrix, majorityLevel, minutesLevel, skyLevel, type MatrixCell } from "./matrix";
import { instantAtLocal, localTime, observingSlots, type ObservingSlot } from "./nights";
import type { ForecastDisplay } from "./types";

type Location = { lat: number; lon: number; tz: string };

const DEGREES_PER_RADIAN = 180 / Math.PI;

function validDate(date: Date | undefined): date is Date {
  return date !== undefined && !Number.isNaN(date.getTime());
}

function eventTime(date: Date | undefined, timezone: string): string {
  return validDate(date) ? localTime(date, timezone) : "?";
}

// Minutes of the slot's hour during which the body is above the horizon,
// sampled at the middle of every minute from the slot's start. A rise or set
// partway through the hour thus counts the minutes on each side.
export function minutesAboveHorizon(slot: ObservingSlot, altitude: (instant: Date) => number): number {
  let minutes = 0;
  for (let minute = 0; minute < 60; minute += 1) {
    if (altitude(new Date(slot.start.getTime() + minute * 60_000 + 30_000)) > 0) minutes += 1;
  }
  return minutes;
}

// The sky level held for most of the slot's hour, sampled at the middle of
// every minute like minutesAboveHorizon. `altitude` is in degrees.
export function slotSkyLevel(slot: ObservingSlot, altitude: (instant: Date) => number): MatrixCell {
  const levels = Array.from({ length: 60 }, (_, minute) =>
    skyLevel(altitude(new Date(slot.start.getTime() + minute * 60_000 + 30_000))));
  return majorityLevel(levels);
}

function sample(
  slots: ObservingSlot[],
  level: (slot: ObservingSlot) => MatrixCell
): Array<MatrixCell | null> {
  return slots.map((slot) => {
    try {
      return level(slot);
    } catch {
      return null;
    }
  });
}

// suncalc's times for the night's date, from its local noon, and for the next
// date, whose morning ends the night.
function sunTimes(nightId: string, location: Location) {
  if (!Number.isFinite(location.lat) || !Number.isFinite(location.lon)) {
    throw new Error("Invalid astronomy coordinates");
  }
  const followingDate = new Date(`${nightId}T12:00:00Z`);
  followingDate.setUTCDate(followingDate.getUTCDate() + 1);
  const nextNightDate = followingDate.toISOString().slice(0, 10);
  return {
    times: SunCalc.getTimes(instantAtLocal(nightId, 12, 0, location.tz), location.lat, location.lon),
    nextTimes: SunCalc.getTimes(instantAtLocal(nextNightDate, 12, 0, location.tz), location.lat, location.lon)
  };
}

export type NightWindow = { start: Date; end: Date };

// The darkest kind of night the location gets: astronomical dusk to dawn;
// without an astronomical night (summer at 50 degrees north), nautical dusk to
// dawn; without that, sunset to sunrise; null without a sunset.
export function observingNight(nightId: string, location: Location): NightWindow | null {
  const { times, nextTimes } = sunTimes(nightId, location);
  const pairs: Array<[Date | undefined, Date | undefined]> = [
    [times.night, nextTimes.nightEnd],
    [times.nauticalDusk, nextTimes.nauticalDawn],
    [times.sunset, nextTimes.sunrise]
  ];
  for (const [start, end] of pairs) {
    if (validDate(start) && validDate(end)) return { start, end };
  }
  return null;
}

// Astronomical dusk (the sun 18 degrees below the horizon, suncalc's `night`)
// of the night's date and astronomical dawn (`nightEnd`) of the next date. On
// a night the sun does not get that low, both are `?`. The sun row grades the
// sky by twilight and the moon row by minutes above the horizon.
export function calculateAstronomy(nightId: string, location: Location): Pick<ForecastDisplay, "astronomicalDusk" | "astronomicalDawn" | "sun" | "moon"> {
  const { times, nextTimes } = sunTimes(nightId, location);
  const slots = observingSlots(nightId, location.tz);

  return {
    astronomicalDusk: eventTime(times.night, location.tz),
    astronomicalDawn: eventTime(nextTimes.nightEnd, location.tz),
    sun: encodeMatrix(sample(slots, (slot) => slotSkyLevel(slot, (instant) =>
      SunCalc.getPosition(instant, location.lat, location.lon).altitude * DEGREES_PER_RADIAN))),
    moon: encodeMatrix(sample(slots, (slot) => minutesLevel(minutesAboveHorizon(slot, (instant) =>
      SunCalc.getMoonPosition(instant, location.lat, location.lon).altitude))))
  };
}
