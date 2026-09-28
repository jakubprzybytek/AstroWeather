import { addDays, nightIdFor } from "../forecast/nights";

// A source value with the UTC interval it applies to. `start` is inclusive,
// `end` exclusive; both are ISO timestamps. Sources keep their own granularity
// (an hour, three hours, a UTC day), and the interval is never clipped.
export type Span<T> = T & {
  start: string;
  end: string;
};

export type SourceNight<T> = {
  nightId: string;
  spans: Span<T>[];
};

// Buckets spans into local-noon-to-noon nights. A span belongs to every night
// it overlaps, so a UTC-day value shows up in two consecutive nights and a
// three-hour bin that crosses local noon in both nights around it.
export function groupSpansByNight<T>(spans: Span<T>[], timezone: string): SourceNight<T>[] {
  const nights = new Map<string, Span<T>[]>();

  for (const span of spans) {
    const first = nightIdFor(new Date(span.start), timezone);
    const last = nightIdFor(new Date(Date.parse(span.end) - 1), timezone);
    for (let nightId = first; nightId <= last; nightId = addDays(nightId, 1)) {
      const bucket = nights.get(nightId) ?? [];
      bucket.push(span);
      nights.set(nightId, bucket);
    }
  }

  return [...nights.entries()]
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([nightId, nightSpans]) => ({
      nightId,
      spans: [...nightSpans].sort((left, right) => left.start.localeCompare(right.start))
    }));
}

export function utcSpan(start: Date, durationMs: number): { start: string; end: string } {
  return {
    start: start.toISOString(),
    end: new Date(start.getTime() + durationMs).toISOString()
  };
}
