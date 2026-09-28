import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import { DynamoDBDocumentClient, QueryCommand } from "@aws-sdk/lib-dynamodb";
import { Resource } from "sst";
import type { ClearOutsideItem } from "../weather/clearoutside-storage";
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

export function projectWeather(item: ClearOutsideItem, timezone: string): Pick<ReturnType<typeof weatherFields>, "cloud" | "precipitation" | "maximumTemperature" | "minimumTemperature"> {
  return weatherFields(item, timezone);
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

function weatherFields(item: ClearOutsideItem, timezone: string) {
  const slots = observingSlots(item.nightId, timezone);
  const cloudValues: Array<MatrixCell | null> = [];
  const precipitationValues: Array<MatrixCell | null> = [];
  const temperatures: number[] = [];

  for (const slot of slots) {
    const hour = item.hours.find((candidate) => {
      const expected = instantAtLocal(slot.date, slot.hour, 0, timezone).getTime();
      return Date.parse(candidate.timestampUtc) === expected;
    });
    cloudValues.push(hour?.cloudCoverTotalPct == null ? null : cloudLevel(hour.cloudCoverTotalPct));
    precipitationValues.push(precipitationCell(hour?.precipitationProbabilityPct, hour?.thunderstormRisk));
  }

  const start = instantAtLocal(item.nightId, 12, 0, timezone).getTime();
  const nextDate = new Date(`${item.nightId}T12:00:00Z`);
  nextDate.setUTCDate(nextDate.getUTCDate() + 1);
  const endInstant = instantAtLocal(nextDate.toISOString().slice(0, 10), 12, 0, timezone).getTime();
  for (const hour of item.hours) {
    const timestamp = Date.parse(hour.timestampUtc);
    if (timestamp >= start && timestamp < endInstant && hour.temperatureC != null) temperatures.push(hour.temperatureC);
  }

  return {
    cloud: encodeMatrix(cloudValues),
    precipitation: encodeMatrix(precipitationValues),
    maximumTemperature: temperatures.length ? wholeDegrees(Math.max(...temperatures)) : "?",
    minimumTemperature: temperatures.length ? wholeDegrees(Math.min(...temperatures)) : "?"
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