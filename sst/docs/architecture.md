# AstroWeather API Architecture

## Overview
AstroWeather is a serverless API built on **AWS** using the **SST (Serverless Stack)** framework. The primary goal is to provide sunrise, sunset, moonrise, and moonset times for a specific location identified by a configuration ID.

## Core Components
### 1. API Gateway (SST `Api`)
- **Endpoint**: `GET /astro/{configId}`
- **Input**: `configId` (path parameter)
- **Output**: JSON payload containing astronomical rise/set times.

### 2. Lambda Function
- **Handler**: Processes the `configId`.
- **Logic**: 
  - Retrieves location data (latitude, longitude, timezone).
  - Calculates astronomical data using the **`suncalc`** library.
  - Returns formatted JSON.

### 3. Data Storage
- **Phase 1 (Current)**: Hardcoded mapping within the Lambda or a shared constant file.
  ```typescript
  const locations = {
    "krakow-home": { lat: 50.0647, lon: 19.9450, tz: "Europe/Warsaw" }
  };
  ```
- **Phase 2 (Future)**: **Amazon DynamoDB** table to store configuration profiles indexed by `configId`, plus the nightly data described below.

## Nightly Data Architecture (Phase 2)

The API will expand beyond astro rise/set times to also serve **weather forecasts**
and **northern lights (aurora) forecasts** for tonight and the next few nights.
These sources update on different schedules and paces, so the persistence design
has to split data **by night** and **by service** while still allowing a single
fast read per night.

### Defining a "night"

Data pivots around midnight, not noon-to-noon by calendar date. A "night" spans
**local noon → next local noon**, anchored to each location's timezone (not UTC):

```
nightId = the calendar date (YYYY-MM-DD) of the local noon that starts the night
```

`nightId = "2026-09-10"` represents the window `2026-09-10T12:00` →
`2026-09-11T12:00` in that location's timezone.

```typescript
function nightIdFor(t: DateTime, tz: string): string {
  const local = t.setZone(tz);
  const anchor = local.hour < 12 ? local.minus({ days: 1 }) : local;
  return anchor.toFormat('yyyy-LL-dd');
}
```

### Persistence: DynamoDB, single table

DynamoDB is the best fit: small semi-structured JSON items, unpredictable write
cadence per source, on-demand pricing, native TTL for expiring old nights, and
no connection-pool concerns from Lambda.

**Key design** — partition by location, sort by night + service, so each source
writes independently without clobbering the others:

| PK | SK | attributes |
|---|---|---|
| `LOC#krakow-home` | `NIGHT#2026-09-10#ASTRO` | sun/moon rise-set, `fetchedAt`, `ttl` |
| `LOC#krakow-home` | `NIGHT#2026-09-10#WEATHER` | forecast blob, `fetchedAt`, `ttl` |
| `LOC#krakow-home` | `NIGHT#2026-09-10#AURORA` | kp-index/forecast, `fetchedAt`, `ttl` |
| `LOC#krakow-home` | `NIGHT#2026-09-11#ASTRO` | … |

- **By-night split**: `Query(PK = LOC#krakow-home, SK begins_with "NIGHT#2026-09-10")`
  returns all services for one night in a single call. A range query
  (`SK between "NIGHT#2026-09-10" and "NIGHT#2026-09-14"`) returns "tonight +
  next few nights" in one query, since ISO dates sort correctly as strings.
- **By-service split**: each source (weather poller, astro calculator, aurora
  poller) only ever writes its own `#SERVICE` suffix, so independent schedules
  never conflict and a stale/failing source doesn't block the others.
- **Freshness/TTL**: each item carries a `ttl` attribute (epoch seconds) a few
  days past the night; DynamoDB auto-deletes stale items, keeping the table small.
- **Merge at read time**: the API Lambda queries the night range, groups items
  by `nightId`, and assembles one JSON response per night with keys `astro`,
  `weather`, `auroraForecast`, even though the writes are fully decoupled.

**Optional GSI** for maintenance/backfill jobs (e.g. "find locations missing
weather data for night X"):

| GSI1PK | GSI1SK |
|---|---|
| `SERVICE#WEATHER` | `NIGHT#2026-09-10#LOC#krakow-home` |

### Write path (independent cadence per source)

- **Astro**: deterministic; computed on-demand or via an infrequent scheduled
  Lambda (long or no TTL).
- **Weather**: EventBridge Scheduler triggers a Lambda every N minutes/hours per
  location, upserting `NIGHT#...#WEATHER` items for the next few nights.
- Meteosource is the first evaluated weather supplier; see
  [Meteosource Weather Supplier Evaluation](meteosource-weather-supplier.md) for
  live API results, plan limits, integration guidance, and the proposed weather
  item shape.
- Clear Outside was evaluated as a supplementary, astronomy-specific source
  (observing-condition rating, Bortle estimate, dark-sky windows) obtained via
  HTML scraping rather than an API; see
  [Clear Outside Supplier Evaluation](clearoutside-weather-supplier.md) for
  feasibility, risks, and integration guidance.
- **Aurora forecast**: separate scheduled Lambda with its own polling interval;
  shorter TTL since forecasts go stale quickly.

Each writer is a small, independent Lambda + schedule, matching the existing
SST/Lambda-per-concern style and keeping blast radius small if one upstream API
changes or breaks.

### Alternatives considered

- **S3 + JSON files**: viable, but weaker query semantics (no native range query
  across nights/services without prefix listing) and manual TTL cleanup. Could
  complement DynamoDB for large blobs (e.g. weather radar images), storing a
  pointer in DynamoDB and the blob in S3.
- **RDS/Aurora**: unnecessary relational overhead and always-on cost/connection
  management that fights the serverless model.
- **Timestream**: built for high-cardinality time-series metrics/analytics, not
  per-night structured lookups; more complexity than needed for a handful of
  nights per location.

## Technical Stack
- **Infrastructure as Code**: SST (v3/Ion or v2)
- **Runtime**: Node.js / TypeScript
- **Library**: `suncalc`
- **Cloud Provider**: AWS

## Project Structure (Proposed)
- `sst/stacks/`: Infrastructure definitions.
- `sst/packages/functions/src/`: Lambda handler code.
- `sst/docs/`: Documentation.

## Data Flow
1. Client calls `GET /astro/krakow-home`.
2. API Gateway triggers the Lambda.
3. Lambda looks up `krakow-home` coordinates.
4. Lambda computes times using `suncalc.getTimes()` and `suncalc.getMoonTimes()`.
5. Lambda returns the JSON response.

## Web UI

The React/TypeScript web UI lives in `packages/web` and is hosted by an SST
`StaticSite` component backed by S3 and CloudFront. At build time, SST injects
the API URL as `VITE_API_URL`. The browser calls API Gateway, which invokes the
Lambda and returns the sun and moon data for the selected location.

The project structure is:

```text
sst/
├── packages/functions/   # API Lambda handlers
├── packages/web/         # Vite + React UI
├── docs/                 # Architecture and testing documentation
└── sst.config.ts         # API and StaticSite infrastructure
```

## Implementation Steps
1. Initialize SST project in the `sst` folder.
2. Install `suncalc` and `@types/suncalc`.
3. Create a Lambda handler that parses `configId`, looks up location, and calls `suncalc`.
4. Set up the SST API stack.
