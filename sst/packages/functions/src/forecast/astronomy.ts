import SunCalc from "suncalc";
import { instantAtLocal, localTime, observingSlots, type ObservingSlot } from "./nights";
import type { ForecastDisplay } from "./types";

type Location = { lat: number; lon: number; tz: string };

function matrix(states: Array<boolean | null>): string {
  const available = states.some((state) => state !== null);
  return available ? states.map((state) => state === null ? "?" : state ? "*" : ".").join("") : "?";
}

function eventTime(date: Date | undefined, timezone: string): string {
  return date && !Number.isNaN(date.getTime()) ? localTime(date, timezone) : "?";
}

function sample(
  slots: ObservingSlot[],
  callback: (slot: ObservingSlot) => boolean
): Array<boolean | null> {
  return slots.map((slot) => {
    try {
      return callback(slot);
    } catch {
      return null;
    }
  });
}

export function calculateAstronomy(nightId: string, location: Location): Pick<ForecastDisplay, "sunset" | "sunrise" | "sun" | "moon"> {
  if (!Number.isFinite(location.lat) || !Number.isFinite(location.lon)) {
    throw new Error("Invalid astronomy coordinates");
  }
  const sunsetDate = instantAtLocal(nightId, 12, 0, location.tz);
  const followingDate = new Date(`${nightId}T12:00:00Z`);
  followingDate.setUTCDate(followingDate.getUTCDate() + 1);
  const nextNightDate = followingDate.toISOString().slice(0, 10);
  const times = SunCalc.getTimes(sunsetDate, location.lat, location.lon);
  const nextTimes = SunCalc.getTimes(instantAtLocal(nextNightDate, 12, 0, location.tz), location.lat, location.lon);
  const slots = observingSlots(nightId, location.tz);

  return {
    sunset: eventTime(times.sunset, location.tz),
    sunrise: eventTime(nextTimes.sunrise, location.tz),
    sun: matrix(sample(slots, (slot) => SunCalc.getPosition(slot.midpoint, location.lat, location.lon).altitude > 0)),
    moon: matrix(sample(slots, (slot) => SunCalc.getMoonPosition(slot.midpoint, location.lat, location.lon).altitude > 0))
  };
}