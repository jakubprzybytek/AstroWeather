# Aurora Forecast Supplier Evaluation

## Purpose

This document records the search for a source of aurora (northern lights)
forecasts for AstroWeather's configured locations (Wrocław by default), and
the feasibility assessment of the candidates found. It is based on live fetches
performed on **2026-09-28**.

The product asked for:

- forecasts for a given geographical location;
- forecasts by hour, so they can be drawn on the matrix display;
- an aurora probability expressible on the display's four-level scale;
- a 7-night horizon, matching the weather rows.

## Executive Assessment

**No public source publishes a per-location, hourly, 7-night aurora
forecast, and none can**: geomagnetic activity is only forecastable from
measured solar-wind conditions (up to ~3 days ahead, driven by CME arrival and
high-speed-stream predictions); beyond that the only outlook is a 27-day
solar-rotation recurrence table with one daily value. Every aurora site or app
checked (AuroraMe, SpaceWeatherLive, Aurora Map, PolarForecast, …) is a
re-packaging of the same two upstream feeds plus a latitude rule.

The recommendation is therefore to build the aurora row from the **upstream
feeds directly**, which are free, keyless, machine-readable, and openly
licensed, and to apply the location rule ourselves:

| Nights | Source | Resolution | Updated | Format / licence |
| --- | --- | --- | --- | --- |
| 1–3 | GFZ Hp60 ensemble forecast (PAGER/SWIFT) | **hourly**, 72 h ahead, with probabilities per Kp band | hourly | JSON, CC BY 4.0 |
| 1–3 (fallback) | NOAA SWPC 3-day Kp forecast | 3-hour bins | ~twice daily | JSON, US public domain |
| 4–7 | NOAA SWPC 27-day outlook | **one "largest Kp" per day** | weekly (Mondays) | text, US public domain |
| now (optional) | NOAA OVATION 30-minute nowcast | 1°×1° grid, per-location probability % | every 5 min | JSON, US public domain |

The per-location step is a geomagnetic-latitude threshold (see
[Deriving a four-level scale](#deriving-a-four-level-scale)); for Wrocław an
aurora is only plausible at Kp ≥ 6 and only a real display at Kp ≥ 8, i.e.
during G2+ and G4+ storms, a few nights per year. Most of the time the row
will be dark, which is the correct answer.

## Candidates Rejected

| Candidate | Finding |
| --- | --- |
| `api.auroras.live` (`/v1/?type=all&lat=…&long=…`) | Returns `HTTP 500` with an empty body; the service appears dead. |
| `auroraforecast.me/wrocaw` (AuroraMe) | Scrape-only page. Its own Kp comes from NOAA; hourly cloud from Open-Meteo. Shows 6 h and 27-day views, 72 h only in a paid app. No API. Nothing we could not assemble ourselves from the same feeds. |
| `aurora-service.eu` (Aurora Service Europe) | Forecast page says it is "down for maintenance". |
| `spaceweatherlive.com` | Republishes the NOAA 3-day (3-hourly) and 27-day (daily) Kp forecasts; "All rights reserved", no API. |
| NOAA "Aurora for Tonight and Tomorrow Night" (experimental) | Two-night horizon, North America only, published as PNG maps, not archived, no data file. |
| GFZ "Auroral Activity Forecast" | Kp-driven neural-network oval maps (Feng et al. 2025); image product, no data file found. |
| Met Office space weather | 4-day text summary only; data feeds need a "specialist space weather account". |
| Meteomatics space-weather API | Commercial. |
| Open-Meteo | No Kp/aurora parameter (open-meteo/open-meteo#328 remains a feature request). |

## Live Fetch Tests

All fetches were plain `curl` from a residential connection with a browser
`User-Agent`; no keys, cookies, or challenges were involved.

### GFZ Hp60 ensemble forecast (nights 1–3, primary)

```text
GET https://spaceweather.gfz.de/fileadmin/SW-Monitor/hp60_product_file_FORECAST_HP60_SWIFT_DRIVEN_LAST.json
```

- HTTP `200`, `application/json; charset=utf-8`, 18 KB, ~60 ms.
- `Last-Modified: Mon, 28 Sep 2026 19:05:18 GMT`; the page states the model
  runs every hour. No `Cache-Control` header.
- Shape: column-oriented, each column an object keyed by row index as a
  string. 73 rows at **1-hour spacing**, `28-09-2026 18:00` → `01-10-2026 18:00`
  UTC (72 h horizon).
- Columns: `Time (UTC)` (`dd-mm-yyyy HH:MM`), `minimum`, `0.25-quantile`,
  `median`, `0.75-quantile`, `maximum`, `prob 4-5`, `prob 5-6`, `prob 6-7`,
  `prob 7-8`, `prob >= 8`, and the individual ensemble members `hp60_0` …
  `hp60_N` (12–20 members, per the README).
- Hp60 is GFZ's hourly-resolution analogue of Kp on the same 0–9 scale (and,
  unlike Kp, is open-ended above 9); Kp-based thresholds apply directly.
- A sibling Kp product exists at
  `…/fileadmin/Kp-Forecast/CSV/kp_product_file_FORECAST_PAGER_SWIFT_LAST.json`
  (25 rows at 3-hour spacing, same columns with `kp_i` members) and Hp30 at
  `…/fileadmin/SW-Monitor/hp30_product_file_FORECAST_HP30_SWIFT_DRIVEN_LAST.json`.
- README: `https://spaceweather.gfz.de/fileadmin/SW-Monitor/README.rst`.
- Licence: GFZ states all index data and graphs are under **CC BY 4.0**, and
  asks that Matzka et al. (2021), doi:10.5880/Kp.0001 and
  doi:10.1029/2020SW002641, be cited. The forecast page adds an "as-is, no
  warranty" disclaimer. Produced within the EU PAGER (Horizon 2020) project.

### NOAA SWPC 3-day Kp forecast (nights 1–3, fallback)

```text
GET https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json
```

- HTTP `200`, `application/json`, 7 KB, ~70 ms, `Access-Control-Allow-Origin: *`,
  `Cache-Control: max-age=60`.
- Array of `{time_tag, kp, observed, noaa_scale}` rows at **3-hour spacing**:
  ~7 days of `"observed"` history, two `"estimated"` bins, then `"predicted"`
  bins to the end of day 3 (`2026-10-01T00:00:00` when fetched).
- Same numbers as the human-readable
  `https://services.swpc.noaa.gov/text/3-day-forecast.txt` (issued 12:30 UTC)
  and `…/text/3-day-geomag-forecast.txt` (issued 22:05 UTC), which also carry
  daily probabilities of "Active / Minor / Moderate / Strong-Extreme storm".
- Licence: US federal government work, public domain; no key, no documented
  rate limit beyond the 60 s cache hint.

### NOAA SWPC 27-day outlook (nights 4–7)

```text
GET https://services.swpc.noaa.gov/text/27-day-outlook.txt
```

- HTTP `200`, `text/plain`, 1.6 KB. `:Issued: 2026 Sep 28 0221 UTC` — issued
  weekly on Mondays.
- Fixed-width rows `YYYY Mon DD  F10.7  Ap  LargestKp`, 27 days from the issue
  date; for example the fetched table forecast `Largest Kp 4` for Oct 4–6 and
  `5` for Oct 22. This is a **daily maximum**, so every hour of a night gets
  the same level.
- A JSON alternative, `https://services.swpc.noaa.gov/json/45-day-forecast.json`,
  carries daily `ap` and `f107` but not Kp; Ap→Kp needs a lookup table, so the
  text product is simpler.

### NOAA OVATION 30-minute nowcast (optional "now" refinement)

```text
GET https://services.swpc.noaa.gov/json/ovation_aurora_latest.json
```

- HTTP `200`, 920 KB, refreshed every 5 minutes; `Observation Time`,
  `Forecast Time` (~30–90 min ahead) and 65 160 `[lon, lat, probability%]`
  triples on a 1° grid (longitude 0–359, so Wrocław is `[17, 51, …]`; it read
  `0` at 20:03 UTC on the test day).
- This is the only feed that is per-location out of the box, but it is a
  nowcast, not a forecast; it could refine the current hour's cell only.

## Deriving a Four-Level Scale

Kp/Hp60 is global; whether the aurora reaches a place depends on that place's
**magnetic latitude**. NOAA's viewing guidance gives the rule: at Kp 0 the
equatorward edge of the auroral oval sits at ~66° magnetic latitude and moves
~2° equatorward per Kp step (Kp 9 → ~48°), and the aurora "can often be
observed hundreds of kilometers equatorward" of that edge, low on the poleward
horizon. Magnetic longitude does not enter: two places on the same magnetic
latitude get the same thresholds, and longitude only matters through local
time (whether it is dark, and how close the hour is to magnetic midnight,
where the oval reaches furthest south).

### Which magnetic latitude

Two conventions give different numbers for the same place, and the choice is
worth about one Kp step:

- **Centred-dipole latitude** treats the field as a single tilted bar magnet
  through Earth's centre (north pole at ~80.7° N, 72.7° W) and is a pure
  spherical rotation of the geographic coordinates. For Wrocław
  (51.11° N, 17.03° E) it gives ~50°.
- **Corrected geomagnetic (CGM / AACGM) latitude** traces the actual IGRF
  field line from the location to the magnetic equator and labels the place
  with the dipole coordinates of that crossing. Auroral particles precipitate
  along field lines, so this is the latitude the oval actually follows, and it
  is what NOAA, OVATION and the aurora sites use. For Wrocław it gives ~47°.

They differ over Europe because the real field is offset ~500 km from
Earth's centre toward the western Pacific and bent by the non-dipole
Siberian anomaly, which connects European field lines to lower dipole
latitudes than the plain rotation suggests (and North American ones to
higher). That asymmetry is why the oval reaches further south over Canada and
the US than over Europe at the same geographic latitude.

**Use the CGM value.** Wrocław ≈ 47°, Kraków ≈ 46°; both should be stored as
a per-configuration constant rather than computed at runtime, since an AACGM
dependency is not worth it for two cities.

### Levels

Generalised for a location with CGM latitude `mlat` and a horizon-visibility
allowance of ~5° of latitude: `kpMin = (66 − (mlat + 5)) / 2` rounded to the
nearest whole step, then levels at `kpMin − 1`, `kpMin`, `kpMin + 1`. For
Wrocław `kpMin = 7`:

| Level | Kp / Hp60 | NOAA scale | What it means at Wrocław |
| --- | --- | --- | --- |
| `0` | < 6 | quiet – G1 | nothing to see |
| `1` | 6 – 6.99 | G2 | camera-only glow on the northern horizon, on a good night |
| `2` | 7 – 7.99 | G3 | faint naked-eye glow / red pillars low in the north |
| `3` | ≥ 8 | G4 – G5 | proper display, colour visible, possibly overhead |

This matches recent experience: the G3 storms of November 2023 and October
2024 (Kp 7) gave a photographable red glow across Poland, while only the
May 2024 G5 storm (Kp 9) put the aurora overhead.

Which value to threshold: for nights 1–3 use the GFZ **`median`** for the
level, and raise the level by one when the ensemble gives at least a 25 %
chance of the next band (`prob 7-8 + prob >= 8` for level `2`, `prob >= 8`
for level `3`), so a storm the ensemble is split about still shows as a
possibility. For nights 4–7 threshold the 27-day `Largest Kp` directly. These
weights are a first guess to tune by eye, like the cloud-coverage thirds were.

## Concerns And Risks

- **Skill drops sharply with lead time.** Days 1–3 come from a physics/ML model
  fed by live solar-wind data; days 4–7 come from a recurrence outlook that
  cannot see a CME launched after Monday. A big storm typically appears in the
  forecast 1–3 days ahead, so the display's far-right columns will mostly say
  "quiet" even in the week of a storm. That is inherent, not a supplier defect;
  the API should carry a `horizon`/`confidence` tag so the device can dim or
  hatch nights 4–7.
- **Two providers, two shapes, two clocks.** GFZ's JSON is column-oriented with
  `dd-mm-yyyy HH:MM` strings and no `Cache-Control`; NOAA's is row-oriented ISO
  timestamps without a `Z`. Both are UTC and must be re-binned into the
  local-noon-to-noon night model (`Europe/Warsaw`) already used by
  `#WEATHER`; the 27-day rows are UTC calendar days, so a "night" straddles two
  of them — take the max of both.
- **GFZ has no stated SLA and no cache headers.** `Last-Modified` moves hourly.
  Fetching every six hours with the existing job cadence is far below anything
  that would register.
- **File names encode the model** (`FORECAST_HP60_SWIFT_DRIVEN_LAST`); a PAGER
  pipeline change could rename them. The NOAA 3-day feed is the fallback for
  exactly that case, at the cost of 3-hour instead of 1-hour resolution.
- **Attribution.** CC BY 4.0 requires crediting GFZ (and the requested
  citation) wherever the data is republished — the API docs and any web UI.
  NOAA data is public domain but SWPC asks to be credited as the source.
- **Hp60 vs Kp above 9.** Hp60 is open-ended; clamp before thresholding.
- **Accuracy was not evaluated**; this test only established availability,
  shape, and terms.

## Recommended Integration

1. Extend the existing six-hourly scheduled job (or add a sibling) to fetch,
   per run: the GFZ Hp60 JSON, the NOAA 3-day Kp JSON (used only if the GFZ
   fetch or parse fails), and the NOAA 27-day outlook. The feeds are global,
   so fetch once and derive every configured location from the same payload.
2. Build, per location and per night, 24 hourly levels for local noon → noon:
   Hp60 (or NOAA 3-hour bins expanded to hours) for hours inside the 72 h
   window; the 27-day daily max beyond it; `null` where neither covers.
3. Store as the already-reserved item:

   ```text
   PK = LOC#wroclaw
   SK = NIGHT#2026-09-28#AURORA
   ```

   holding the hourly level array, the raw Kp/Hp60 median and `prob >= 6`
   per hour, a per-hour `source` (`gfz-hp60` / `noaa-3day` / `noaa-27day`),
   `fetchedAt` and `expireAt` — the same pattern as `#WEATHER`.
4. Expose an aurora row in the API payload using the existing `0`–`3` level
   characters, and flag the nights that come from the 27-day outlook so the
   device can render them at reduced brightness or blinking.
5. Fail closed as for weather: keep the last good item and fail the invocation
   when parsing breaks; add fixture-based parser tests for both feeds so a
   column rename is caught in CI.
6. Add attribution for GFZ (CC BY 4.0, Matzka et al. 2021) and NOAA SWPC to
   the API documentation.

## Decision Checklist Before Adoption

- Agree the Wrocław/Kraków thresholds and the "raise on 25 % probability" rule
  by watching the row through a real G1–G2 event.
- Decide how nights 4–7 are shown (dimmed, hatched, or hidden) given they are a
  daily recurrence outlook, not a forecast.
- Decide whether the OVATION nowcast is worth 920 KB per fetch for a
  current-hour refinement, or whether Hp60's hourly value is enough.
- Confirm the attribution wording for GFZ and NOAA in `api.md`.

## Sources Checked

- GFZ Kp forecast: `https://spaceweather.gfz.de/products-data/forecasts/forecast-kp-index`
- GFZ Hp30/Hp60 forecast: `https://spaceweather.gfz.de/products-data/forecasts/forecast-hp30-hp60-indices`
- GFZ auroral activity forecast: `https://spaceweather.gfz.de/products-data/forecasts/auroral-activity-forecast`
- GFZ Kp data and licence: `https://kp.gfz.de/en/data`, `https://kp.gfz.de/en/about-kp`
- NOAA SWPC product index: `https://services.swpc.noaa.gov/products/`,
  `https://services.swpc.noaa.gov/json/`
- NOAA Kp forecast: `https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json`
- NOAA 3-day text forecasts: `https://services.swpc.noaa.gov/text/3-day-forecast.txt`,
  `https://services.swpc.noaa.gov/text/3-day-geomag-forecast.txt`
- NOAA 27-day outlook: `https://services.swpc.noaa.gov/text/27-day-outlook.txt`
- NOAA 45-day Ap/F10.7: `https://services.swpc.noaa.gov/json/45-day-forecast.json`
- NOAA OVATION nowcast: `https://services.swpc.noaa.gov/json/ovation_aurora_latest.json`
- NOAA aurora tonight/tomorrow (experimental):
  `https://www.spaceweather.gov/products/aurora-viewline-tonight-and-tomorrow-night-experimental`
- NOAA viewing tips (Kp vs latitude): `https://www.spaceweather.gov/content/tips-viewing-aurora`
- Met Office space weather: `https://weather.metoffice.gov.uk/specialist-forecasts/space-weather`
- AuroraMe Wrocław page: `https://auroraforecast.me/wrocaw`
- SpaceWeatherLive: `https://www.spaceweatherlive.com/en/auroral-activity/aurora-forecast.html`
- Aurora Service Europe: `https://www.aurora-service.eu/aurora-forecast/` (down)
- auroras.live API: `https://api.auroras.live/v1/?type=all&lat=51.11&long=17.03&tz=120` (HTTP 500)
- Open-Meteo Kp request: `https://github.com/open-meteo/open-meteo/issues/328`
