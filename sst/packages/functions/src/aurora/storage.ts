import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import { DynamoDBDocumentClient, GetCommand, PutCommand, QueryCommand } from "@aws-sdk/lib-dynamodb";
import { Resource } from "sst";
import { addDays, instantAtLocal } from "../forecast/nights";
import type { GfzHp60Values } from "./gfz-hp60";
import type { NoaaKpValues } from "./noaa-kp";
import type { NoaaOutlookValues } from "./noaa-outlook";
import type { Span } from "./spans";

// One item per location, night and source, under the night's `#AURORA`
// prefix; merged at read time. See docs/aurora-forecast-supplier.md#merging-sources.
export type AuroraSourceKey = "GFZ" | "NOAA3" | "NOAA27";

type ItemBase = {
  pk: string;
  sk: string;
  configurationId: string;
  nightId: string;
  fetchedAt: string;
  expireAt: number;
};

export type AuroraSpansItem<K extends AuroraSourceKey, T> = ItemBase & {
  source: K;
  spans: Span<T>[];
  lastModified: string | null;
  issuedAt?: string;
};

export type GfzItem = AuroraSpansItem<"GFZ", GfzHp60Values>;
export type Noaa3Item = AuroraSpansItem<"NOAA3", NoaaKpValues>;
export type Noaa27Item = AuroraSpansItem<"NOAA27", NoaaOutlookValues>;

// The nowcast per observing slot, keyed by the slot's start (ISO): the latest
// sample that targeted it and the maximum over all of them.
export type OvationSlot = {
  latestPct: number;
  maxPct: number;
  latestValidAt: string;
  samples: number;
};

export type OvationItem = ItemBase & {
  source: "OVATION";
  slots: Record<string, OvationSlot>;
};

// Set once for a night by the forecast ingestion, and kept for the night.
export type FlagItem = ItemBase & {
  source: "FLAG";
  reason: string;
};

export type AuroraItem = GfzItem | Noaa3Item | Noaa27Item | OvationItem | FlagItem;
export type AuroraItemKind = AuroraItem["source"];

export type AuroraNight = {
  gfz?: GfzItem;
  noaa3?: Noaa3Item;
  noaa27?: Noaa27Item;
  ovation?: OvationItem;
  flag?: FlagItem;
};

export function auroraKey(configurationId: string, nightId: string, kind: AuroraItemKind) {
  return { pk: `LOC#${configurationId}`, sk: `NIGHT#${nightId}#AURORA#${kind}` };
}

// As for the weather: kept until three days after the night ends.
export function auroraExpireAt(nightId: string, timezone: string): number {
  const nightEnd = instantAtLocal(addDays(nightId, 1), 12, 0, timezone).getTime();
  return Math.floor(nightEnd / 1000) + 72 * 60 * 60;
}

function isAuroraItem(value: unknown): value is AuroraItem {
  if (!value || typeof value !== "object") return false;
  const item = value as Partial<AuroraItem>;
  return typeof item.sk === "string" && typeof item.nightId === "string"
    && typeof item.source === "string" && item.sk === `NIGHT#${item.nightId}#AURORA#${item.source}`
    && typeof item.expireAt === "number";
}

export function addToNight(night: AuroraNight, item: AuroraItem): void {
  switch (item.source) {
    case "GFZ": night.gfz = item; break;
    case "NOAA3": night.noaa3 = item; break;
    case "NOAA27": night.noaa27 = item; break;
    case "OVATION": night.ovation = item; break;
    case "FLAG": night.flag = item; break;
  }
}

// One nowcast sample per location and run, kept for good (no `expireAt`, so
// the table's TTL never removes it): the data the nowcast thresholds are
// calibrated from after real storms. See
// docs/aurora-forecast-supplier.md#calibrating-the-nowcast.
export type CalibrationSample = {
  configurationId: string;
  observedAt: string;
  validAt: string;
  [field: string]: unknown;
};

export function calibrationKey(configurationId: string, observedAt: string) {
  return { pk: `LOC#${configurationId}`, sk: `CALIBRATION#AURORA#${observedAt}` };
}

export type AuroraStore = {
  put(item: AuroraItem): Promise<void>;
  putCalibration(sample: CalibrationSample): Promise<void>;
  get<K extends AuroraItemKind>(configurationId: string, nightId: string, kind: K): Promise<Extract<AuroraItem, { source: K }> | undefined>;
  // The items of consecutive nights, skipping expired ones.
  readNights(configurationId: string, nightIds: string[], now: Date): Promise<Map<string, AuroraNight>>;
};

export function createAuroraStore(
  tableName: string = (Resource as unknown as { ForecastData: { name: string } }).ForecastData.name,
  client = DynamoDBDocumentClient.from(new DynamoDBClient({}))
): AuroraStore {
  return {
    async put(item) {
      await client.send(new PutCommand({ TableName: tableName, Item: item }));
    },
    async putCalibration(sample) {
      await client.send(new PutCommand({
        TableName: tableName,
        Item: { ...calibrationKey(sample.configurationId, sample.observedAt), ...sample }
      }));
    },
    async get(configurationId, nightId, kind) {
      const response = await client.send(new GetCommand({ TableName: tableName, Key: auroraKey(configurationId, nightId, kind) }));
      return isAuroraItem(response.Item) && response.Item.source === kind
        ? response.Item as Extract<AuroraItem, { source: typeof kind }>
        : undefined;
    },
    async readNights(configurationId, nightIds, now) {
      const result = new Map<string, AuroraNight>();
      let exclusiveStartKey: Record<string, unknown> | undefined;
      do {
        // The range also holds the nights' weather items, which are skipped.
        const response = await client.send(new QueryCommand({
          TableName: tableName,
          KeyConditionExpression: "pk = :pk AND sk BETWEEN :start AND :end",
          ExpressionAttributeValues: {
            ":pk": `LOC#${configurationId}`,
            ":start": `NIGHT#${nightIds[0]}#AURORA#`,
            ":end": `NIGHT#${nightIds[nightIds.length - 1]}#AURORA#~`
          },
          ExclusiveStartKey: exclusiveStartKey
        }));
        for (const value of response.Items ?? []) {
          if (!isAuroraItem(value) || !nightIds.includes(value.nightId) || value.expireAt <= Math.floor(now.getTime() / 1000)) continue;
          const night = result.get(value.nightId) ?? {};
          addToNight(night, value);
          result.set(value.nightId, night);
        }
        exclusiveStartKey = response.LastEvaluatedKey;
      } while (exclusiveStartKey);
      return result;
    }
  };
}
