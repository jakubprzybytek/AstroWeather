import { parse } from "node-html-parser";

export type ClearOutsideHour = {
  timestampUtc: string;
  temperatureC: number | null;
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
  utcOffsetMinutes: number;
};

const REQUIRED_ROWS = [
  "Temperature",
  "Total Clouds",
  "Precipitation Probability",
  "Precipitation Type"
] as const;

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
  return {
    startDate,
    utcOffsetMinutes: parseUtcOffset(text)
  };
}

function localDateAtNoon(startDate: string, dayOffset: number): Date {
  const date = new Date(`${startDate}T12:00:00.000Z`);
  date.setUTCDate(date.getUTCDate() + dayOffset);
  return date;
}

function formatDate(date: Date): string {
  return date.toISOString().slice(0, 10);
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

function toUtcTimestamp(nightDate: string, hour: number, utcOffsetMinutes: number): string {
  const localDate = new Date(`${nightDate}T00:00:00.000Z`);
  localDate.setUTCDate(localDate.getUTCDate() + (hour < 12 ? 1 : 0));
  localDate.setUTCHours(hour, 0, 0, 0);
  localDate.setUTCMinutes(localDate.getUTCMinutes() - utcOffsetMinutes);
  return localDate.toISOString();
}

export function parseClearOutside(html: string): ClearOutsideNight[] {
  const document = parse(html);
  const header = parseForecastHeader(document.querySelector("body")?.textContent ?? "");
  const days = document.querySelectorAll(".fc_day");

  if (days.length === 0) {
    throw new Error("Clear Outside page contains no forecast days");
  }

  return days.map((day, dayIndex) => {
    const nightDate = localDateAtNoon(header.startDate, dayIndex);
    const rows = REQUIRED_ROWS.map((label) => {
      const row = findRow(day, label);
      if (!row) {
        throw new Error(`Clear Outside page is missing the ${label} row`);
      }
      const values = rowValues(row);
      if (values.length === 0 || values.length > 24) {
        throw new Error(`Clear Outside ${label} row has ${values.length} hourly values`);
      }
      return values;
    });

    const hours = day.querySelectorAll(".fc_hours.fc_hour_ratings li");
    if (hours.length === 0 || hours.length > 24) {
      throw new Error(`Clear Outside day ${dayIndex} has ${hours.length} hour labels`);
    }

    if (rows.some((row) => row.length !== hours.length)) {
      throw new Error(`Clear Outside day ${dayIndex} has misaligned hourly rows`);
    }

    return {
      nightId: formatDate(nightDate),
      hours: hours.map((hourElement, hourIndex) => {
        const hour = parseHour(hourElement.textContent.replace(/\s+/g, " ").trim().split(" ")[0]);
        const precipitationType = rows[3][hourIndex].textContent.trim();
        const thunderstormMatch = `${precipitationType} ${rows[3][hourIndex].getAttribute("title") ?? ""}`.match(
          /thunderstorm|thunder storm/i
        );

        return {
          timestampUtc: toUtcTimestamp(formatDate(nightDate), hour, header.utcOffsetMinutes),
          temperatureC: parseNumber(rows[0][hourIndex].textContent),
          cloudCoverTotalPct: parseNumber(rows[1][hourIndex].textContent),
          precipitationProbabilityPct: parseNumber(rows[2][hourIndex].textContent),
          thunderstormRisk: thunderstormMatch ? true : precipitationType ? false : null
        };
      })
    };
  });
}

export async function fetchClearOutsideHtml(latitude: number, longitude: number): Promise<string> {
  const response = await fetch(`https://clearoutside.com/forecast/${latitude}/${longitude}`, {
    headers: {
      "user-agent": "AstroWeather/0.1 (weather forecast research)"
    }
  });

  if (!response.ok) {
    throw new Error(`Clear Outside request failed with HTTP ${response.status}`);
  }

  return response.text();
}
