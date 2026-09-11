# Clear Outside (HTML Scraping) Supplier Evaluation

## Purpose

This document records the feasibility assessment of `clearoutside.com` as a data
source for AstroWeather. Clear Outside has no public API; any integration would
require scraping its HTML forecast page. This assessment is based on a live
fetch performed on **2026-09-11** against:

```text
https://clearoutside.com/forecast/27.86/34.30
```

## Executive Assessment

Scraping is **technically feasible** and the data is unusually well-suited to
this product: it is astronomy-oriented, already organized around a
noon-to-noon day, and includes fields no generic weather API provides (per-hour
observing-condition rating, Bortle/sky-brightness estimate, and civil/nautical/
astronomical dark windows alongside sun and moon rise/set). However, there is
**no official API, no documented terms for automated access, and no stability
guarantee on the HTML structure**. It should be treated as a supplementary
enrichment source behind a resilient, low-frequency scraper and cache — not as
the sole or primary weather/astro data source.

## Live Fetch Test

```text
GET https://clearoutside.com/forecast/27.86/34.30
User-Agent: Mozilla/5.0 (...) Chrome/120.0 Safari/537.36
```

### Observed result

- HTTP status: `200`
- Content type: `text/html; charset=UTF-8`
- Page size: approximately 217 KB
- Response time: approximately 128 ms
- Server: Cloudflare (`cf-cache-status: DYNAMIC`), no bot-challenge encountered
  with a plain `curl` request and a standard browser `User-Agent`.
- `cache-control: no-store, no-cache, must-revalidate` — the page is generated
  per request and not intended to be cached by intermediaries.
- `robots.txt` returns the site's normal 404 page (not a real robots file), so
  no machine-readable crawl policy is published one way or the other.
- No public API, JSON endpoint, or documented rate limit was found.

### Page identity and header data

The page header for the tested coordinates included:

```text
Forecast for Um el Sid, Egypt (27.86,34.30)
Est. Sky Quality: 18.7 Magnitude. Class 7 Bortle. 3.58 mcd/m² Brightness.
3412.59 μcd/m² Artificial Brightness.
Generated: 11/09/26 12:04:11. Forecast: 11/09/26 to 17/09/26. Timezone: UTC+3.00
```

This confirms: reverse-geocoded place name, a **light-pollution/Bortle
estimate** for the location, generation timestamp, a **7-day** forecast range,
and the location's UTC offset (needed to convert the page's local hour labels).

## Data Fields Observed

The forecast is split into 7 day blocks (`id="day_0"` … `id="day_6"`), each
containing 24 hourly columns. Within each day block:

- **Per-hour observing rating** (`fc_hour_ratings`): a qualitative label
  (`Poor`/`OK`/`Good`, inferred from CSS classes `fc_bad`/`fc_ok`/`fc_good`) —
  Clear Outside's own composite "is this a good time to observe" signal.
- **Moon**: phase name, illumination percentage, rise/set times, and (via a
  popover `data-content` attribute) meridian time, altitude, and distance.
- **Sun / darkness** (via a popover `data-content` attribute): sunrise, sunset,
  solar transit, and civil/nautical/astronomical dark start-end windows.
- **Total / Low / Medium / High cloud cover** (% sky obscured), per hour.
- **ISS passover** windows with start/max/end time, direction, elevation, and
  magnitude, where applicable.
- **Visibility** (miles), **fog** (%).
- **Precipitation type, probability (%), and amount (mm)**.
- **Wind speed and direction** (mph, with compass label and degrees).
- **Chance of frost**, **temperature**, **feels-like**, **dew point**.
- **Relative humidity (%)**, **pressure (mb)**, **ozone (du)**.

All of the above is present as plain server-rendered HTML — no JavaScript
execution is required to obtain the values.

### Alignment with the noon-to-noon night model

The default view's hourly columns for `day_0` start at hour `12` (noon) and run
through hour `11` the next calendar day — i.e., the page is **already laid out
per the app's local-noon-to-local-noon night definition**, without any
transformation needed beyond confirming the location's UTC offset from the page
header. A `?view=midnight` query parameter exists to re-center the columns on
midnight instead; the default (midday-centered) view should be used for this
project.

## Concerns And Risks

- **No official API or contract.** All access is unofficial HTML scraping.
  There is no SLA, no versioning, and no notice of breaking changes.
- **Fragile parsing.** The data model depends on specific CSS class names and
  DOM structure (`fc_day`, `fc_detail_row`, `fc_hours`, popover `data-content`
  strings). Any site redesign can silently break the parser; scraping code
  needs defensive parsing and monitoring/alerting on shape changes, not silent
  failure.
- **Terms and Conditions** state the service and its content are the exclusive
  property of First Light Optics Ltd and are protected by copyright, with no
  explicit permission or prohibition for automated/programmatic use. This is a
  legal gray area for a product that stores and republishes the data. Written
  permission or a licensing conversation with the site owner is recommended
  before relying on this source in production, especially for a public-facing
  or commercial product.
- **No documented rate limits.** Cloudflare fronts the site and could rate-limit
  or challenge requests from datacenter/cloud IP ranges (such as AWS Lambda)
  more aggressively than from a residential browser, even though a single test
  request succeeded without a challenge. Aggressive polling risks IP blocking.
- **Attribution suggests a third-party upstream source.** The site footer credits
  "Powered by Forecast" (the old Dark Sky / forecast.io branding). Dark Sky's
  public API was shut down after its acquisition by Apple, so the ultimate data
  pipeline behind Clear Outside is unclear and not something AstroWeather can
  verify or depend on directly.
- **No coverage/accuracy guarantee.** Forecast accuracy for the underlying model
  was not evaluated as part of this test.

## Recommended Integration (If Adopted)

Only pursue this as a **secondary/enrichment source**, not a replacement for a
documented weather API:

1. A single scheduled Lambda (low frequency — e.g., every few hours, not
   hourly) fetches the page per configured location with a realistic
   `User-Agent` and reasonable timeout/backoff.
2. Parse server-rendered HTML with a robust HTML parser (not regex) and fail
   closed: if expected elements are missing, keep the last good cached result
   and emit a monitoring alert rather than serving partial/incorrect data.
3. Normalize only the fields not otherwise available from a primary weather
   provider — the per-hour observing-condition rating, Bortle/sky-brightness
   estimate, and astronomical/nautical/civil dark windows are the most
   differentiated additions.
4. Store results using the same nightly DynamoDB pattern as other sources:

   ```text
   PK = LOC#krakow-home
   SK = NIGHT#2026-09-10#SKY_CONDITIONS
   ```

5. Cache aggressively and keep polling frequency low to reduce legal/ethical
   exposure and the chance of IP blocking; this data changes slowly enough that
   frequent polling has little product value.
6. Do not expose the scraped HTML or Clear Outside's own text/branding directly
   in the API response; store only normalized numeric/categorical fields.

## Decision Checklist Before Adoption

- Contact First Light Optics Ltd (site operator) about acceptable automated use
  or a data-sharing arrangement, given the absence of a published API or terms
  for scraping.
- Confirm acceptable request frequency and add safeguards (backoff, caching,
  circuit breaker) to avoid being blocked.
- Build parser regression tests against saved HTML fixtures so a future site
  redesign is caught by CI rather than discovered in production.
- Decide whether the unique observing-rating/Bortle data is valuable enough to
  justify the legal and maintenance risk, versus relying solely on a
  documented weather API (see
  [Meteosource Weather Supplier Evaluation](meteosource-weather-supplier.md))
  plus the app's own `suncalc`-based astro calculations.

## Sources Checked

- Forecast page: `https://clearoutside.com/forecast/27.86/34.30`
- Midnight-centered view: `https://clearoutside.com/forecast/27.86/34.30?view=midnight`
- FAQ: `https://clearoutside.com/page/faq/`
- Terms and Conditions: `https://clearoutside.com/page/terms_and_conditions/`
- `https://clearoutside.com/robots.txt` (not a real robots file; returns the
  site's standard page)
