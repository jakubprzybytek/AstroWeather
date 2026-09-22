# AstroWeather API Architecture

## Overview
AstroWeather is a serverless API built on **AWS** using the **SST (Serverless Stack)** framework. The API presents a single view of forecast data for a configured location, including astronomical events, weather forecasts, northern lights (aurora) forecasts, and additional data sources as they are added.

The main API response is a fixed six-display text protocol. Each display combines
astronomy and weather fields for one observing night, while clients do not need
to call a separate endpoint for each data source.

## Core Components
### 1. API Gateway (SST `ApiGatewayV2`)
- **Endpoint**: `GET /configurations`
  - **Output**: JSON array of public configuration identifiers and labels for UI selectors.
- **Endpoint**: `GET /astro/{configurationId}`
  - **Input**: `configurationId` (path parameter)
  - **Output**: `text/plain; charset=utf-8` version 1 payload containing six fixed
    display blocks with astronomical and weather fields. See [api.md](api.md)
    and [api-payload.md](api-payload.md).
- **Endpoint**: `POST /tools/clearoutside`
  - **Output**: JSON with normalized hourly Clearoutside nights, used by the web
    UI's helper tool (see [Web UI](#web-ui)).
- **Throttling**: burst of one request and a steady rate of one request per second.

### 2. Lambda Functions
- **Forecast handler** (`packages/functions/src/astro.ts`): a thin adapter that
  resolves the configuration and delegates to `forecast/`:
  - `nights.ts` derives the six observing nights and 21 local-hour slots;
  - `astronomy.ts` calculates sunset, sunrise, and sun/moon matrices with `suncalc`;
  - `weather-reader.ts` reads and projects the stored `#WEATHER` items;
  - `assemble.ts` merges astronomy and weather independently, using sentinels
    when a source is unavailable;
  - `protocol.ts` validates the model and serializes the text payload.
- **Configurations handler** and **Clearoutside tool handler** for the other routes.
- **Clearoutside ingestion job** (`jobs/clearoutside-weather.ts`), described in
  [Write path](#write-path-independent-cadence-per-source).

### 3. API edge (CloudFront, HTTP and HTTPS)

The public API hostname points at a CloudFront distribution rather than directly
at API Gateway, so the API is reachable over both `http://` and `https://`
without redirects. This supports constrained clients, such as the embedded
device, that cannot use TLS.

```text
HTTP or HTTPS client
  |
  v
CloudFront (public API hostname, viewer protocol policy allow-all)
  |
  | HTTPS only
  v
API Gateway generated execute-api endpoint
  |
  v
Route Lambdas
```

- The distribution uses the managed `CachingDisabled` cache policy and the
  `AllViewerExceptHostHeader` origin request policy, so every request, method,
  query string, and body reaches API Gateway, which still owns routing, CORS,
  and throttling.
- The CloudFront certificate is issued by ACM in `us-east-1` and validated
  through Route 53; `A` and `AAAA` alias records point the API hostname at the
  distribution.
- The web UI always calls the API over HTTPS (`VITE_API_URL` uses `https://`),
  and CORS allows only the HTTPS web origin plus local Vite origins.

**Security constraints.** Plain HTTP exposes request paths, query strings,
bodies, and responses to interception and modification. It is accepted only as
compatibility behavior for public, non-sensitive forecast data. Do not add
credentials, cookies, tokens, API keys, or sensitive parameters to the API
while HTTP is allowed. Any future authenticated or sensitive route must be
HTTPS-only, for example on a separate hostname whose CloudFront behavior uses
`redirect-to-https`. HSTS is intentionally not enabled.

### 4. Configuration and location

The public identifier is currently called `configurationId` rather than
`locationId`. At present, a configuration contains only a location, so
`locationId` would be a valid and simpler name. `configurationId` is retained
because the profile can later include settings that are not properties of a
location, such as units, forecast horizon, enabled data sources, or presentation
preferences. This keeps the API contract open to those additions without
renaming the path parameter later.

For now, configurations are deliberately simple and are hardcoded in
`packages/functions/src/configurations.ts`. The object maps each identifier to
a display label, location, and timezone:

```typescript
export const configurations = {
  "wroclaw": {
    label: "Wrocław",
    location: { lat: 51.1079, lon: 17.0385, tz: "Europe/Warsaw" }
  },
  "krakow": {
    label: "Kraków",
    location: { lat: 50.0647, lon: 19.945, tz: "Europe/Warsaw" }
  }
} as const;
```

The configuration collection can eventually be managed with CRUD operations,
with profiles stored independently from the nightly forecast records. That is
not required for the current implementation; keeping the configuration in one
global JSON object is sufficient while the model and API are being established.

### 5. Data Storage

**Amazon DynamoDB** stores the nightly data described below. Configuration
profiles remain in the shared global JSON object for now; they can be moved to
DynamoDB when CRUD management is needed.

The `ForecastData` table uses `pk` and `sk` as its primary key and `expireAt`
as its DynamoDB TTL attribute. The scheduled Clearoutside writer stores one
weather item per configuration and forecast night:

```text
PK = LOC#<configurationId>
SK = NIGHT#<nightId>#WEATHER
```

Each item contains the configuration identifier, `nightId`,
`service: "skyConditions"`, coordinates, normalized hourly Clearoutside fields
(`hour`, `timestampUtc`, `temperatureC`, `cloudCoverTotalPct`,
`precipitationProbabilityPct`, `thunderstormRisk`), `fetchedAt`, and an
`expireAt` timestamp (epoch seconds) 72 hours after the last hourly forecast
value. Raw Clearoutside HTML is never persisted. DynamoDB TTL deletion is
eventual, so the forecast reader also discards items whose `expireAt` is at or
before the request time.

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

`nightIdFor` in `packages/functions/src/forecast/nights.ts` implements this with
the runtime's `Intl.DateTimeFormat` and IANA timezones: it takes the local date
and moves to the previous date when the local hour is before 12:00. No
date/time library is used.

### Persistence: DynamoDB, single table

DynamoDB is the best fit: small semi-structured JSON items, unpredictable write
cadence per source, on-demand pricing, native TTL for expiring old nights, and
no connection-pool concerns from Lambda.

**Key design** — partition by location, sort by night + service, so each source
writes independently without clobbering the others:

| PK | SK | attributes |
|---|---|---|
| `LOC#krakow` | `NIGHT#2026-09-10#WEATHER` | hourly forecast, `fetchedAt`, `expireAt` |
| `LOC#krakow` | `NIGHT#2026-09-10#AURORA` | kp-index/forecast, `fetchedAt`, `expireAt` (future) |
| `LOC#krakow` | `NIGHT#2026-09-11#WEATHER` | … |

Only `#WEATHER` items exist today. Astronomy is calculated on demand and is not
stored; `#AURORA` and other suffixes illustrate how future sources fit the key
design.

- **By-night split**: `Query(PK = LOC#krakow, SK begins_with "NIGHT#2026-09-10")`
  returns all services for one night in a single call. A range query
  (`SK between "NIGHT#2026-09-10" and "NIGHT#2026-09-14"`) returns "tonight +
  next few nights" in one query, since ISO dates sort correctly as strings.
- **By-service split**: each source (weather poller, astro calculator, aurora
  poller) only ever writes its own `#SERVICE` suffix, so independent schedules
  never conflict and a stale/failing source doesn't block the others.
- **Freshness/TTL**: each item carries an `expireAt` attribute (epoch seconds) a few
  days past the night; DynamoDB auto-deletes stale items, keeping the table small.
- **Merge at read time**: the API Lambda queries the weather night range and
  assembles the six fixed display blocks, even though source writes are fully
  decoupled.

**Optional GSI** for maintenance/backfill jobs (e.g. "find locations missing
weather data for night X"):

| GSI1PK | GSI1SK |
|---|---|
| `SERVICE#WEATHER` | `NIGHT#2026-09-10#LOC#krakow` |

### Write path (independent cadence per source)

- **Astro**: deterministic; computed on demand by the forecast Lambda for every
  request and not stored.
- **Weather (Clearoutside, implemented)**: the `ClearOutsideIngestion` `CronV2`
  schedule invokes a Lambda every six hours with no scheduler retries. It
  processes the configured locations sequentially with a one-second gap between
  them. Each fetch times out after 15 seconds and is retried once on network
  errors and HTTP `5xx`; HTTP `429` and other `4xx` responses are not retried.
  The whole page is parsed before anything is written, so a fetch or parse
  failure leaves the previous items untouched. Each parsed night is upserted as
  one `NIGHT#...#WEATHER` item with a deterministic key. A failed location is
  logged and does not block other locations; the invocation still throws after
  all locations are attempted so the failure is visible in monitoring, and the
  next scheduled run repairs it. See
  [Clear Outside Supplier Evaluation](clearoutside-weather-supplier.md) for
  feasibility, risks, and the outstanding permission question.
- **Weather (Meteosource, evaluated only)**: a documented API alternative; see
  [Meteosource Weather Supplier Evaluation](meteosource-weather-supplier.md) for
  live API results, plan limits, and integration guidance.
- **Aurora forecast (future)**: separate scheduled Lambda with its own polling
  interval; shorter TTL since forecasts go stale quickly.

Each writer is a small, independent Lambda + schedule, matching the existing
SST/Lambda-per-concern style and keeping blast radius small if one upstream API
changes or breaks.

## Technical Stack
- **Infrastructure as Code**: SST v4 (`sst.config.ts`), with Pulumi AWS
  resources for the API CloudFront distribution and DNS records
- **Runtime**: Node.js / TypeScript
- **Libraries**: `suncalc`, `node-html-parser`, AWS SDK v3 DynamoDB clients
- **Web UI**: React, Vite, React-Bootstrap
- **Cloud Provider**: AWS

## Data Flow
1. Client calls `GET /astro/krakow` over HTTP or HTTPS.
2. CloudFront forwards the request over HTTPS to API Gateway, which triggers the
   forecast Lambda.
3. The Lambda resolves the `krakow` configuration and its location.
4. The Lambda calculates astronomy and queries the stored weather items for the
   six requested nights.
5. The Lambda returns the versioned six-display text payload. The web UI shows
  that response body verbatim for inspection; the embedded client parses it.

## Web UI

The React/TypeScript web UI lives in `packages/web` and is hosted by an SST
`StaticSite` component backed by S3 and CloudFront. At build time, SST injects
the HTTPS API URL as `VITE_API_URL`. The main view calls
`GET /astro/{configurationId}` and shows the HTTP status, content type, and
response body verbatim; it deliberately does not parse the line protocol.

In deployed stages, the web UI uses an app-specific custom domain. The `prod` stage is
available at `https://astroweather.albedoonline.com`, with the API at
`https://api.astroweather.albedoonline.com`. Other stages use the stage name as
the first label, for example `https://int.astroweather.albedoonline.com` and
`https://api.int.astroweather.albedoonline.com`. SST manages the web
certificate and DNS records; the API certificate, CloudFront distribution, and
aliases are defined explicitly in `sst.config.ts` (see
[API edge](#3-api-edge-cloudfront-http-and-https)). All records live in the
`albedoonline.com` hosted zone. The API allows the matching web origin plus the
local Vite development origins.

The UI also exposes helper tools independently from the main astronomy view.
It loads the configuration list from `GET /configurations` and uses that list
for every configuration selector, so configuration identifiers and labels have
one backend source of truth.
The first tool is Clearoutside, backed by `POST /tools/clearoutside`. It accepts
either a configured `configurationId` or direct latitude/longitude coordinates,
resolves the final coordinates, fetches and parses the server-rendered
Clearoutside forecast in Lambda, and returns normalized hourly nights to the
browser.

The project structure is:

```text
sst/
├── packages/functions/
│   ├── src/              # Lambda handlers, forecast/, weather/, jobs/
│   └── tests/integration/
├── packages/web/         # Vite + React UI
├── docs/                 # Architecture, API, development, and testing documentation
└── sst.config.ts         # API, CloudFront, DynamoDB, schedule, and StaticSite
```
