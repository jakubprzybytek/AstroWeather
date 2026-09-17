# Clearoutside Scheduled Ingestion Plan

## Goal

Add a scheduled Lambda that fetches and parses Clearoutside data for every
configured location every six hours, then stores one normalized DynamoDB item
per forecast night. A failed fetch or parse must never replace previously
stored good data.

Clearoutside remains a supplementary source. It has no public API or published
automation policy, so production use still requires agreement from First Light
Optics about automated access and caching.

## Scope

This change includes the DynamoDB table, six-hour scheduler, ingestion Lambda,
storage module, bounded fetch behavior, unit tests, and deployed-stage
verification documentation.

It does not include reading the stored records from `GET /astro`, changing the
existing `POST /tools/clearoutside` response, or expanding the parser with new
Clearoutside fields. Those are separate follow-up changes.

## Decisions

| Concern | Decision |
|---|---|
| Configuration source | Iterate `packages/functions/src/configurations.ts` |
| Schedule | SST `CronV2` with `rate(6 hours)` |
| Request order | Process locations sequentially with a 5-second gap between locations |
| Forecast horizon | Store every complete parsed night, currently seven |
| Table key | `pk = LOC#<configurationId>`, `sk = NIGHT#<nightId>#WEATHER` |
| Write model | One complete `Put` per night; deterministic keys make retries idempotent |
| TTL | `expireAt` is 72 hours after the final hourly timestamp in the item |
| Fetch/parse failure | Write nothing for that location and retain previous items |
| Write failure | Continue other locations, then fail the invocation after all attempts |
| Schedule retries | One EventBridge Scheduler retry |
| Stored schema | Persist the parser's current normalized fields without raw HTML |

The handler logs one structured result per location and a final summary. It
processes every location even when one fails, then throws an aggregate error if
there were failures. This gives the scheduler and monitoring a failure signal
without preventing successful locations from being updated. Retried writes are
safe because every item has a deterministic key.

## Remaining Production Decision

Before production reliance, contact First Light Optics, the Clearoutside operator, about automated access and caching. The site has no documented API, rate limit, or automation policy, and its HTML structure may change.

The technical implementation can proceed in a staging or research environment using the six-hour schedule.

## Implementation Steps

### 1. Add AWS SDK dependencies

Add direct runtime dependencies to the root `package.json`:

- `@aws-sdk/client-dynamodb`
- `@aws-sdk/lib-dynamodb`

Do not rely on packages pulled transitively by SST. Use
`DynamoDBDocumentClient` and `PutCommand` so the repository works with ordinary
JavaScript objects.

### 2. Add DynamoDB infrastructure

Update `sst.config.ts` before defining the scheduled job:

```typescript
const forecastData = new sst.aws.Dynamo("ForecastData", {
  fields: {
    pk: "string",
    sk: "string"
  },
  primaryIndex: { hashKey: "pk", rangeKey: "sk" },
  ttl: "expireAt"
});
```

Return `forecastData.name` in the stack outputs to simplify deployed-stage
inspection. Do not add a GSI yet; the writer and planned location/night API
query only require the primary index.

### 3. Add the persistence module

Create `packages/functions/src/weather/clearoutside-storage.ts` with:

- a `ClearOutsideItem` type;
- a pure `toClearOutsideItem(...)` mapper that builds keys, metadata, and TTL;
- a narrow `ClearOutsideStore` interface with `putNight(item)`;
- a production implementation backed by `DynamoDBDocumentClient` and
  `Resource.ForecastData.name`.

Use separate `PutCommand` calls instead of `BatchWriteCommand`. There are few
records, and individual puts avoid unprocessed-item retry handling while
retaining deterministic, idempotent writes.

### 4. Add the ingestion service and Lambda handler

Create `packages/functions/src/jobs/clearoutside-weather.ts`:

```typescript
export async function ingestClearOutside(dependencies = productionDependencies) {
  // Fetch, parse, map, and store all configured locations.
}

export const handler = async () => ingestClearOutside();
```

Inject the configuration collection, fetch function, parser, store, and clock.
This keeps unit tests independent of SST resource bindings, DynamoDB, the
network, and wall-clock time.

For each configuration, in sequence:

1. Record one `fetchedAt` value for the location attempt.
2. Fetch HTML using the existing `fetchClearOutsideHtml` helper.
3. Parse the entire response with `parseClearOutside` before writing anything.
4. Reject an empty parsed forecast.
5. Map and write each complete night using the deterministic key.
6. Log `{ configurationId, nightCount, fetchedAt, status: "success" }`.
7. On failure, log `{ configurationId, status: "failed", error }`, retain
   previous records, and continue with the next configuration.

After all configurations are attempted, log success and failure counts, then
throw if any location failed. Parsing before the first write guarantees that an
HTML shape change cannot partially replace a location's cached forecast. A
mid-write DynamoDB failure can still leave a mix of new and previous nights;
this is acceptable for independent nightly records and is repaired by the
scheduler retry or next run.

### 5. Define the DynamoDB item

```typescript
type ClearOutsideItem = {
  pk: `LOC#${string}`;
  sk: `NIGHT#${string}#WEATHER`;
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
```

`expireAt` is integer epoch seconds calculated from the greatest valid
`timestampUtc` in `hours`, plus 72 hours. Reject an empty night or invalid
timestamp rather than creating an item with an undefined retention period.
DynamoDB TTL deletion is eventual, so readers must still filter expired items.

The `hours` array uses the existing normalized shape:

- `hour`
- `timestampUtc`
- `temperatureC`
- `cloudCoverTotalPct`
- `precipitationProbabilityPct`
- `thunderstormRisk`

### 6. Harden fetching

Update `fetchClearOutsideHtml` in
`packages/functions/src/weather/clearoutside.ts`:

- Abort requests after 15 seconds.
- Retry at most once for network failures and HTTP `5xx`, using bounded
  exponential backoff with jitter.
- Do not immediately retry HTTP `429`; include the upstream `Retry-After`
  value in the failure and defer the next attempt to the scheduled run.
- Do not retry other `4xx` responses.
- Retain the identifying `User-Agent` header.
- Wait five seconds between configured locations, including after a failed
  location, to avoid making a burst of requests from one Lambda invocation.
- Preserve fail-closed parsing when required rows or hourly values are missing.

### 7. Add the six-hour schedule

Use the current SST scheduler component in `sst.config.ts`:

```typescript
new sst.aws.CronV2("ClearOutsideIngestion", {
  schedule: "rate(6 hours)",
  retries: 1,
  function: {
    handler: "packages/functions/src/jobs/clearoutside-weather.handler",
    link: [forecastData],
    timeout: "2 minutes"
  }
});
```

The linked table supplies `Resource.ForecastData.name` and grants table access.
The two-minute timeout covers three sequential locations with one bounded fetch
retry each while still limiting hung work.

### 8. Add focused unit tests

Create `packages/functions/src/weather/clearoutside-storage.test.ts` for the
pure mapper:

- builds exact partition and sort keys;
- preserves coordinates and normalized hours;
- uses the injected fetch timestamp;
- calculates integer epoch-seconds TTL from the latest hourly timestamp;
- rejects empty hours and invalid timestamps.

Create `packages/functions/src/jobs/clearoutside-weather.test.ts` with injected
fakes:

- fetches all configured locations sequentially;
- writes every parsed night with expected configuration and coordinates;
- uses one `fetchedAt` value per location attempt;
- performs no writes when fetching or parsing fails;
- continues to later locations after one location fails;
- throws only after all locations are attempted when any location fails;
- returns a summary when all locations are stored;
- rejects an empty parsed forecast without overwriting data.

Extend `packages/functions/src/weather/clearoutside.test.ts` with fake-timer and
mocked-fetch cases for timeout, retryable responses, and non-retryable `4xx`
responses. Keep the saved HTML fixture as the parser regression test.

### 9. Update documentation

Update `docs/architecture.md` with:

- the concrete Clear Outside weather service;
- the DynamoDB item shape;
- the six-hour writer;
- per-location failure isolation.

Update `docs/testing.md` with the new unit tests, manual Lambda invocation,
DynamoDB inspection, and expected CloudWatch log fields.

## Implementation Order and Validation

1. Install the two AWS SDK dependencies.
2. Add the pure item mapper and run its test file.
3. Add the injectable ingestion service and run its test file.
4. Harden the fetch helper and run all Clearoutside unit tests.
5. Add `ForecastData` and `ClearOutsideIngestion` to `sst.config.ts`; run the
   TypeScript/SST config check and full unit suite.
6. Update architecture and testing documentation.
7. Deploy to the `int` stage, manually invoke the generated Lambda once, and
   query DynamoDB for all three configured locations.

## Acceptance Criteria

- The deployed schedule is enabled and runs every six hours.
- One invocation attempts `wroclaw` and `krakow`.
- Every valid parsed night is stored under its deterministic key.
- Re-running the handler updates existing keys instead of creating duplicates.
- A fetch or parse failure produces no writes for that location and does not
  prevent attempts for the remaining locations.
- Any failed location fails the final invocation after all locations have been
  attempted, enabling the scheduler retry.
- Stored items contain normalized data only, have numeric TTL values, and do
  not contain source HTML.
- Existing `POST /tools/clearoutside` behavior remains unchanged.
- Unit tests and TypeScript checks pass, and the `int` table contains records
  for all configured locations after manual invocation.
