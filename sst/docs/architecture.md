# AstroWeather API Architecture

## Overview
AstroWeather is a serverless API built on **AWS** using the **SST (Serverless Stack)** framework. The API presents a single view of forecast data for a configured location, including astronomical events, weather forecasts, northern lights (aurora) forecasts, and additional data sources as they are added.

The API response is organized by night. Each night can contain an `astro` section, a `weather` section, an `auroraForecast` section, and other service sections in the future. Clients do not need to call a separate endpoint for each data source.

## Core Components
### 1. API Gateway (SST `Api`)
- **Endpoint**: `GET /astro/{configurationId}`
- **Input**: `configurationId` (path parameter)
- **Output**: JSON payload containing the available astronomical, weather, aurora, and related forecast data for the configured location, grouped by night.

### 2. Lambda Function
- **Handler**: Processes the `configurationId` and assembles the response.
- **Logic**:
  - Resolves the configuration and its location data (latitude, longitude, timezone).
  - Reads or calculates the available data for the requested nights.
  - Merges the independent service records into one formatted JSON response.

### 3. Configuration and location

The public identifier is currently called `configurationId` rather than
`locationId`. At present, a configuration contains only a location, so
`locationId` would be a valid and simpler name. `configurationId` is retained
because the profile can later include settings that are not properties of a
location, such as units, forecast horizon, enabled data sources, or presentation
preferences. This keeps the API contract open to those additions without
renaming the path parameter later.

For now, configurations are deliberately simple and are hardcoded in a shared
global configuration file as a JSON object. The object maps each identifier to
its location and timezone:

```typescript
const configurations = {
  "krakow-home": {
    location: { lat: 50.0647, lon: 19.9450, tz: "Europe/Warsaw" }
  }
};
```

The configuration collection can eventually be managed with CRUD operations,
with profiles stored independently from the nightly forecast records. That is
not required for the current implementation; keeping the configuration in one
global JSON object is sufficient while the model and API are being established.

### 4. Data Storage

**Amazon DynamoDB** stores the nightly data described below. Configuration
profiles remain in the shared global JSON object for now; they can be moved to
DynamoDB when CRUD management is needed.

## Nightly Data Architecture

The API serves astronomical events, weather forecasts, northern lights (aurora)
forecasts, and other available services for tonight and the next few nights.
These sources update at different schedules and paces, so persistence is split
**by night** and **by service** while the API still provides a single fast read
per location.

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
  `weather`, `auroraForecast`, and any additional service keys, even though the
  writes are fully decoupled.

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

## Technical Stack
- **Infrastructure as Code**: SST (v3/Ion or v2)
- **Runtime**: Node.js / TypeScript
- **Library**: `suncalc`
- **Cloud Provider**: AWS

## Project Structure
- `sst/stacks/`: Infrastructure definitions.
- `sst/packages/functions/src/`: Lambda handler code.
- `sst/docs/`: Documentation.

## Data Flow
1. Client calls `GET /astro/krakow-home`.
2. API Gateway triggers the Lambda.
3. Lambda resolves the `krakow-home` configuration and its location.
4. The Lambda reads or computes the available astro, weather, aurora, and other
  service data for the requested night range.
5. The Lambda returns one JSON response grouped by night.

## Web UI

The React/TypeScript web UI lives in `packages/web` and is hosted by an SST
`StaticSite` component backed by S3 and CloudFront. At build time, SST injects
the API URL as `VITE_API_URL`. The browser calls API Gateway, which invokes the
Lambda and returns the unified nightly data for the selected configuration and
location.

The project structure is:

```text
sst/
├── packages/functions/   # API Lambda handlers
├── packages/web/         # Vite + React UI
├── docs/                 # Architecture and testing documentation
└── sst.config.ts         # API and StaticSite infrastructure
```
