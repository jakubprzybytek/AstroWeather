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
- six observing nights, starting with the current night in the configuration's
  local timezone;
- available minimum and maximum temperature for each night;
- local sunset and next-day sunrise times for each night;
- fixed-size hourly sun and moon state matrices for each night.

An observing night is the local-noon-to-local-noon window already used by
weather ingestion. Its `nightId` is the local calendar date on which the window
starts. For example, `nightId = 2026-09-17` covers local noon on September 17
through local noon on September 18.

## Endpoint

```http
GET /astro/{configurationId}
```

This is the only main forecast endpoint. It expands the existing
`GET /astro/{configurationId}` response from one day's astronomy data into the
six-night response described below. No `/forecast` endpoint should be added,
and clients do not need a migration period or a second route.

## Response Format

The endpoint is consumed by an STM32-based device without a JSON parser. A
successful request returns `text/plain; charset=utf-8` using the versioned ASCII
`key=value` protocol defined in [api-payload.txt](api-payload.txt).

The response contains a short header followed by six fixed-order display
blocks. Each block contains the local `nightId`, sunset and sunrise times,
fixed-size sun and moon matrices, and the available weather summary. Required
records remain present when source data is missing and use documented sentinel
values, allowing firmware to parse the response without dynamic allocation or
a general-purpose document parser.

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

### Astronomy

Astronomical events are calculated for each night from the configured latitude
and longitude. Sunset belongs to the date that starts the night, and sunrise
belongs to the following local date. Moon events are included when they occur
within the same local-noon-to-local-noon window.

Sunset and sunrise are serialized as local `HH:MM` values. An event that does
not occur is represented by `--:--`. Sun and moon state are sampled for the
fixed local-hour slots defined by the embedded payload protocol. This handles
always-up and always-down cases without separate boolean fields.

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
requested observing night.

### Partial availability

Astronomy calculation does not depend on weather availability. A missing or
expired weather record must not fail the whole request. The display block is
still returned with `-` for its weather-derived numerical values.

The endpoint returns all six display blocks even when weather is available for
fewer nights. This gives firmware a predictable payload and avoids optional
records or variable block shapes.

An unexpected DynamoDB or astronomy calculation failure returns an error rather
than silently presenting a complete-looking but unreliable response.

## Errors

| Status | Body | When |
|---|---|---|
| `200` | Forecast response | The configuration exists, including when some weather is unavailable |
| `404` | `error=configuration_not_found` response | The identifier is missing or unknown |
| `500` | `error=forecast_unavailable` response | The endpoint cannot reliably assemble the response |

The API does not expose upstream errors, AWS details, or stack traces to the
client. Operational details belong in structured Lambda logs.

## Acceptance Criteria

1. A request for a known configuration returns `200`, content type `text/plain`,
   protocol version 1, and exactly six display blocks in ascending order.
2. The first night is selected using the location's timezone and local-noon
   boundary; the next five night identifiers are consecutive local dates.
3. Every display block contains the complete, fixed-order record set defined in
   `api-payload.txt` and all matrices contain exactly 22 valid characters.
4. Available, unexpired `#WEATHER` records supply the matching nights' minimum
   and maximum temperatures.
5. Expired, out-of-range, and other service records are not returned as weather.
6. Missing weather does not remove a display block; its weather numerical
   values use the documented unavailable sentinel.
7. An unknown configuration returns the versioned text error payload with
   status `404`.
8. Unit tests cover the local-noon boundary, six-night range, item grouping,
   expired-item filtering, missing weather, matrix encoding, sentinels, and
   serialization order.
9. An integration test verifies the deployed route for one known and one
   unknown configuration.

## Non-goals

- Backfilling or migrating old DynamoDB records.
- Fetching Clear Outside during an API request.
- Configuration CRUD or accepting arbitrary coordinates.
- Adding aurora or additional forecast services in this story.
- Guaranteeing that weather exists for every requested night.

## Decisions Still Needed

1. **Third matrix:** define the displayed meaning and source of `matrix_2`.
2. **Physical slot count:** confirm that the display has 22 LEDs for local
   14:00 through 11:00; the original sketch's count and examples did not match
   that interval.
3. **Matrix sampling:** confirm midpoint sampling for partial-hour sun and moon
   transitions.
4. **DST fallback:** decide how the repeated local hour maps to one physical LED
   slot when an observing night crosses the end of daylight saving time.
5. **Caching:** decide whether the API response needs HTTP cache headers and,
   if so, the maximum acceptable staleness relative to the six-hour weather
   ingestion schedule.

## Client Impact

The existing web client expects JSON from `GET /astro/{configurationId}`. Since
there is only one endpoint, changing it to this text protocol also requires the
web client to parse the line format or to stop consuming this route. The handler
and web client changes must be deployed together.