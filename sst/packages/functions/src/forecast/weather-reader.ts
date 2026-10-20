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

type Reading = { time: number; celsius: number };

// The temperature at `instant`, interpolated between the hourly readings on
// either side; with only one of them within the hour, that one; null with
// neither.
export function temperatureAt(readings: Reading[], instant: number): number | null {
  let before: Reading | undefined;
  let after: Reading | undefined;
  for (const reading of readings) {
    if (reading.time <= instant && instant - reading.time < HOUR_MS && (!before || reading.time > before.time)) before = reading;
    if (reading.time > instant && reading.time - instant < HOUR_MS && (!after || reading.time < after.time)) after = reading;
  }
  if (before && after) {
    return before.celsius + (after.celsius - before.celsius) * (instant - before.time) / (after.time - before.time);
  }
  return before?.celsius ?? after?.celsius ?? null;
}

// The observing night's temperatures: at its start (dusk), and the lowest
// from its start to its end, counting the interpolated values at both ends.
// `?` without a night or without readings for it.
export function nightTemperatures(readings: Reading[], night: NightWindow | null): { duskTemperature: string; nightMinimumTemperature: string } {
  if (!night) return { duskTemperature: "?", nightMinimumTemperature: "?" };
  const start = night.start.getTime();
  const end = night.end.getTime();
  const atStart = temperatureAt(readings, start);
  const atEnd = temperatureAt(readings, end);
  const values = readings.filter((reading) => reading.time > start && reading.time < end).map((reading) => reading.celsius);
  if (atStart !== null) values.push(atStart);
  if (atEnd !== null) values.push(atEnd);
  return {
    duskTemperature: atStart === null ? "?" : wholeDegrees(atStart),
    nightMinimumTemperature: values.length ? wholeDegrees(Math.min(...values)) : "?"
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
    if (!Number.isNaN(time) && hour.temperatureC != null) readings.push({ time, celsius: hour.temperatureC });
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