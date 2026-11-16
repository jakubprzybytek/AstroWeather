import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import { DynamoDBDocumentClient, GetCommand, PutCommand } from "@aws-sdk/lib-dynamodb";
import { Resource } from "sst";
import type { ClearOutsideHour, ClearOutsideNight } from "./clearoutside";

export type ClearOutsideItem = {
  pk: string;
  sk: string;
  configurationId: string;
  nightId: string;
  service: "skyConditions";
  coordinates: {
    latitude: number;
    longitude: number;
  };
  hours: ClearOutsideHour[];
  fetchedAt: string;
  expireAt: number;
};

export type ClearOutsideStore = {
  getNight(configurationId: string, nightId: string): Promise<ClearOutsideItem | undefined>;
  putNight(item: ClearOutsideItem): Promise<void>;
};

// The page starts at the hour it was generated, so the night in progress comes
// without its earlier hours. Keep those from the stored night, so a run at
// 18:00 or midnight doesn't drop the dusk hours of tonight.
export function withEarlierHours(item: ClearOutsideItem, stored: ClearOutsideItem | undefined): ClearOutsideItem {
  if (!stored || stored.nightId !== item.nightId || item.hours.length === 0) return item;
  const firstNew = Date.parse(item.hours[0].timestampUtc);
  const earlier = stored.hours.filter((hour) => Date.parse(hour.timestampUtc) < firstNew);
  return earlier.length === 0 ? item : { ...item, hours: [...earlier, ...item.hours] };
}

function latestTimestamp(hours: ClearOutsideHour[]): number {
  if (hours.length === 0) {
    throw new Error("Cannot store a Clear Outside night without hourly data");
  }

  const timestamps = hours.map((hour) => Date.parse(hour.timestampUtc));
  if (timestamps.some((timestamp) => Number.isNaN(timestamp))) {
    throw new Error("Cannot store a Clear Outside night with an invalid timestamp");
  }

  return Math.max(...timestamps);
}

export function toClearOutsideItem(
  configurationId: string,
  coordinates: { latitude: number; longitude: number },
  night: ClearOutsideNight,
  fetchedAt: string
): ClearOutsideItem {
  const lastHourMs = latestTimestamp(night.hours);

  return {
    pk: `LOC#${configurationId}`,
    sk: `NIGHT#${night.nightId}#WEATHER`,
    configurationId,
    nightId: night.nightId,
    service: "skyConditions",
    coordinates,
    hours: night.hours,
    fetchedAt,
    expireAt: Math.floor(lastHourMs / 1000) + 72 * 60 * 60
  };
}

export function createClearOutsideStore(
  tableName: string = (Resource as unknown as { ForecastData: { name: string } }).ForecastData.name,
  client = DynamoDBDocumentClient.from(new DynamoDBClient({}))
): ClearOutsideStore {
  return {
    async getNight(configurationId, nightId) {
      const result = await client.send(new GetCommand({
        TableName: tableName,
        Key: { pk: `LOC#${configurationId}`, sk: `NIGHT#${nightId}#WEATHER` }
      }));
      return result.Item as ClearOutsideItem | undefined;
    },
    async putNight(item) {
      await client.send(new PutCommand({ TableName: tableName, Item: item }));
    }
  };
}