# Meteosource Weather Supplier Evaluation

## Purpose

This document records the initial evaluation of Meteosource as a weather forecast
supplier for AstroWeather. It is intended to support a later supplier decision
and the design of weather ingestion and persistence structures.

This assessment is based on a live API test performed on **2026-09-10** and on
the provider's public documentation and pricing pages. Forecast accuracy and
long-term reliability were not independently verified.

## Executive Assessment

Meteosource is a good candidate for the first weather integration:

- The API is operational and returns valid JSON for a point forecast.
- It provides the variables needed for astronomy-oriented conditions: cloud
  cover, precipitation, wind, temperature, and weather classification.
- It supports location-specific IANA timezones and both hourly and daily sections.
- Its independent HTTP API fits the Lambda and scheduled-ingestion architecture.

The free tier is appropriate for development, but not for several complete
hourly nights. The minimum paid tier observed during this evaluation provides
three days of hourly forecasts, which is sufficient for tonight and roughly the
next two nights. A longer hourly horizon is needed if the product must show four
or more complete nights.

## Live API Test

The tested request used the point forecast endpoint for Krakow:

```text
GET /api/v1/free/point
  ?lat=50.0647
  &lon=19.9450
  &sections=all
  &timezone=auto
  &language=en
  &units=metric
  &key=<secret>
```

The key was used only for the live test and is intentionally not recorded here.
It must not be put in source control or exposed in the browser bundle.

### Observed result

- HTTP status: `200`
- Content type: `application/json`
- Full response size: approximately 8.3 KB
- Server request time: approximately 253 ms
- Current-only request size: 306 bytes
- Current-only request end-to-end time: approximately 83 ms
- Returned location: latitude `50.0647N`, longitude `19.945E`
- Returned elevation: 217 m
- Returned timezone: `Europe/Warsaw`
- Returned units: metric

The response included `current`, `hourly`, and `daily` sections. The free-tier
response contained 24 hourly records and seven daily records. Hourly timestamps
were local wall-clock strings when `timezone=auto` was used, for example:

```json
{
  "date": "2026-09-10T20:00:00",
  "weather": "overcast",
  "temperature": 13.8,
  "wind": { "speed": 1.2, "dir": "NNW", "angle": 337 },
  "cloud_cover": { "total": 92 },
  "precipitation": { "total": 0, "type": "none" }
}
```

A request with `timezone=UTC` returned the same forecast instants as UTC wall
clock values. This is preferable for persistence because the returned local
strings do not include an explicit UTC offset.

## Data Fit For AstroWeather

The useful fields for an observing forecast include:

- Total cloud cover, with optional cloud-layer fields depending on the plan.
- Precipitation amount and type.
- Temperature.
- Wind speed, direction, and angle.
- Weather classification, icon, and summary.
- Provider location, elevation, timezone, and units metadata.

Meteosource's normal daily forecast is calendar-day based. It should not be used
as the source for the application's local-noon-to-local-noon night partition.
The tested free response also returned `null` for the daily `morning`,
`afternoon`, and `evening` blocks.

The nightly model should therefore be built from hourly records. Astronomy
information such as sunrise, sunset, moonrise, and moonset should continue to be
calculated or supplied by the separate astro service rather than inferred from
Meteosource's daily summary.

## Plan And Operational Constraints

The public pricing information observed during the evaluation showed the
following weather forecast limits:

| Plan | Calls | Hourly horizon | Daily horizon | Other observations |
|---|---:|---:|---:|---|
| Free | 400/day, 10/min | 1 day | 7 days | English only; standard variables; provider mention and backlink required |
| Startup | 10,000/day, 350/min | 3 days | 10 days | 15 languages; 7-day horizon shown as extendable; 98% uptime shown |

The exact features and prices are subscription-dependent and should be checked
again before implementation or purchase.

The provider's update page states that observations are gathered in real time,
that most locations are updated about every 10 minutes, and that hourly
forecasts are revised as necessary, usually about every hour. This supports an
hourly ingestion schedule for AstroWeather. Polling every 10 minutes would
consume quota without a corresponding need in the product.

## Recommended Integration

Use one server-side weather ingestion function per provider, invoked by an
EventBridge schedule approximately once per hour for each configured location.
The function should:

1. Request `sections=hourly,daily` with `timezone=UTC` and metric units.
2. Convert each returned UTC timestamp to the location's IANA timezone.
3. Assign each hourly sample to the local-noon-to-local-noon `nightId`.
4. Upsert the provider's weather item for each affected night.
5. Record fetch time, source metadata, and freshness status.
6. Preserve the last successful forecast when a provider request fails.

The public API should read the stored weather data rather than call Meteosource
directly. This keeps the provider credential private, avoids exposing upstream
latency to clients, enables source-specific retry behavior, and gives the API a
stable response even during a temporary provider outage.

## Night Assignment

A night is anchored by the local noon that starts it:

```text
nightId = YYYY-MM-DD for the local noon starting the night
```

For example, `nightId=2026-09-10` represents local time from
`2026-09-10T12:00` through `2026-09-11T12:00` in the location timezone.

Use an IANA timezone conversion library and UTC instants. Do not assign nights
by slicing the provider's local timestamp strings, because daylight-saving
transitions can make local wall-clock times ambiguous or cause a night to have
23 or 25 hourly samples.

## Proposed DynamoDB Weather Item

Use the shared nightly table described in the architecture document:

```text
PK = LOC#krakow-home
SK = NIGHT#2026-09-10#WEATHER
```

A weather item can have this shape:

```typescript
type WeatherNightItem = {
  pk: string;
  sk: string;
  entityType: 'night-weather';
  locationId: string;
  nightId: string;
  service: 'weather';
  provider: 'meteosource';
  fetchedAt: string;
  forecastValidFrom: string;
  forecastValidTo: string;
  timezone: string;
  units: 'metric' | 'imperial';
  hourly: Array<{
    timestamp: string;
    temperature?: number;
    cloudCover?: number;
    precipitationAmount?: number;
    precipitationType?: string;
    windSpeed?: number;
    windDirection?: string;
    weather?: string;
  }>;
  summary?: {
    cloudCoverMin?: number;
    cloudCoverAverage?: number;
    cloudCoverMax?: number;
    precipitationTotal?: number;
    temperatureMin?: number;
    temperatureMax?: number;
    windSpeedMax?: number;
  };
  sourceMetadata?: {
    latitude: number;
    longitude: number;
    elevation?: number;
  };
  ttl: number;
};
```

The stored structure should be treated as an internal normalized model. Keep
provider-specific response details out of the public API contract where they do
not add product value. If raw responses are needed for debugging or later
reprocessing, store them separately in S3 with a short retention period and
keep only a pointer in DynamoDB.

## Quota Estimate

At one combined hourly request per location:

```text
24 requests per location per day
```

The free quota theoretically covers 16 locations per day, before retries and
other endpoints. A lower operational ceiling of 10 to 12 locations is more
realistic on the free plan. A single request containing both hourly and daily
sections is preferable to separate requests.

## Credential And Security Requirements

- Rotate the key used during the initial evaluation before production use.
- Store the replacement as an SST secret or in AWS Secrets Manager.
- Read the secret only from the server-side ingestion function.
- Never place the key in React/Vite environment variables or browser requests.
- Do not log the request URL, query string, or the complete provider response if
  it could contain credentials or unnecessary location data.
- Add retry with bounded exponential backoff and record provider failures without
  deleting the last known good forecast.

## Supplier Decision Checklist

Before selecting Meteosource for production, verify:

- The purchased plan's hourly horizon covers the number of nights AstroWeather
  promises.
- The plan permits the intended commercial use and removes or satisfies any
  attribution requirement.
- Required variables include cloud layers, precipitation probability/rate, and
  uncertainty or predictability if the UI will use them.
- Rate limits and quotas support the expected number of locations plus retries.
- The service's uptime commitment and support response are acceptable.
- Terms permit storing normalized forecast data in DynamoDB and serving it from
  AstroWeather's API.
- A second provider or graceful degradation strategy is available for outages.
- A sample of forecasts has been compared against observations for the locations
  most important to the product.

## Sources Checked

- Meteosource interactive documentation:
  `https://www.meteosource.com/client/interactive-documentation`
- Meteosource weather forecast product page:
  `https://www.meteosource.com/api-weather-forecast`
- Meteosource pricing:
  `https://www.meteosource.com/pricing`
- Meteosource data update information:
  `https://www.meteosource.com/meteosource-data-update`
