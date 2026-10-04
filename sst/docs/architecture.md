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
- **Endpoint**: `GET /device/astro/{configurationId}?key=<device key>`
  - The same handler and payload as `GET /astro/{configurationId}`, for the
    embedded device (see [Access control](#access-control)).
- **Endpoint**: `POST /tools/clearoutside`
  - **Output**: JSON with normalized hourly Clearoutside nights, used by the web
    UI's helper tool (see [Web UI](#web-ui)).
- **Endpoints**: `POST /tools/gfz-hp60`, `POST /tools/noaa-kp`,
  `POST /tools/noaa-outlook`, `POST /tools/ovation`
  - **Output**: JSON with one aurora source's values grouped into local nights,
    at the source's own granularity, used by the web UI's helper tools (see
    [Web UI](#web-ui)).
- **Throttling**: burst of one request and a steady rate of one request per second.

#### Access control

The API is not public data for anyone who finds the hostname; every route
needs one of two credentials. This keeps it from being obviously open rather
than locking it down: anyone may create an account.

| Routes | Client | Credential | Checked by |
|---|---|---|---|
| `GET /configurations`, `GET /astro/{configurationId}`, `POST /tools/*` | Web UI | `Authorization: Bearer <Cognito access token>` | API Gateway JWT authorizer `Cognito` |
| `GET /device/astro/{configurationId}` | HostController | `key` query parameter | Lambda authorizer `DeviceKey` (`auth/device-key.ts`) |

- **Users** sign in to the shared Albedo user pool `eu-west-1_IVai0KEAA`
  (`AlbedoUserPool`), which this app does not create or manage. The web UI
  uses its own app client, `AstroWeatherWeb` (`2i29cn3m973qqh3re94fj7oejr`):
  public, no secret, SRP and refresh-token flows only. The pool allows
  self-registration, so any confirmed user is accepted; there is no group
  check. The JWT authorizer accepts tokens issued by the pool for that client.
- **The device** cannot add HTTP headers (the ST67 T01 driver builds the
  request from fixed templates), so its key travels in the query string. Over
  HTTPS the query string is encrypted, and SST's access log records
  `$context.path`, which excludes it. API Gateway rejects a request without
  `key` with `401` before calling the authorizer; a wrong key gets `403`.
  Decisions are cached for five minutes per key.
- **The key** is the SST secret `DeviceApiKey`, set per stage. It may hold
  several comma-separated keys: add the new key, switch the device, then
  remove the old one.
- CORS lists `authorization` and `content-type` by name, because a `*` in
  `Access-Control-Allow-Headers` never covers `Authorization`.

### 2. Lambda Functions
- **Forecast handler** (`packages/functions/src/astro.ts`): a thin adapter that
  resolves the configuration and delegates to `forecast/`:
  - `nights.ts` derives the six observing nights and 21 local-hour slots;
  - `astronomy.ts` calculates sunset, sunrise, and sun/moon matrices with `suncalc`;
  - `weather-reader.ts` reads and projects the stored `#WEATHER` items;
  - the aurora store (`aurora/storage.ts`) reads the stored `#AURORA#…` items
    and `aurora/merge.ts` merges them into the aurora row, slot by slot;
  - `assemble.ts` merges astronomy, weather and aurora independently, using
    sentinels when a source is unavailable, and sets the refresh interval;
  - `protocol.ts` validates the model and serializes the text payload.
- **Configurations handler** and **Clearoutside tool handler** for the other routes.
- **Aurora source tool handlers** (`tools/`): one `createSourceToolHandler`
  factory resolves the location and groups spans into nights; `aurora/` holds
  a fetcher and parser per source (`gfz-hp60.ts`, `noaa-kp.ts`,
  `noaa-outlook.ts`, `ovation.ts`) and the shared `spans.ts` night grouping.
- **Clearoutside ingestion job** (`jobs/clearoutside-weather.ts`) and the
  **aurora jobs** (`jobs/aurora-forecast.ts`, `jobs/aurora-nowcast.ts`),
  described in [Write path](#write-path-independent-cadence-per-source).

### 3. API edge (API Gateway custom domain, HTTPS only)

The public API hostname is an API Gateway custom domain (regional endpoint)
mapped to the HTTP API. API Gateway listens only on port 443, so `http://`
requests are not answered at all; there is no redirect.

```text
HTTPS client
  |
  v
API Gateway custom domain (public API hostname, TLS_1_2 security policy)
  |
  v
Route Lambdas
```

- SST's `ApiGatewayV2` `domain` option creates the ACM certificate in the app's
  region, validates it through Route 53, and adds the `A` and `AAAA` alias
  records, all through the `route53Dns()` adapter in `sst.config.ts`.
- The certificate is ACM RSA 2048 and chains to Amazon Root CA 1, the trust
  anchor the HostController loads into the ST67 module.
- The generated `execute-api` URL stays reachable and serves the same routes.
  SST's `AstroApi` URL output, and with it the integration tests, uses the
  custom domain.
- The web UI calls the API through `VITE_API_URL`, and CORS allows only the
  HTTPS web origin plus local Vite origins.

Until October 2026 the hostname pointed at a CloudFront distribution with viewer
protocol policy `allow-all`, so a device without TLS could use plain HTTP. The
HostController now verifies the server over HTTPS, so the distribution was
removed.

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
a display label, location, timezone and aurora threshold:

```typescript
export const configurations = {
  "wroclaw": {
    label: "Wrocław",
    location: { lat: 51.1079, lon: 17.0385, tz: "Europe/Warsaw" },
    aurora: { kpMain: 7 }
  },
  "krakow": {
    label: "Kraków",
    location: { lat: 50.0647, lon: 19.945, tz: "Europe/Warsaw" },
    aurora: { kpMain: 7.5 }
  }
} as const;
```

`aurora.kpMain` is the Kp at which an aurora becomes visible to the naked eye
from the location; the file's comment gives the procedure for computing it
from the location's corrected geomagnetic latitude.

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
| `LOC#krakow` | `NIGHT#2026-09-10#AURORA#GFZ` (one item per aurora source) | raw source spans, `Last-Modified`, `fetchedAt`, `expireAt` (future; merged at read time, see [aurora-forecast-supplier.md](aurora-forecast-supplier.md#merging-sources)) |
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
  schedule invokes a Lambda at 00:00, 06:00, 12:00 and 18:00 Europe/Warsaw time
  (`cron(0 0/6 * * ? *)` with a timezone, so DST is followed) with no scheduler
  retries. The HostController refreshes 10 minutes after each of these times,
  so the fixed hours matter: a `rate()` schedule would drift with each deploy. It
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
- **Aurora forecast (implemented)**: the `AuroraIngestion` `CronV2` runs on the
  weather's schedule (00:00, 06:00, 12:00 and 18:00 Europe/Warsaw). It fetches
  the global GFZ Hp60 ensemble, the NOAA 3-day Kp forecast (with its observed
  bins) and the NOAA 27-day outlook once, and stores each source's spans per
  location and displayed night as `NIGHT#…#AURORA#GFZ`, `#NOAA3` and
  `#NOAA27`. A failed source is logged, the others are still stored, and the
  invocation throws at the end. The same run sets the sticky
  `NIGHT#…#AURORA#FLAG` item for a night that may reach the location's aurora
  threshold, which switches the device to hourly refreshes.
- **Aurora nowcast (implemented)**: the `AuroraNowcast` `CronV2` runs every
  ten minutes (`cron(3/10 * * * ? *)`). After dark (sun below −6°) it reads
  NOAA's live Kp estimate and flags a location's night itself when that
  reaches the flag Kp. It returns at once unless a location's current night is
  flagged and it is dark there; then it refreshes GFZ and NOAA 3-day, fetches
  the OVATION grid once, stores the location's percentage in
  `NIGHT#…#AURORA#OVATION` for the slot the nowcast describes (latest and
  maximum), and writes an `aurora-calibration` sample both to the log and as a
  `CALIBRATION#AURORA#<observedAt>` item that never expires. Every run logs one
  line saying, per location, whether it sampled and why.
  See [Aurora Forecast Supplier Evaluation](aurora-forecast-supplier.md).

Each writer is a small, independent Lambda + schedule, matching the existing
SST/Lambda-per-concern style and keeping blast radius small if one upstream API
changes or breaks.

## Technical Stack
- **Infrastructure as Code**: SST v4 (`sst.config.ts`), with Pulumi AWS
  resources for the Route 53 records
- **Runtime**: Node.js / TypeScript
- **Libraries**: `suncalc`, `node-html-parser`, AWS SDK v3 DynamoDB clients
- **Web UI**: React, Vite, React-Bootstrap
- **Cloud Provider**: AWS

## Data Flow
1. Client calls `GET /astro/krakow` over HTTPS.
2. API Gateway triggers the forecast Lambda.
3. The Lambda resolves the `krakow` configuration and its location.
4. The Lambda calculates astronomy and queries the stored weather items for the
   six requested nights.
5. The Lambda returns the versioned six-display text payload. The web UI shows
  that response body verbatim for inspection; the embedded client parses it.

## Web UI

The React/TypeScript web UI lives in `packages/web` and is hosted by an SST
`StaticSite` component backed by S3 and CloudFront. At build time, SST injects
the HTTPS API URL as `VITE_API_URL` and the user pool and app client IDs as
`VITE_USER_POOL_ID` and `VITE_USER_POOL_CLIENT_ID`. The app is wrapped in
Amplify's `Authenticator`, which handles sign-in, account creation with an
emailed confirmation code, and password reset; every API call carries the
signed-in user's access token (see [Access control](#access-control)). The
main view calls
`GET /astro/{configurationId}` and shows the HTTP status, content type, and
response body verbatim; it deliberately does not parse the line protocol.

In deployed stages, the web UI uses an app-specific custom domain. The `prod` stage is
available at `https://astroweather.albedoonline.com`, with the API at
`https://api.astroweather.albedoonline.com`. Other stages use the stage name as
the first label, for example `https://int.astroweather.albedoonline.com` and
`https://api.int.astroweather.albedoonline.com`. SST manages the web
and API certificates and DNS records (see
[API edge](#3-api-edge-api-gateway-custom-domain-https-only)). All records live in the
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

The aurora source tools (`GFZ Hp60`, `NOAA Kp`, `NOAA 27-day`, `OVATION`) are
backed by `POST /tools/<id>` and share one form and one table. They accept a
`configurationId`, or coordinates plus an IANA `timezone`, because every tool
needs the timezone to cut the source into local-noon-to-noon nights even
though three of the four sources are global. The response keeps the source's
own granularity as spans with their real UTC boundaries: hourly for GFZ Hp60,
three-hour bins for the NOAA Kp forecast, UTC days for the 27-day outlook, and
one observation-to-forecast interval for OVATION. A span belongs to every
night it overlaps, so a night built from the 27-day outlook shows the two
consecutive UTC days it straddles, and a three-hour bin crossing local noon
appears in both neighbouring nights. See
[Aurora Forecast Supplier Evaluation](aurora-forecast-supplier.md) for the
sources.

The project structure is:

```text
sst/
├── packages/functions/
│   ├── src/              # Lambda handlers, forecast/, weather/, aurora/, tools/, jobs/
│   └── tests/integration/
├── packages/web/         # Vite + React UI
├── docs/                 # Architecture, API, development, and testing documentation
└── sst.config.ts         # API, DynamoDB, schedules, and StaticSite
```
