import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, test, vi } from "vitest";
import { fetchClearOutsideHtml, parseClearOutside } from "./clearoutside.js";

const fixture = readFileSync(
  fileURLToPath(new URL("./__fixtures__/clearoutside.html", import.meta.url)),
  "utf8"
);

// A page in Clear Outside's layout: each day's columns start at the hour the
// page was generated, so a day's hours can wrap past midnight or be empty.
// The temperature is the hour, so tests can tell the hours apart.
function page(generated: string, days: Array<{ label: string; hours: number[] }>): string {
  const row = (label: string, values: string[]) =>
    `<div class="fc_detail_row"><span class="fc_detail_label">${label}</span><div class="fc_hours"><ul>${values.map((v) => `<li>${v}</li>`).join("")}</ul></div></div>`;
  return `<html><body><h2>Generated: ${generated}. Forecast: x. Timezone: UTC+1.00</h2>${days.map((day, index) => `
    <div class="fc_day" id="day_${index}"><div class="fc_day_date"><span>Day</span> ${day.label}</div>
    <div class="fc_hours fc_hour_ratings"><ul>${day.hours.map((h) => `<li>${String(h).padStart(2, "0")} <span>OK</span></li>`).join("")}</ul></div>
    ${row("Temperature (&deg;C)", day.hours.map(String))}
    ${row("Total Clouds (% Sky Obscured)", day.hours.map(() => "50"))}
    ${row("Precipitation Probability (%)", day.hours.map(() => "0"))}
    ${row("Precipitation Type", day.hours.map(() => "None"))}</div>`).join("")}</body></html>`;
}

const fromHour = (start: number, count = 24) => Array.from({ length: count }, (_, i) => (start + i) % 24);
const summary = (nights: ReturnType<typeof parseClearOutside>) =>
  nights.map((night) => `${night.nightId}: ${night.hours[0].timestampUtc} .. ${night.hours.at(-1)!.timestampUtc} (${night.hours.length})`);

const originalFetch = globalThis.fetch;

afterEach(() => {
  globalThis.fetch = originalFetch;
  vi.useRealTimers();
});

describe("parseClearOutside", () => {
  test("parses a noon-to-noon night into hourly weather samples", () => {
    const [night] = parseClearOutside(fixture);

    expect(night.nightId).toBe("2026-09-11");
    expect(night.hours).toHaveLength(24);
    expect(night.hours[0]).toEqual({
      hour: 12,
      timestampUtc: "2026-09-11T09:00:00.000Z",
      temperatureC: 36,
      dewPointC: 18,
      cloudCoverTotalPct: 0,
      precipitationProbabilityPct: 0,
      thunderstormRisk: false
    });
    expect(night.hours[12].timestampUtc).toBe("2026-09-11T21:00:00.000Z");
    expect(night.hours[6].thunderstormRisk).toBe(true);
    expect(night.hours[6].precipitationProbabilityPct).toBe(40);
  });

  test("parses the dew point, negative values included", () => {
    const [night] = parseClearOutside(fixture);

    expect(night.hours[19].dewPointC).toBe(27);
    expect(night.hours[23].dewPointC).toBe(-3);
  });

  test("keeps parsing with null dew points when the row is missing or misaligned", () => {
    const missing = parseClearOutside(fixture.replace("Dew Point (&deg;C)", "Dew"))[0];
    expect(missing.hours.every((hour) => hour.dewPointC === null)).toBe(true);
    expect(missing.hours[0].temperatureC).toBe(36);

    const misaligned = parseClearOutside(fixture.replace("<li>25</li><li>-3</li>", "<li>25</li>"))[0];
    expect(misaligned.hours.every((hour) => hour.dewPointC === null)).toBe(true);
  });

  test("dates the hours of a page generated at 18:00 and regroups them into noon-to-noon nights", () => {
    const nights = parseClearOutside(page("16/11/26 18:05:00", [
      { label: "16", hours: fromHour(18) },
      { label: "17", hours: fromHour(18) }
    ]));

    expect(summary(nights)).toEqual([
      "2026-11-16: 2026-11-16T17:00:00.000Z .. 2026-11-17T10:00:00.000Z (18)",
      "2026-11-17: 2026-11-17T11:00:00.000Z .. 2026-11-18T10:00:00.000Z (24)",
      "2026-11-18: 2026-11-18T11:00:00.000Z .. 2026-11-18T16:00:00.000Z (6)"
    ]);
    // 12:00 on the 17th, the 19th column of the first day, not the 16th.
    expect(nights[1].hours[0]).toMatchObject({ hour: 12, temperatureC: 12 });
  });

  test("dates the hours of a page generated at 06:00", () => {
    const nights = parseClearOutside(page("17/11/26 06:03:00", [{ label: "17", hours: fromHour(6) }]));

    expect(summary(nights)).toEqual([
      "2026-11-16: 2026-11-17T05:00:00.000Z .. 2026-11-17T10:00:00.000Z (6)",
      "2026-11-17: 2026-11-17T11:00:00.000Z .. 2026-11-18T04:00:00.000Z (18)"
    ]);
  });

  test("skips a day that is already over at midnight instead of failing", () => {
    const nights = parseClearOutside(page("17/11/26 00:04:00", [
      { label: "16", hours: [] },
      { label: "17", hours: fromHour(0) }
    ]));

    expect(summary(nights)).toEqual([
      "2026-11-16: 2026-11-16T23:00:00.000Z .. 2026-11-17T10:00:00.000Z (12)",
      "2026-11-17: 2026-11-17T11:00:00.000Z .. 2026-11-17T22:00:00.000Z (12)"
    ]);
  });

  test("dates day labels across the end of a month", () => {
    const nights = parseClearOutside(page("30/11/26 12:00:00", [
      { label: "30", hours: fromHour(12) },
      { label: "1", hours: fromHour(12) }
    ]));

    expect(nights.map((night) => night.nightId)).toEqual(["2026-11-30", "2026-12-01"]);
  });

  test("fails closed when the hours don't start at the generation time", () => {
    expect(() => parseClearOutside(page("17/11/26 00:04:00", [{ label: "16", hours: fromHour(0) }])))
      .toThrow("not near its generation time");
  });

  test("fails closed when a page has no hours at all", () => {
    expect(() => parseClearOutside(page("17/11/26 00:04:00", [{ label: "16", hours: [] }])))
      .toThrow("Clear Outside page contains no forecast hours");
  });

  test("fails closed when a required weather row is missing", () => {
    const malformed = fixture.replace("Total Clouds (% Sky Obscured)", "Clouds");

    expect(() => parseClearOutside(malformed)).toThrow(
      "Clear Outside page is missing the Total Clouds row"
    );
  });

  test("retries one transient server failure", async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(new Response("temporary failure", { status: 503 }))
      .mockResolvedValueOnce(new Response("forecast", { status: 200 }));
    globalThis.fetch = fetchMock;

    await expect(fetchClearOutsideHtml(50, 20)).resolves.toBe("forecast");
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  test("does not retry a non-retryable client failure", async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response("bad request", { status: 400 }));
    globalThis.fetch = fetchMock;

    await expect(fetchClearOutsideHtml(50, 20)).rejects.toThrow("HTTP 400");
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  test("does not immediately retry a rate-limited response", async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response("too many requests", {
      status: 429,
      headers: { "retry-after": "3600" }
    }));
    globalThis.fetch = fetchMock;

    await expect(fetchClearOutsideHtml(27.9, 34.3)).rejects.toThrow(
      "HTTP 429; retry after 3600"
    );
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  test("retries after a request timeout", async () => {
    vi.useFakeTimers();
    const fetchMock = vi.fn((_: string, init?: RequestInit) => {
      if (fetchMock.mock.calls.length === 1) {
        return new Promise<Response>((_, reject) => {
          init?.signal?.addEventListener("abort", () => reject(new DOMException("aborted", "AbortError")));
        });
      }

      return Promise.resolve(new Response("forecast", { status: 200 }));
    });
    globalThis.fetch = fetchMock;

    const result = fetchClearOutsideHtml(50, 20);
    await vi.advanceTimersByTimeAsync(15_000);
    await vi.advanceTimersByTimeAsync(400);

    await expect(result).resolves.toBe("forecast");
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });
});
