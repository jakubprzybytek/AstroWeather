# Testing Approach

## Tech Stack

| Concern | Tool |
|---|---|
| Test runner | [Vitest](https://vitest.dev/) v4 |
| Language | TypeScript (native ESM, no transpilation step) |
| Mocking | Dependency injection first; Vitest built-ins (`vi.fn`, `vi.mock`) where needed |
| Web components | React Testing Library with jsdom |

Vitest was chosen because it supports native ESM and TypeScript out of the box, shares the same API surface as Jest, and integrates well with a monorepo project structure through its **projects** feature.

## Vitest Projects

`vitest.config.ts` declares three named projects. The `--project` flag selects which suite to run:

| npm script | Vitest project | Include pattern | Target environment |
|---|---|---|---|
| `npm run test` / `npm run test:unit` | `unit` | `packages/**/src/**/*.test.ts` | Node, no network or AWS |
| `npm run test:integration` | `integration` | `packages/**/tests/integration/**/*.test.ts` | A deployed or `sst dev` stage, through `sst shell` |
| `npm run test:web` | `web` | `packages/web/src/**/*.test.tsx` | jsdom |
| `npm run test:all` | _(all)_ | all patterns above | — |

`test:all` includes the integration project, so it needs the same SST
credentials and stage as `test:integration`.

## Unit Tests

Unit tests exercise individual functions and modules in isolation. They are
co-located with source files as `*.test.ts`. Plain `.test.ts` files under
`packages/web/src` (for example `time.test.ts`) also run in the `unit` project.

```bash
npm run test
```

Current coverage:

| Test file | Covers |
|---|---|
| `forecast/nights.test.ts` | Local-noon boundary, six consecutive nights, 21 slots, DST transitions |
| `forecast/assemble.test.ts` | Six displays and independent astronomy/weather degradation |
| `forecast/protocol.test.ts` | Serialized record order, `time` header format, and error payloads |
| `astro.test.ts` | Forecast handler: local `time` header (including after DST ends), status, content type, `404` error |
| `configurations-handler.test.ts` | `GET /configurations` response |
| `clearoutside.test.ts` | `POST /tools/clearoutside` handler |
| `weather/clearoutside.test.ts` | HTML parser against a saved fixture; fetch timeout and retry behavior |
| `weather/clearoutside-storage.test.ts` | DynamoDB keys, metadata, and `expireAt` calculation |
| `jobs/clearoutside-weather.test.ts` | Scheduled ingestion: sequential locations, failure isolation, final failure signal |

The forecast weather reader (`forecast/weather-reader.ts`) and astronomy
projection (`forecast/astronomy.ts`) have no dedicated unit tests yet. Astronomy is exercised indirectly through `assemble.test.ts`.

Run a focused subset by passing paths:

```bash
npx vitest run --project unit packages/functions/src/forecast
```

### Writing unit tests

Modules take their external dependencies as parameters, so most tests pass
plain fakes instead of mocking modules. The forecast assembler receives the
clock, weather reader, and logger:

```typescript
import { describe, expect, test, vi } from "vitest";
import { assembleForecast } from "./assemble";

describe("assembleForecast", () => {
  test("keeps six displays and available astronomy when weather fails", async () => {
    const result = await assembleForecast("krakow", { lat: 50, lon: 20, tz: "Europe/Warsaw" }, {
      now: () => new Date("2026-09-17T10:00:00Z"),
      readWeather: vi.fn().mockRejectedValue(new Error("Dynamo unavailable")),
      log: vi.fn()
    });

    expect(result).toHaveLength(6);
    expect(result[0].cloud).toBe("?");
  });
});
```

The same pattern applies to the other injectable entry points:

- `createHandler({ now, readWeather, log })` in `astro.ts`;
- `createWeatherReader(tableName, client)` and `createClearOutsideStore(tableName, client)`,
  which accept a DynamoDB document client;
- `ingestClearOutside({ configurations, fetchHtml, parse, store, now, waitBetweenLocations, log })`.

Astronomy tests run the real `suncalc` library, which is deterministic for an
injected time. Use `vi.mock` only for modules that are imported directly and
have no injection point, as `clearoutside.test.ts` does for the scraper.

## Integration Tests

Integration tests call a deployed API over HTTP. They live in
`packages/<pkg>/tests/integration/` rather than next to the source.

`npm run test:integration` wraps Vitest in `sst shell`, which makes the stage's
linked resources available. `globalSetup.ts` reads `Resource.AstroApi.url`, the
generated API Gateway URL of the selected stage, prints it, and hands it to the
test files through Vitest's `provide`/`inject`. Test files must use
`inject("apiUrl")` rather than `Resource`: on Windows, Vitest workers receive
environment variable names upper-cased, which hides the SST links from
`Resource` inside the worker.

`sst shell` selects the stage from `SST_STAGE`, so deploy the stage first (see
[development.md](development.md)) and then run:

```bash
# Your personal stage, deployed or running under `sst dev`
npm run test:integration

# A named stage
SST_STAGE=int npm run test:integration
```

The suite covers:

- `GET /astro/krakow` returns `200`, `text/plain`, protocol 1, a `time` header
  within the Krakow UTC offset of the test machine's clock, and six well-formed
  display blocks;
- an unknown configuration returns `404` with the versioned error body;
- a missing path parameter is not `200`;
- `GET /configurations` returns the configuration list.

Because the tests call the generated API Gateway URL, they do not exercise the
public CloudFront hostname, its certificate, or plain-HTTP access. Check those
manually after infrastructure changes, with redirects disabled:

```bash
curl -si http://api.<stage>.astroweather.albedoonline.com/configurations
curl -si https://api.<stage>.astroweather.albedoonline.com/astro/krakow
```

Both should return the final API response with no `Location` header.

### Live Clear Outside scrape

`clearoutside.integration.test.ts` fetches and parses the real Clear Outside
page for Wrocław and prints three nights of hourly data. It is skipped unless
`RUN_LIVE_SCRAPE=1`, which the dedicated script sets:

```bash
npm run test:clearoutside
```

Use it to check whether a Clear Outside page change has broken the parser.

## Web Tests

The web project uses React Testing Library with jsdom. API requests are stubbed
with Vitest, so component tests do not require SST or a running API:

```bash
npm run test:web
```

## Scheduled Clearoutside Ingestion

The scheduled writer is unit-tested without AWS or network access (see above).
The deployed `ClearOutsideIngestion` schedule runs every six hours without
scheduler retries. For a manual staging check, identify the generated Lambda
from the SST deployment output or AWS Console, invoke it with `{}`, and inspect
CloudWatch for one structured success or failure record per configuration
followed by the completion summary.

The table name is returned as the `forecastDataTableName` stack output. Query
one location with:

```bash
aws dynamodb query \
  --table-name <forecastDataTableName> \
  --key-condition-expression "pk = :pk" \
  --expression-attribute-values '{":pk":{"S":"LOC#krakow"}}'
```

Verify that returned items use the `NIGHT#<nightId>#WEATHER` sort-key
format, contain numeric `expireAt` values and normalized `hours`, and contain no
raw HTML. A failed scrape should leave the previous item unchanged.

## CI Usage

| Pipeline stage | Command | Notes |
|---|---|---|
| Pull Request | `npm run test` and `npm run test:web` | No AWS or network access required |
| Post-deploy | `SST_STAGE=<stage> npm run test:integration` | After `npm run deploy -- --stage <stage>`; needs AWS credentials that can read the stage's SST resources |
