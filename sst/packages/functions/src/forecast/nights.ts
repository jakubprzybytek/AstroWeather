const DATE_FORMATTER_CACHE = new Map<string, Intl.DateTimeFormat>();

function formatter(timezone: string): Intl.DateTimeFormat {
  let result = DATE_FORMATTER_CACHE.get(timezone);
  if (!result) {
    result = new Intl.DateTimeFormat("en-CA", {
      timeZone: timezone,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      hourCycle: "h23"
    });
    DATE_FORMATTER_CACHE.set(timezone, result);
  }
  return result;
}

function parts(date: Date, timezone: string): Record<string, string> {
  return Object.fromEntries(formatter(timezone).formatToParts(date)
    .filter((part) => part.type !== "literal")
    .map((part) => [part.type, part.value]));
}

export function localDateTime(date: Date, timezone: string): string {
  const value = parts(date, timezone);
  return `${value.year}-${value.month}-${value.day}T${value.hour}:${value.minute}:${value.second}`;
}

// Local date-time with milliseconds, for the response `time` record. Every IANA
// offset in use is a whole number of seconds, so the milliseconds are the UTC ones.
export function localDateTimeMillis(date: Date, timezone: string): string {
  return `${localDateTime(date, timezone)}.${String(date.getUTCMilliseconds()).padStart(3, "0")}`;
}

// The zone's UTC offset at `date` as `+HH:MM`, for the response time records.
// Every IANA offset in use today is a whole number of minutes.
export function utcOffset(date: Date, timezone: string): string {
  const minutes = Math.round(offsetMilliseconds(date, timezone) / 60_000);
  const magnitude = Math.abs(minutes);
  const hours = String(Math.floor(magnitude / 60)).padStart(2, "0");
  return `${minutes < 0 ? "-" : "+"}${hours}:${String(magnitude % 60).padStart(2, "0")}`;
}

export function localDate(date: Date, timezone: string): string {
  return localDateTime(date, timezone).slice(0, 10);
}

function addDays(date: string, amount: number): string {
  const value = new Date(`${date}T12:00:00Z`);
  value.setUTCDate(value.getUTCDate() + amount);
  return value.toISOString().slice(0, 10);
}

function offsetMilliseconds(date: Date, timezone: string): number {
  const value = parts(date, timezone);
  const asUtc = Date.UTC(
    Number(value.year), Number(value.month) - 1, Number(value.day),
    Number(value.hour), Number(value.minute), Number(value.second)
  );
  return asUtc - date.getTime();
}

export function instantAtLocal(
  date: string,
  hour: number,
  minute: number,
  timezone: string
): Date {
  const wallClock = Date.UTC(
    Number(date.slice(0, 4)), Number(date.slice(5, 7)) - 1,
    Number(date.slice(8, 10)), hour, minute
  );
  const initial = new Date(wallClock);
  const adjusted = new Date(wallClock - offsetMilliseconds(initial, timezone));
  return new Date(wallClock - offsetMilliseconds(adjusted, timezone));
}

export function localTime(date: Date, timezone: string): string {
  const value = parts(date, timezone);
  return `${value.hour}:${value.minute}`;
}

export type ObservingSlot = {
  index: number;
  date: string;
  hour: number;
  midpoint: Date;
};

export function observingSlots(nightId: string, timezone: string): ObservingSlot[] {
  return Array.from({ length: 21 }, (_, index) => {
    const hourOffset = index;
    const hour = 14 + hourOffset;
    const date = hour >= 24 ? addDays(nightId, 1) : nightId;
    return {
      index,
      date,
      hour: hour % 24,
      midpoint: instantAtLocal(date, hour % 24, 30, timezone)
    };
  });
}

export function nightIdFor(now: Date, timezone: string): string {
  const current = parts(now, timezone);
  const date = `${current.year}-${current.month}-${current.day}`;
  return Number(current.hour) < 12 ? addDays(date, -1) : date;
}

export function nextNightIds(now: Date, timezone: string, count = 6): string[] {
  const first = nightIdFor(now, timezone);
  return Array.from({ length: count }, (_, index) => addDays(first, index));
}