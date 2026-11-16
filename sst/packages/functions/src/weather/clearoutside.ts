import { parse } from "node-html-parser";

export type ClearOutsideHour = {
  hour: number;
  timestampUtc: string;
  temperatureC: number | null;
  // Optional: absent from records stored before it was collected, and null
  // when the page has no Dew Point row.
  dewPointC?: number | null;
  cloudCoverTotalPct: number | null;
  precipitationProbabilityPct: number | null;
  thunderstormRisk: boolean | null;
};

export type ClearOutsideNight = {
  nightId: string;
  hours: ClearOutsideHour[];
};

type ForecastHeader = {
  startDate: string;
  generatedHour: number;
  utcOffsetMinutes: number;
};

const REQUIRED_ROWS = [
  "Temperature",
  "Total Clouds",
  "Precipitation Probability",
  "Precipitation Type"
] as const;

// Collected for a later dew-risk indicator (docs/api.md, Open Items). Not
// required: a page without it, or with it misaligned, still parses, with null
// dew points.
const DEW_POINT_ROW = "Dew Point";

function parseNumber(value: string): number | null {
  const match = value.trim().match(/-?\d+(?:\.\d+)?/);
  return match ? Number(match[0]) : null;
}

function parseDate(value: string): Date {
  const match = value.match(/Generated:\s+(\d{2})\/(\d{2})\/(\d{2})/i);
  if (!match) {
    throw new Error("Clear Outside page is missing a generated date");
  }

  const year = 2000 + Number(match[3]);
  const month = Number(match[2]) - 1;
  const day = Number(match[1]);
  return new Date(Date.UTC(year, month, day, 12));
}

function parseUtcOffset(value: string): number {
  const match = value.match(/Timezone:\s+UTC\s*([+-])(\d{1,2})(?::(\d{2}))?/i);
  if (!match) {
    throw new Error("Clear Outside page is missing a UTC timezone offset");
  }

  const minutes = Number(match[2]) * 60 + Number(match[3] ?? 0);
  return match[1] === "+" ? minutes : -minutes;
}

function parseForecastHeader(text: string): ForecastHeader {
  const generatedDate = parseDate(text);
  const startDate = generatedDate.toISOString().slice(0, 10);
  const time = text.match(/Generated:\s+\d{2}\/\d{2}\/\d{2}\s+(\d{2}):\d{2}/i);
  return {
    startDate,
    generatedHour: time ? Number(time[1]) : 12,
    utcOffsetMinutes: parseUtcOffset(text)
  };
}

function addDays(date: string, days: number): string {
  const value = new Date(`${date}T12:00:00.000Z`);
  value.setUTCDate(value.getUTCDate() + days);
  return value.toISOString().slice(0, 10);
}

// A day's label gives only the day of the month ("Monday 16"): the date with
// that day closest after the day before the page was generated.
function dayLabelDate(day: ReturnType<typeof parse>, generatedDate: string): string {
  const label = (day.querySelector(".fc_day_date")?.textContent ?? "").replace(/\s+/g, " ").trim();
  const match = label.match(/(\d{1,2})\s*$/);
  if (!match) {
    throw new Error(`Clear Outside day label has no date: ${label}`);
  }
  for (let offset = -1; offset <= 8; offset += 1) {
    const date = addDays(generatedDate, offset);
    if (Number(date.slice(8, 10)) === Number(match[1])) return date;
  }
  throw new Error(`Clear Outside day label is outside the forecast: ${label}`);
}

function findRow(day: ReturnType<typeof parse>, label: string) {
  return day.querySelectorAll(".fc_detail_row").find((row) => {
    const rowLabel = row.querySelector(".fc_detail_label")?.textContent ?? "";
    return rowLabel.replace(/\s+/g, " ").trim().startsWith(label);
  });
}

function rowValues(row: ReturnType<typeof parse>) {
  return row.querySelectorAll(".fc_hours li");
}

function parseHour(value: string): number {
  const hour = Number(value.trim());
  if (!Number.isInteger(hour) || hour < 0 || hour > 23) {
    throw new Error(`Invalid Clear Outside hour: ${value}`);
  }
  return hour;
}

function toUtcTimestamp(localDateValue: string, hour: number, utcOffsetMinutes: number): string {
  const localDate = new Date(`${localDateValue}T00:00:00.000Z`);
  localDate.setUTCHours(hour, 0, 0, 0);
  localDate.setUTCMinutes(localDate.getUTCMinutes() - utcOffsetMinutes);
  return localDate.toISOString();
}

// The page's days start at the hour it was generated (12-11 at noon, 00-23 at
// midnight, 18-17 at 18:00), and a day can be empty. So each hour is dated from
// its day's label, moving to the next date when the hours wrap past midnight,
// and the hours are regrouped into noon-to-noon nights (nightIdFor). The first
// and last nights can be partial.
export function parseClearOutside(html: string): ClearOutsideNight[] {
  const document = parse(html);
  const header = parseForecastHeader(document.querySelector("body")?.textContent ?? "");
  const days = document.querySelectorAll(".fc_day");

  if (days.length === 0) {
    throw new Error("Clear Outside page contains no forecast days");
  }

  const nights = new Map<string, ClearOutsideHour[]>();

  days.forEach((day, dayIndex) => {
    const hours = day.querySelectorAll(".fc_hours.fc_hour_ratings li");
    if (hours.length > 24) {
      throw new Error(`Clear Outside day ${dayIndex} has ${hours.length} hour labels`);
    }
    if (hours.length === 0) return; // a day already over when the page was generated

    const rows = REQUIRED_ROWS.map((label) => {
      const row = findRow(day, label);
      if (!row) {
        throw new Error(`Clear Outside page is missing the ${label} row`);
      }
      return rowValues(row);
    });
    if (rows.some((row) => row.length !== hours.length)) {
      throw new Error(`Clear Outside day ${dayIndex} has misaligned hourly rows`);
    }
    const dewPointRow = findRow(day, DEW_POINT_ROW);
    const dewPoints = dewPointRow ? rowValues(dewPointRow) : [];
    const hasDewPoints = dewPoints.length === hours.length;

    let date = dayLabelDate(day, header.startDate);
    let previousHour = -1;
    hours.forEach((hourElement, hourIndex) => {
      const hour = parseHour(hourElement.textContent.replace(/\s+/g, " ").trim().split(" ")[0]);
      if (hour <= previousHour) date = addDays(date, 1);
      previousHour = hour;

      const precipitationType = rows[3][hourIndex].textContent.trim();
      const thunderstormMatch = `${precipitationType} ${rows[3][hourIndex].getAttribute("title") ?? ""}`.match(
        /thunderstorm|thunder storm/i
      );
      const nightId = hour < 12 ? addDays(date, -1) : date;
      const night = nights.get(nightId) ?? [];
      nights.set(nightId, night);
      night.push({
        hour,
        timestampUtc: toUtcTimestamp(date, hour, header.utcOffsetMinutes),
        temperatureC: parseNumber(rows[0][hourIndex].textContent),
        dewPointC: hasDewPoints ? parseNumber(dewPoints[hourIndex].textContent) : null,
        cloudCoverTotalPct: parseNumber(rows[1][hourIndex].textContent),
        precipitationProbabilityPct: parseNumber(rows[2][hourIndex].textContent),
        thunderstormRisk: thunderstormMatch ? true : precipitationType ? false : null
      });
    });
  });

  if (nights.size === 0) {
    throw new Error("Clear Outside page contains no forecast hours");
  }

  // The first hour is the hour the page was generated: if the dating went
  // wrong (a label read as the wrong date), fail rather than store a shifted day.
  const generated = Date.parse(toUtcTimestamp(header.startDate, header.generatedHour, header.utcOffsetMinutes));
  const first = Math.min(...[...nights.values()].flat().map((hour) => Date.parse(hour.timestampUtc)));
  if (Math.abs(first - generated) > 2 * 3_600_000) {
    throw new Error(`Clear Outside page starts at ${new Date(first).toISOString()}, not near its generation time`);
  }

  return [...nights.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([nightId, hours]) => ({
      nightId,
      hours: hours.sort((a, b) => a.timestampUtc.localeCompare(b.timestampUtc))
    }));
}

export async function fetchClearOutsideHtml(latitude: number, longitude: number): Promise<string> {
  const url = `https://clearoutside.com/forecast/${latitude}/${longitude}`;
  const maxAttempts = 2;

  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15_000);
    let response: Response;

    try {
      response = await fetch(url, {
        headers: {
          "user-agent": "AstroWeather/0.1 (weather forecast research)"
        },
        signal: controller.signal
      });
    } catch (cause) {
      clearTimeout(timeout);
      if (attempt === maxAttempts - 1) {
        throw cause;
      }

      await retryDelay(attempt);
      continue;
  }

    clearTimeout(timeout);

    if (response.ok) {
      return response.text();
    }

    if (response.status === 429) {
      const retryAfter = response.headers.get("retry-after");
      const retryAfterMessage = retryAfter ? `; retry after ${retryAfter}` : "";
      throw new Error(`Clear Outside request failed with HTTP 429${retryAfterMessage}`);
    }

    if (response.status < 500 || attempt === maxAttempts - 1) {
      throw new Error(`Clear Outside request failed with HTTP ${response.status}`);
    }

    await retryDelay(attempt);
  }

  throw new Error("Clear Outside request failed");
}

function retryDelay(attempt: number): Promise<void> {
  const baseDelay = Math.min(2_000, 250 * 2 ** attempt);

  return new Promise((resolve) => {
    setTimeout(resolve, baseDelay + Math.floor(Math.random() * 100));
  });
}
