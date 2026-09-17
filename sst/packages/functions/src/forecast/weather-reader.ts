import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import { DynamoDBDocumentClient, QueryCommand } from "@aws-sdk/lib-dynamodb";
import { Resource } from "sst";
import type { ClearOutsideItem } from "../weather/clearoutside-storage";
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

export function projectWeather(item: ClearOutsideItem, timezone: string): Pick<ReturnType<typeof weatherFields>, "cloud" | "thunderstorm" | "maximumTemperature" | "minimumTemperature"> {
  return weatherFields(item, timezone);
}

function weatherFields(item: ClearOutsideItem, timezone: string) {
  const slots = observingSlots(item.nightId, timezone);
  const cloudValues: Array<boolean | null> = [];
  const thunderstormValues: Array<boolean | null> = [];
  const temperatures: number[] = [];

  for (const slot of slots) {
    const hour = item.hours.find((candidate) => {
      const expected = instantAtLocal(slot.date, slot.hour, 0, timezone).getTime();
      return Date.parse(candidate.timestampUtc) === expected;
    });
    cloudValues.push(hour?.cloudCoverTotalPct == null ? null : hour.cloudCoverTotalPct >= 10);
    thunderstormValues.push(hour?.thunderstormRisk ?? null);
  }

  const start = instantAtLocal(item.nightId, 12, 0, timezone).getTime();
  const nextDate = new Date(`${item.nightId}T12:00:00Z`);
  nextDate.setUTCDate(nextDate.getUTCDate() + 1);
  const endInstant = instantAtLocal(nextDate.toISOString().slice(0, 10), 12, 0, timezone).getTime();
  for (const hour of item.hours) {
    const timestamp = Date.parse(hour.timestampUtc);
    if (timestamp >= start && timestamp < endInstant && hour.temperatureC != null) temperatures.push(hour.temperatureC);
  }

  const encode = (values: Array<boolean | null>) => values.some((value) => value !== null)
    ? values.map((value) => value === null ? "?" : value ? "*" : ".").join("") : "?";
  return {
    cloud: encode(cloudValues),
    thunderstorm: encode(thunderstormValues),
    maximumTemperature: temperatures.length ? Math.max(...temperatures).toFixed(1) : "?",
    minimumTemperature: temperatures.length ? Math.min(...temperatures).toFixed(1) : "?"
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