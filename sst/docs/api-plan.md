# Main Forecast API Implementation Plan

## Goal

Replace the current one-day JSON response from `GET /astro/{configurationId}`
with the version 1 line protocol defined in [api.md](api.md) and
[api-payload.md](api-payload.md). The endpoint will assemble six observing
nights from independently calculated astronomy and stored Clear Outside weather,
degrade failed sources to `?`, and continue to serve the existing web client by
updating it to display the raw HTTP response in the same release.

## Current State

- `packages/functions/src/astro.ts` calculates one day's SunCalc events using
  the current instant and returns JSON.
- `ForecastData` already stores normalized `#WEATHER` items with hourly
  temperature, total cloud cover, and thunderstorm fields.
- The astro Lambda is not linked to `ForecastData`; only the scheduled writer is.
- There are no unit tests for the astro handler or forecast assembly.
- The web client parses the route as JSON and renders one set of sun/moon events.
- The deployed astro integration test asserts the old JSON contract.

## Design

Keep the Lambda handler thin and place time-window calculation, astronomy,
weather projection, and serialization behind small testable functions. Inject
the clock and external dependencies into the assembler so local-noon and failure
tests are deterministic without mocking global time or AWS.

Use one shared forecast model internally:

```typescript
type MatrixValue = string; // "?" or exactly 21 characters from "*", ".", "?"

type ForecastDisplay = {
  display: number;
  board: "num4x4_matrix5x21";
  nightId: string;
  sunset: string;
  sunrise: string;
  sun: MatrixValue;
  moon: MatrixValue;
  cloud: MatrixValue;
  thunderstorm: MatrixValue;
  maximumTemperature: string;
  minimumTemperature: string;
};
```

The assembler always creates all six displays first with sentinel values, then
merges astronomy and weather independently. Serialization is the final pure
step and validates the complete model before emitting any response body.

## Implementation Steps

### 1. Add timezone-aware observing-night utilities

- Use the runtime's `Intl.DateTimeFormat` with IANA zones for wall-clock-to-instant
  conversion and DST handling, avoiding hand-written fixed-offset arithmetic or
  an additional runtime dependency.
- Add a focused module under `packages/functions/src/forecast/` that:
  - derives the first `nightId` from the injected request instant and configured
    IANA timezone, moving to the previous local date before 12:00;
  - returns six consecutive local calendar dates;
  - creates the 21 wall-clock slots from 14:00 on `nightId` through 10:00 on the
    following date;
  - maps each slot midpoint to an instant in the configured timezone, using
    Temporal's `compatible` DST disambiguation consistently for skipped or
    repeated local times.
- Unit-test before noon, exactly noon, after noon, UTC/local-midnight separation,
  month/year rollover, and spring/fall DST transitions. Assert that every night
  still has 21 labeled slots.

### 2. Implement astronomy projection

- Add a pure astronomy adapter that accepts a night, location, timezone, and its
  slot instants.
- Calculate sunset for the local date starting the night and sunrise for the
  following local date. Convert valid events to local `HH:MM`; use `?` when
  SunCalc reports an invalid or absent event.
- Sample `SunCalc.getPosition` and `SunCalc.getMoonPosition` at each slot
  midpoint. Encode altitude above the horizon as `*`, altitude at or below the
  horizon as `.`, and a failed/unavailable sample as `?`.
- Keep failures scoped as narrowly as possible: one failed night must not erase
  other nights, and one invalid event must not erase valid matrices.
- Unit-test event date selection, local time formatting, polar/no-event
  sentinels, 21-character matrix order, midpoint transitions, and isolated
  sample/night failures.

### 3. Add the ForecastData weather reader and projection

- Add a `ForecastWeatherReader` interface and DynamoDB implementation using
  `DynamoDBDocumentClient` and `QueryCommand`.
- Query `pk = LOC#<configurationId>` across the requested `NIGHT#<date>` sort-key
  range. Handle pagination, then retain only items whose sort key exactly matches
  `NIGHT#<nightId>#WEATHER` for one of the six requested nights.
- Validate item shape at the reader boundary. Ignore malformed, duplicate,
  out-of-range, and non-weather service items and log enough context to diagnose
  them without exposing it to the client.
- Compare `expireAt` with the injected request time in epoch seconds and discard
  items where `expireAt <= now`, regardless of DynamoDB TTL deletion status.
- For each display slot, match normalized hourly data by its UTC timestamp mapped
  into the configuration timezone and corresponding wall-clock slot. Emit:
  - cloud `*` for coverage at least 10 percent, `.` below 10 percent, and `?` for
    a missing hour/value;
  - thunderstorm `*` for `true`, `.` for `false`, and `?` for `null` or a missing
    hour;
  - maximum and minimum from available `temperatureC` values whose timestamps
    fall inside that observing-night window, formatted with one decimal place.
- Emit a single `?` for a matrix row when no source slots are available; otherwise
  emit all 21 characters, preserving per-slot `?` values. Emit `?` for both
  extrema when no valid in-window temperatures exist.
- Unit-test query keys and pagination, night grouping, exact service filtering,
  expiry boundary, malformed/duplicate records, threshold behavior, sparse
  slots, extrema range filtering, decimal formatting, and reader failure.

### 4. Build and serialize the six-night forecast

- Add an assembler that resolves the six-night window and invokes astronomy per
  night and weather once for the range.
- Initialize each display with the fixed board identifier and sentinel fields.
  Catch and log astronomy and weather failures independently, including source
  and affected night identifiers, while preserving data from the other source.
- Add a serializer that emits exactly this order with LF endings and one empty
  row between display blocks: `protocol`, `configurationId`, then for displays 0 through 5 `display`,
  `board`, `nightId`, `numerical_0`, `numerical_1`, `matrix_0` through `matrix_3`,
  `numerical_2`, and `numerical_3`.
- Reject invalid identifiers, non-ASCII values, non-consecutive displays,
  malformed dates/times/numbers, unsupported board values, and invalid matrix
  lengths or characters before serialization. Do not emit `displayCount`,
  `matrix_4`, comments, or an end marker.
- Serialize errors through one helper as:

  ```text
  protocol=1
  error=<stable_identifier>
  ```

- Unit-test exact full-body snapshots, record count/order, LF framing, ASCII-only
  output, all-sentinel displays, mixed slot sentinels, and validation failures.

### 5. Replace the Lambda handler and wire infrastructure

- Refactor `packages/functions/src/astro.ts` into a thin API Gateway adapter:
  - return `404` with `error=configuration_not_found` for a missing or unknown
    identifier;
  - assemble the forecast for a known configuration;
  - return `200` for isolated astronomy or weather failures because the assembler
    provides sentinels;
  - return `500` with `error=forecast_unavailable` only when input/model
    validation or serialization prevents a trustworthy protocol response.
- Set `content-type` to `text/plain; charset=utf-8` for success and handled
  errors. Do not expose exception messages, AWS details, or stack traces.
- In `sst.config.ts`, link `forecastData` to the astro route's function while
  retaining the existing API Gateway burst and rate limits.
- Add handler tests for status, content type, dependency invocation, source
  degradation, safe logging, and both stable error payloads.

### 6. Migrate the web client in the same release

- Remove the old `AstroResponse` JSON model from the main forecast flow. The web
  client is a protocol inspection tool and must not parse the line protocol into
  forecast or display types.
- Change `fetchAstro` to read `response.text()` exactly once and return both the
  HTTP status and unmodified body. Do this for successful and non-successful
  responses so the page can show exactly what other HTTP clients receive.
- Update `AstroResults` to display the raw body in a whitespace-preserving
  monospace `<pre>` element. Do not split records, reorder fields, translate
  errors, replace `?`, normalize line endings, trim the body, or prettify values.
- Show the HTTP status and response content type separately from the body so they
  can be inspected without altering the payload text.
- Keep transport failures, where no HTTP response body exists, in the existing
  UI error state. Do not substitute a UI message for an HTTP error response body.
- Update web tests to assert that complete, sentinel-heavy, malformed, truncated,
  and non-2xx bodies are rendered byte-for-byte as supplied by mocked responses.

### 7. Update integration coverage and documentation

- Rewrite `packages/functions/tests/integration/astro.integration.test.ts` to
  read text and verify:
  - a known configuration returns `200`, the exact text content type, protocol 1,
    six consecutive displays, fixed record order, valid dates/times/matrices,
    and the supported board;
  - an unknown configuration returns `404` and the exact versioned error body;
  - a missing path parameter remains non-200 at the gateway level.
- Avoid asserting that deployed weather is always present; validate either the
  allowed sentinel or correctly formatted available data.
- Update `docs/architecture.md` and `docs/testing.md` to remove the old JSON,
  one-day, and generic multi-service response descriptions and document the
  raw web response view, table access, and focused test commands.

## Recommended File Layout

```text
packages/functions/src/
├── astro.ts
└── forecast/
    ├── astronomy.ts
    ├── astronomy.test.ts
    ├── assemble.ts
    ├── assemble.test.ts
    ├── nights.ts
    ├── nights.test.ts
    ├── protocol.ts
    ├── protocol.test.ts
    ├── weather-reader.ts
    └── weather-reader.test.ts

packages/web/src/
├── api.ts
└── components/AstroResults.tsx
```

The exact split may be reduced if adjacent modules remain small, but astronomy,
AWS access, and protocol serialization should remain separate ownership
boundaries. The browser deliberately has no protocol parsing boundary.

## Validation Sequence

Run the cheapest check after each implementation step rather than waiting for
the complete migration:

1. `npx vitest run --project unit packages/functions/src/forecast/nights.test.ts`
2. `npx vitest run --project unit packages/functions/src/forecast`
3. `npm run test:unit`
4. `npm run test:web`
5. `npm run test:all`
6. Start `sst dev` and run `npm run test:integration` against the local route.
7. Deploy handler and web together to a non-production stage and run
   `API_URL=https://<stage-api> npm run test:integration`.
8. Inspect one known response byte-for-byte and confirm display separators,
   CR characters, JSON headers, omitted fields, or extra protocol records.

## Delivery Order and Rollback

Deliver the backend, infrastructure link, raw web response view, tests, and docs as one
coordinated change because the route has no compatibility period. Deploy to a
non-production stage first, then deploy the Lambda and static site together to
production. If validation fails, roll both artifacts back to the previous
release; do not add a temporary `/forecast` route or dual JSON/text behavior.

## Completion Criteria

Implementation is complete when all acceptance criteria in [api.md](api.md) are
covered by automated tests, the known and unknown deployed-route checks pass,
the web client displays each HTTP response body without parsing or modification,
and a six-display response remains well-formed when either astronomy or DynamoDB
is unavailable.