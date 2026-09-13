import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import { DynamoDBDocumentClient, PutCommand } from "@aws-sdk/lib-dynamodb";
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
  putNight(item: ClearOutsideItem): Promise<void>;
};

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
    sk: `NIGHT#${night.nightId}#SKY_CONDITIONS`,
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
    async putNight(item) {
      await client.send(new PutCommand({ TableName: tableName, Item: item }));
    }
  };
}