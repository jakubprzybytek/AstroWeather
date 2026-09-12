# Clearoutside Scheduled Ingestion Plan

## Goal

Add a scheduled Lambda that fetches Clearoutside forecast data for every location in the shared global configuration, stores normalized nightly records in DynamoDB, and runs every six hours.

Clearoutside remains a supplementary astronomy/weather enrichment source. It has no public API, so the scraper must fail closed and preserve the last good DynamoDB record when a fetch or parse fails.

## Recommended Defaults

- **Configuration source**: `packages/functions/src/configurations.ts`
- **Schedule**: EventBridge/SST `rate(6 hours)`
- **Forecast horizon**: Store all forecast nights returned by Clearoutside, currently seven noon-to-noon blocks
- **DynamoDB partition key**: `PK = LOC#<configurationId>`
- **DynamoDB sort key**: `SK = NIGHT#<nightId>#SKY_CONDITIONS`
- **TTL**: Expire records several days after the forecast night ends
- **Failure handling**: Process locations independently; one failed location must not prevent other locations from being updated
- **Write behavior**: Replace the complete item for a night only after successful fetch and parse
- **Stored payload**: Use the parser's current normalized hourly fields; add richer Clearoutside fields in a separate parser/schema change

## Remaining Production Decision

Before production reliance, contact First Light Optics, the Clearoutside operator, about automated access and caching. The site has no documented API, rate limit, or automation policy, and its HTML structure may change.

The technical implementation can proceed in a staging or research environment using the six-hour schedule.

## Implementation Steps

### 1. Add DynamoDB infrastructure

Update `sst/sst.config.ts`:

- Create an SST DynamoDB table with `pk` and `sk` as the primary key.
- Enable TTL on an `expireAt` attribute.
- Link the table to the scheduled Lambda so it receives table access and the table name.
- Keep the public API route separate from the scheduled writer.

### 2. Add the scheduled Lambda

Create `packages/functions/src/jobs/clearoutside-weather.ts`:

1. Iterate over all entries in `configurations`.
2. Read latitude and longitude from each configuration.
3. Fetch HTML with the existing `fetchClearOutsideHtml` helper.
4. Parse the response with `parseClearOutside`.
5. Write one DynamoDB item per returned night.
6. Log failures with the configuration identifier and continue processing other locations.

The handler should fail only for an invocation-level problem, not for an individual location scrape failure.

### 3. Define the DynamoDB item

```typescript
{
  pk: "LOC#krakow-home",
  sk: "NIGHT#2026-09-11#SKY_CONDITIONS",
  configurationId: "krakow-home",
  nightId: "2026-09-11",
  service: "skyConditions",
  coordinates: {
    latitude: 50.0647,
    longitude: 19.945
  },
  hours: [...],
  fetchedAt: "2026-09-12T06:00:00.000Z",
  expireAt: 179...
}
```

The `hours` array uses the existing normalized shape:

- `hour`
- `timestampUtc`
- `temperatureC`
- `cloudCoverTotalPct`
- `precipitationProbabilityPct`
- `thunderstormRisk`

### 4. Add persistence code

Create a small storage module, for example `packages/functions/src/weather/clearoutside-storage.ts`, to:

- Build deterministic partition and sort keys.
- Calculate `expireAt`.
- Write complete nightly items with DynamoDB `PutItem`/Document Client operations.
- Keep AWS SDK details out of the scraping and orchestration code.

Add the required DynamoDB AWS SDK packages to `sst/package.json` if they are not already available in the Lambda runtime bundle.

### 5. Harden fetching

Update the existing Clearoutside fetch helper as needed:

- Add an `AbortController` timeout.
- Preserve fail-closed parsing when required rows or hourly values are missing.
- Use only a small retry policy for transient failures, avoiding aggressive requests to the upstream site.

### 6. Add the six-hour schedule

Define an SST Cron resource in `sst/sst.config.ts` using `rate(6 hours)` and connect it to the new Lambda. The schedule should invoke the same handler that can also be run manually for verification.

### 7. Add tests

Add co-located unit tests covering:

- Every configured location is fetched.
- Each parsed night maps to the expected DynamoDB key.
- Successful writes contain coordinates, timestamps, hourly data, and TTL.
- A failed fetch or parse does not overwrite the previous record.
- A failed location does not prevent other locations from being processed.

Mock the fetch helper and DynamoDB client. Reuse the existing Clearoutside HTML fixture and parser tests.

### 8. Update documentation

Update `sst/docs/architecture.md` with:

- The scheduled Clearoutside writer.
- The DynamoDB table and item shape.
- The six-hour schedule.
- The independent per-location failure behavior.

Update `sst/docs/testing.md` with the scheduled-handler test strategy and manual deployed-stage verification steps.

## Validation

1. Run the unit test suite.
2. Run TypeScript/SST validation.
3. Deploy to a non-production stage.
4. Invoke the scheduled Lambda manually or wait for its first schedule.
5. Query DynamoDB and verify records for each configured location and forecast night.
6. Confirm that a failed scrape leaves the previous successful item unchanged.
7. Inspect CloudWatch logs for per-location success and failure information.
