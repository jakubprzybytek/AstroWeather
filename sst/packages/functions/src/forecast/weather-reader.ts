import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import { DynamoDBDocumentClient, QueryCommand } from "@aws-sdk/lib-dynamodb";
import { Resource } from "sst";
import type { ClearOutsideItem } from "../weather/clearoutside-storage";
import type { NightWindow } from "./astronomy";
import { cloudLevel, encodeMatrix, quartileLevel, type MatrixCell } from "./matrix";
import { instantAtLocal, observingSlots } from "./nights";

type WeatherReader = {
  read(configurationId: string, nightIds: string[], now: Date): Promise<Map<string, ClearOutsideItem>>;
};

function isWeatherItem(value: unknown): value is ClearOutsideItem {
  if (!value || typeof value !== "object") return false;
  const item = value as Partial<ClearOutsideItem>;
  return typeof item.pk === "string" && typeof item.sk === "string"
    && typeof item.nightId === "string" && item.sk === `NIGHT#${item.nightId}#WEATHER`
    && item.service === "skyConditions" && Array.isArray(item.hours)
    && typeof item.expireAt === "number";
}

export function projectWeather(item: ClearOutsideItem, timezone: string, night: NightWindow | null): Pick<ReturnType<typeof weatherFields>, "cloud" | "precipitation" | "duskTemperature" | "nightMinimumTemperature"> {
  return weatherFields(item, timezone, night);
}

// Precipitation probability in quarters; a thunderstorm risk
// shows as `*`, the brightest level blinking, whatever the probability.
export function precipitationCell(probabilityPercent: number | null | undefined, thunderstormRisk: boolean | null | undefined): MatrixCell | null {
  if (thunderstormRisk) return "*";
  return probabilityPercent == null ? null : quartileLevel(probabilityPercent);
}

// Clear Outside gives whole degrees, so a temperature is sent without a
// decimal point; String() turns a rounded -0 into "0".
export function wholeDegrees(celsius: number): string {
  return String(Math.round(celsius));
}

const HOUR_MS = 3_600_000;

type Reading = { time: number; celsius: number; dewPointC?: number | null };

// A value at `instant`, interpolated between the hourly readings on either
// side that have it; with only one of them within the hour, that one; null
// with neither.
function valueAt(readings: Reading[], instant: number, pick: (reading: Reading) => number | null | undefined): number | null {
  let before: { time: number; value: number } | undefined;
  let after: { time: number; value: number } | undefined;
  for (const reading of readings) {
    const value = pick(reading);
    if (value == null) continue;
    if (reading.time <= instant && instant - reading.time < HOUR_MS && (!before || reading.time > before.time)) before = { time: reading.time, value };
    if (reading.time > instant && reading.time - instant < HOUR_MS && (!after || reading.time < after.time)) after = { time: reading.time, value };
  }
  if (before && after) {
    return before.value + (after.value - before.value) * (instant - before.time) / (after.time - before.time);
  }
  return before?.value ?? after?.value ?? null;
}

export function temperatureAt(readings: Reading[], instant: number): number | null {
  return valueAt(readings, instant, (reading) => reading.celsius);
}

// Dew on the optics when the air is within this of its dew point: on a clear
// night the optics radiate and run 1-3 degrees colder than the air.
export const DEW_SPREAD_C = 2;
// Frost instead of dew at or below this air temperature, for the same reason.
export const FROST_TEMPERATURE_C = 1;

export type MoistureMark = "" | "d" | "F";

// The mark for `celsius` with dew point `dewPointC`: `F` frost, `d` dew, ""
// none, also when the dew point is not known.
export function moistureMark(celsius: number, dewPointC: number | null): MoistureMark {
  if (dewPointC === null || celsius - dewPointC > DEW_SPREAD_C) return "";
  return celsius <= FROST_TEMPERATURE_C ? "F" : "d";
}

// Whole degrees as display text: with a mark, the mark in the first of the
// four digits and the degrees right-aligned after it ("d 12", "F -3",
// "d-14"); without, the degrees alone, which the board right-aligns.
export function temperatureText(celsius: number, mark: MoistureMark): string {
  const degrees = wholeDegrees(celsius);
  return mark ? `${mark}${degrees.padStart(3)}` : degrees;
}

// The observing night's temperatures as display text: at its start (dusk),
// and the lowest from its start to its end, counting the interpolated values
// at both ends; each marked when the air there is near its dew point.
// `?` without a night or without readings for it.
export function nightTemperatures(readings: Reading[], night: NightWindow | null): { duskTemperature: string; nightMinimumTemperature: string } {
  if (!night) return { duskTemperature: "?", nightMinimumTemperature: "?" };
  const start = night.start.getTime();
  const end = night.end.getTime();
  const dewPointAt = (instant: number) => valueAt(readings, instant, (reading) => reading.dewPointC);
  const candidates = readings
    .filter((reading) => reading.time > start && reading.time < end)
    .map((reading) => ({ time: reading.time, celsius: reading.celsius }));
  const atStart = temperatureAt(readings, start);
  const atEnd = temperatureAt(readings, end);
  if (atStart !== null) candidates.push({ time: start, celsius: atStart });
  if (atEnd !== null) candidates.push({ time: end, celsius: atEnd });
  let minimum: { time: number; celsius: number } | undefined;
  for (const candidate of candidates) {
    if (!minimum || candidate.celsius < minimum.celsius) minimum = candidate;
  }
  return {
    duskTemperature: atStart === null ? "?" : temperatureText(atStart, moistureMark(atStart, dewPointAt(start))),
    nightMinimumTemperature: minimum ? temperatureText(minimum.celsius, moistureMark(minimum.celsius, dewPointAt(minimum.time))) : "?"
  };
}

function weatherFields(item: ClearOutsideItem, timezone: string, night: NightWindow | null) {
  const slots = observingSlots(item.nightId, timezone);
  const cloudValues: Array<MatrixCell | null> = [];
  const precipitationValues: Array<MatrixCell | null> = [];

  for (const slot of slots) {
    const hour = item.hours.find((candidate) => {
      const expected = instantAtLocal(slot.date, slot.hour, 0, timezone).getTime();
      return Date.parse(candidate.timestampUtc) === expected;
    });
    cloudValues.push(hour?.cloudCoverTotalPct == null ? null : cloudLevel(hour.cloudCoverTotalPct));
    precipitationValues.push(precipitationCell(hour?.precipitationProbabilityPct, hour?.thunderstormRisk));
  }

  const readings: Reading[] = [];
  for (const hour of item.hours) {
    const time = Date.parse(hour.timestampUtc);
    if (!Number.isNaN(time) && hour.temperatureC != null) readings.push({ time, celsius: hour.temperatureC, dewPointC: hour.dewPointC });
  }

  return {
    cloud: encodeMatrix(cloudValues),
    precipitation: encodeMatrix(precipitationValues),
    ...nightTemperatures(readings, night)
  };
}

export function createWeatherReader(
  tableName: string = (Resource as unknown as { ForecastData: { name: string } }).ForecastData.name,
  client = DynamoDBDocumentClient.from(new DynamoDBClient({}))
): WeatherReader {
  return {
    async read(configurationId, nightIds, now) {
      const result = new Map<string, ClearOutsideItem>();
      let exclusiveStartKey: Record<string, unknown> | undefined;
      do {
        const response = await client.send(new QueryCommand({
          TableName: tableName,
          KeyConditionExpression: "pk = :pk AND sk BETWEEN :start AND :end",
          ExpressionAttributeValues: {
            ":pk": `LOC#${configurationId}`,
            ":start": `NIGHT#${nightIds[0]}`,
            ":end": `NIGHT#${nightIds[nightIds.length - 1]}#WEATHER`
          },
          ExclusiveStartKey: exclusiveStartKey
        }));
        for (const value of response.Items ?? []) {
          if (!isWeatherItem(value) || !nightIds.includes(value.nightId) || value.expireAt <= Math.floor(now.getTime() / 1000)) continue;
          result.set(value.nightId, value);
        }
        exclusiveStartKey = response.LastEvaluatedKey;
      } while (exclusiveStartKey);
      return result;
    }
  };
}