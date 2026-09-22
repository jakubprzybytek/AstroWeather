# Main Forecast API

## Story

As an AstroWeather client, I want to request the forecast for a configured
location so that I can display its weather and astronomical events together for
the next six observing nights.

The endpoint presents one location-centric view. Clients should not need to
know which data is calculated on demand, which data is stored, or which weather
supplier produced it.

## Scope

For a valid `configurationId`, the main endpoint returns:

- the configuration identifier;
- the local date and time at which the response was rendered, which the
  device uses to synchronize its real-time clock;
- six observing nights, starting with the current night in the configuration's
  local timezone;
- local sunset and next-day sunrise times for each night;
- fixed-size hourly sun, moon, cloud, and thunderstorm state matrices for each
   night;
- the supported display board identifier for each night;
- available maximum and minimum temperature for each night;

An observing night is the local-noon-to-local-noon window already used by
weather ingestion. Its `nightId` is the local calendar date on which the window
starts. For example, `nightId = 2026-09-17` covers local noon on September 17
through local noon on September 18.

## Endpoint

```http
GET /astro/{configurationId}
```

This is the only main forecast endpoint. It is served on the public API
hostname over both HTTP and HTTPS without redirects; see the API edge section
in [architecture.md](architecture.md) for the security constraints of plain
HTTP.

## Response Format

The endpoint is consumed by an STM32-based device without a JSON parser. A
successful request returns `text/plain; charset=utf-8` using the versioned ASCII
`key=value` protocol defined in [api-payload.md](api-payload.md).

The response contains the `protocol=1`, `configurationId`, and `time` header
records, followed by six fixed-order display blocks. `time` is the
configuration's local wall-clock time as `YYYY-MM-DDTHH:MM:SS`. Each block contains, in order,
`display`, `board`, `nightId`, `numeric_0`, `numeric_1`, `matrix_0` through
`matrix_3`, `numeric_2`, and `numeric_3`. `numeric_0` and `numeric_1`
are sunset and sunrise; `matrix_0` through `matrix_3` are sun, moon, cloud,
and thunderstorm state; and `numeric_2` and `numeric_3` are maximum and
minimum temperature. `board` is `num4x4_matrix5x21` in version 1.

Records remain present when a source is missing or fails and use the payload
sentinel `?` for all unavailable times, weather values, and matrix slots. There is no
`displayCount`, `matrix_4`, or end marker in version 1. The wire
format has one empty row between display blocks and no comments.

Error responses use the same line protocol and stable machine-readable error
identifiers. HTTP status codes remain authoritative.

## Behavior

### Forecast window

The handler determines the current `nightId` using the configuration's timezone
and the local-noon boundary. It returns that night plus the following five
nights in ascending `nightId` order.

The six-night window is stable around UTC midnight because it is derived from
the location's local time. Tests should inject the current time so the boundary
before and after local noon is deterministic.

### Response time

The `time` header record is the configuration's local wall-clock time, read
from the server clock after the forecast has been assembled so it is as close
as possible to the moment the response is sent. It uses the same IANA timezone
and DST rules as the forecast window, has no UTC offset, and truncates to whole
seconds. The embedded device uses it to set its real-time clock.

### Astronomy

Astronomical events are calculated for each night from the configured latitude
and longitude. Sunset belongs to the date that starts the night, and sunrise
belongs to the following local date. Moonrise and moonset times are not
emitted; the moon is represented only by its hourly above-horizon matrix.

Sunset and sunrise are serialized as local `HH:MM` values. An event that does
not occur is represented by `?`. Sun and moon state are sampled for the
21 local-hour slots: 14:00 through 10:00 on the following date. The
11:00-12:00 interval is not displayed. Sun and moon state is sampled at each
slot midpoint. The server emits the same 21 wall-clock slots across DST
transitions and maps each local-hour midpoint to the appropriate instant.

`matrix_2` is on when total cloud coverage is at least 10 percent, and
`matrix_3` is on when a thunderstorm is predicted. An available matrix contains
exactly 21 characters, each `*`, `.`, or `?`; a matrix row with no available
source data is represented by the single `?` character. Within an available
row, `?` represents an unavailable individual slot.

### Weather

Weather is read from the `ForecastData` DynamoDB table using the location
partition and requested night range:

```text
PK = LOC#<configurationId>
SK = NIGHT#<nightId>#WEATHER
```

Only `#WEATHER` records are part of the main API.

DynamoDB TTL removal is asynchronous. The reader must exclude an item when
`expireAt` is at or before the request time, even if DynamoDB has not deleted it
yet. Temperature extrema are calculated only from hourly values within the
requested observing night. The maximum is serialized in `numeric_2` and the
minimum in `numeric_3`, each with one decimal place.

### Graceful degradation

Astronomy and weather are assembled independently. Missing, expired, or failed
weather retrieval must not prevent available astronomy from being returned.
The affected weather matrix slots and weather-derived numeric values use `?`.

Likewise, an astronomy calculation failure must not prevent available weather
from being returned. The affected sunset and sunrise values and sun or moon
matrix slots use `?`.

The endpoint always returns all six display blocks with the complete fixed
record shape. It returns `500` only when it cannot assemble a trustworthy
protocol response at all; a failure isolated to astronomy or weather returns
`200` with sentinels only for the unavailable fields. Failures are logged with
their source and affected nights.

### Rate limiting and caching

API Gateway limits the API to a burst of one request and a steady-state rate of
one request per second. Requests beyond these limits are throttled by API
Gateway. The API does not currently emit cache headers or use response caching.

## Errors

| Status | Body | When |
|---|---|---|
| `200` | Forecast response | The configuration exists, including when astronomy or weather is partly unavailable |
| `404` | `error=configuration_not_found` response | The identifier is missing or unknown |
| `500` | `error=forecast_unavailable` response | No trustworthy protocol response can be assembled |

The API does not expose upstream errors, AWS details, or stack traces to the
client. Operational details belong in structured Lambda logs.

## Acceptance Criteria

1. A request for a known configuration returns `200`, content type `text/plain`,
   protocol version 1, and exactly six display blocks in ascending order.
2. The first night is selected using the location's timezone and local-noon
   boundary; the next five night identifiers are consecutive local dates.
3. Every display block contains the complete, fixed-order record set defined in
   `api-payload.md`; each matrix is either `?` or exactly 21 valid characters.
4. Available, unexpired `#WEATHER` records supply the matching nights' minimum
   and maximum temperatures.
5. Expired, out-of-range, and other service records are not returned as weather.
6. Missing or failed weather does not remove a display block or available
   astronomy; its numeric values and unavailable matrix rows or slots use
   `?`.
7. An unknown configuration returns the versioned text error payload with
   status `404`.
8. Failed astronomy does not remove a display block or available weather; its
   times and unavailable sun or moon matrix rows or slots use `?`.
9. Unit tests cover the local-noon boundary, six-night range, item grouping,
   expired-item filtering, independent astronomy and weather failures, matrix
   encoding, sentinels, and serialization order.
10. An integration test verifies the deployed route for one known and one
   unknown configuration.
11. A successful response carries `time` as the configuration's local
   `YYYY-MM-DDTHH:MM:SS` at render time, including after a DST change.

## Non-goals

- Backfilling or migrating old DynamoDB records.
- Fetching Clear Outside during an API request.
- Configuration CRUD or accepting arbitrary coordinates.
- Adding aurora or additional forecast services in this story.
- Guaranteeing that weather exists for every requested night.

## Clients

- **Embedded device**: parses the line protocol as described in
  [api-payload.md](api-payload.md).
- **Web UI**: a protocol inspection tool. It shows the HTTP status, content
  type, and response body verbatim, and deliberately does not parse the line
  protocol.