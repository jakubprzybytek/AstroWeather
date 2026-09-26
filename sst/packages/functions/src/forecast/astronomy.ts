import SunCalc from "suncalc";
import { encodeMatrix, minutesLevel, type MatrixCell } from "./matrix";
import { instantAtLocal, localTime, observingSlots, type ObservingSlot } from "./nights";
import type { ForecastDisplay } from "./types";

type Location = { lat: number; lon: number; tz: string };

function eventTime(date: Date | undefined, timezone: string): string {
  return date && !Number.isNaN(date.getTime()) ? localTime(date, timezone) : "?";
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

function sample(
  slots: ObservingSlot[],
  altitude: (instant: Date) => number
): Array<MatrixCell | null> {
  return slots.map((slot) => {
    try {
      return minutesLevel(minutesAboveHorizon(slot, altitude));
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
    sun: encodeMatrix(sample(slots, (instant) => SunCalc.getPosition(instant, location.lat, location.lon).altitude)),
    moon: encodeMatrix(sample(slots, (instant) => SunCalc.getMoonPosition(instant, location.lat, location.lon).altitude))
  };
}
