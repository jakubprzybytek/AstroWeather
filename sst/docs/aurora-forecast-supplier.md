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
| current hour, storm nights | NOAA OVATION 30-minute nowcast | 1°×1° grid, per-location probability % | every 5 min | JSON, US public domain |

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

### NOAA OVATION 30-minute nowcast (current hour, storm nights)

```text
GET https://services.swpc.noaa.gov/json/ovation_aurora_latest.json
```

- HTTP `200`, 920 KB, refreshed every 5 minutes; `Observation Time`,
  `Forecast Time` (~30–90 min ahead) and 65 160 `[lon, lat, probability%]`
  triples on a 1° grid (longitude 0–359, so Wrocław is `[17, 51, …]`; it read
  `0` at 20:03 UTC on the test day).
- This is the only feed that is per-location out of the box, but it is a
  nowcast, not a forecast: it refines one cell of the row. Note that its
  `Forecast Time` is 30–90 minutes after `Observation Time` (the solar wind's
  travel time from L1), so a fetch describes the *coming* hour, not the one
  it was fetched in. See [Fetch Cadence](#fetch-cadence).

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

## Fetch Cadence

The sources move at four different speeds and the device consumes at two, so
the cadence follows whichever is slower, and the nowcast is gated on activity
rather than scheduled.

### Device cadence and the storm-night flag

The HostController pulls the API every six hours (00:10, 06:10, 12:10 and
18:10 local). A flag in the API response switches it to **hourly** pulls; the
API sets the flag for a location's current night when there is a chance of
aurora. The flag is:

- **Eager**: set when any hour of the night has a GFZ `0.75-quantile` ≥ 5 or
  `P(≥6)` ≥ 25 %, or the NOAA 3-day forecast has a bin ≥ 5 — one Kp step below
  level `1`. An hourly pull is cheap, so a false alarm is cheap; a missed storm
  is not.
- **Sticky**: once set for a `nightId` it stays until that night ends, so the
  device does not alternate cadences as forecast runs disagree.
- **Late by design**: the device sees a newly set flag only at its next
  six-hourly pull, up to six hours after the forecast turned. The eager
  trigger, decided from a forecast that runs hourly, is what closes most of
  that gap.

### Job cadence

| Situation | Job runs | Fetches |
| --- | --- | --- |
| Quiet (no location flagged) | with the existing weather job, every 6 h at 00/06/12/18 local | Hp60, NOAA 3-day, 27-day outlook |
| Daily | once at ~03:00 UTC | 27-day outlook (issued Mondays ~02:20 UTC; catches a mid-week reissue) |
| Storm night (a location flagged and dark there) | **every 10 minutes** | Hp60, NOAA 3-day, OVATION |

The three forecast feeds are global: one fetch per run serves every
configured location. Every source sends `Last-Modified`, so the runs poll
with `If-Modified-Since` and an unchanged file costs a 304. Runs are offset
from the hour (GFZ writes at ~:05, NOAA regenerates around :00), so the
storm-night schedule is `:03, :13, :23 …`, which also leaves the device's :10
pull a nowcast at most seven minutes old. NOAA's `Cache-Control: max-age=60`
is far above any of these rates. On a storm night the job moves about 55 MB
(OVATION is 920 KB) in ~60 invocations; on a quiet night, ~30 KB in four.

### Why ten minutes and not an hour

For the display alone, hourly at :03 would do: the device pulls hourly and
Hp60 changes hourly. Ten minutes buys three things the display cadence does
not:

- **Substorm peaks.** Auroral activity is bursty on a 20–60 minute scale, so
  one snapshot per hour can land in a lull and print 3 % for an hour that
  peaked at 30 %. With six samples per hour the job stores the **maximum over
  the hour** for each slot as well as the latest value, so a back-filled slot
  says whether there was aurora that hour, not what L1 looked like at :03.
- **Calibration data.** The OVATION % → level mapping is the least known part
  of the design and is only ever calibrated on the few storm nights a year;
  ~60 samples a night instead of ~10 makes those nights count.
- **Resilience.** A failed :03 fetch leaves the device an hour-old nowcast
  under hourly polling, and a 13-minute-old one under ten-minute polling.

Thirty minutes is the worst of both: too coarse for peaks, no simpler than ten.

### How the nowcast lands in the row

- A fetch writes its level into the slot that OVATION's `Forecast Time` falls
  in — usually the *next* hour — and a slot keeps the latest nowcast that
  targeted it, plus the maximum seen. Over a storm night the row fills in
  hour by hour: the current hour shows the nowcast fetched about an hour ago,
  the next hour the fresh one, everything beyond stays forecast.
- Until the mapping is calibrated, the nowcast only **raises** a slot above
  its forecast level, never lowers it; the failure mode is then an optimistic
  hour, not a hidden storm. Placeholder thresholds: `1` ≥ 5 %, `2` ≥ 15 %,
  `3` ≥ 40 %, with the raw percentage logged on every storm-night fetch.
- Daylight slots are skipped; there is nothing to see and the nowcast would
  only add noise.
- Nowcast-sourced slots are marked (blink or level attribute) so a live cell
  can be told from a forecast one on the matrix.

The first implementation step is the cheapest: run the storm-night nowcast
fetch with **logging only**, so a calibration set accumulates before any
nowcast value reaches the display.

## Calibrating the Nowcast

The forecast row is in Kp; the nowcast cell is in OVATION's "probability of
visible aurora" for a 1° cell. Nothing published relates the two for a place
at 47° CGM, and the placeholder thresholds above (`1` ≥ 5 %, `2` ≥ 15 %,
`3` ≥ 40 %) are a guess. They can only be calibrated on real storms, which
come a few nights a year, so the logging has to be in place **before** the
next one, and the nowcast must not shape the display until it is done.

### What to log on every storm-night run

One structured log line per location per ten-minute run, with a fixed prefix
(`aurora-calibration`) and a JSON body, so CloudWatch Logs Insights can pull a
storm night out in one query:

| Field | Source | Why |
| --- | --- | --- |
| `observedAt`, `validAt` | OVATION `Observation Time`, `Forecast Time` | Aligns the sample with the hour it describes. |
| `cellPct` | OVATION, the location's cell | The value the placeholder mapping uses. |
| `northPct[]` | OVATION, the cells 1°–8° north on the same meridian | An observer at 47° CGM sees the oval low on the northern horizon, hundreds of kilometres away; the cell overhead can read 0 % while the cells at 54°–56° N read 30 % and the glow is plainly visible. The profile shows which cell, if any, predicts what is seen. |
| `hp60Median`, `hp60Max`, `probAtLeast6` | GFZ Hp60 for the hour of `validAt` | The forecast the nowcast is compared with. |
| `kpEstimated` | NOAA 3-day, the `estimated` bin covering `validAt` | NOAA's running estimate of the current Kp. |
| `level` | the row's forecast level for that slot | What the display would have shown without the nowcast. |
| `dark` | astronomy for the location | Daylight samples are excluded from the analysis. |

The definitive Kp for each three-hour bin arrives a day later as the
`observed` rows of the same NOAA file (or from `kp.gfz.de/app/json/`); the
analysis joins it by time rather than logging it live.

### Ground truth

Kp and OVATION are both proxies; the thing being calibrated is *visibility
from the configured locations*. For each storm night, record what was
actually seen, with times: own observations and photographs, and public
reports from Polish observers (the sighting threads that appear on every
G2+ night are dated to the minute and give the direction and height above the
horizon). A night with no reports despite clear skies is data too.

### Analysis

After each storm night, for every dark ten-minute sample:

1. Tabulate `cellPct`, `max(northPct)`, `hp60Median`, the definitive Kp and
   the visibility record for that time.
2. Check the forecast side first: did level `1`/`2`/`3` (Kp 6/7/8) coincide
   with camera-only / naked-eye / overhead reports? This validates the
   magnetic-latitude thresholds and is independent of OVATION.
3. Then find the `cellPct` (or `northPct`) values that coincide with the same
   three visibility classes, and set the nowcast thresholds from them. Decide
   from the data which cell predicts better, the location's own or the
   northern profile's maximum.
4. Compare the ten-minute samples within an hour with the hour's maximum, to
   see whether the max-over-hour rule is worth keeping.

### Exit criterion

Keep the nowcast in **raise-only** mode (it can lift a slot above its
forecast level, never lower it) until at least three G2+ nights have been
logged and analysed. Only then let it set the slot outright, and revisit the
thresholds after every further G3+ night; the oval's behaviour at the
equatorward edge is not linear in Kp, and the biggest storms are the ones the
row exists for.

## Merging Sources

Each slot of a night takes its value from the best source that covers it, so
the cut-over from a better source to a worse one falls exactly where the
better one runs out of data.

### The rule

For each of the night's 21 hourly observing slots (14:00 → 10:00 local), **the
highest-ranked source whose span contains the slot's midpoint wins**.
`observingSlots()` in `forecast/nights.ts` already gives each slot its
`midpoint`. With whole-hour zone offsets every span boundary falls on a slot
boundary, so the test is exact; the midpoint also handles half-hour zones and
OVATION's odd-minute windows without special cases.

| Rank | Source | Covers | Unit |
| --- | --- | --- | --- |
| 0 | OVATION nowcast | slots its `Forecast Time` fell in (latest and max over the hour) | % → level; **raise-only** until calibrated |
| 1 | NOAA observed / estimated Kp | past three-hour bins | Kp |
| 2 | GFZ Hp60 | now → +72 h, hourly | Hp60 median, with the ensemble bump |
| 3 | NOAA 3-day predicted | three-hour bins to the end of UTC day 3 | Kp |
| 4 | NOAA 27-day outlook | UTC days | largest Kp of the day |
| — | none | | `?` (unknown) |

For example (the exact hours move with each fetch): on a night in CEST where
GFZ ends at 18:00 UTC, GFZ covers 14:00–19:59 local, the NOAA 3-day bins may
carry on for a few more hours, and the 27-day outlook takes the rest: its
first UTC day until 01:59 local and the next UTC day from 02:00. A night
covered only by the outlook is the same case — two spans from two
consecutive UTC days, split at local 01:00 or 02:00.

### Details that make it work

1. **Merge on Kp, threshold once.** Ranks 1–4 all yield a Kp-equivalent, and
   the location's magnetic-latitude thresholds (see
   [Levels](#levels)) turn the merged value into a level in one place, so
   every source is judged on the same scale. The GFZ ensemble bump (≥ 25 %
   for the next band) applies to GFZ slots only. The nowcast is in a
   different unit, so it has its own % → level mapping and merges at the
   level stage.
2. **Past hours have a source.** GFZ starts at the current hour, so tonight's
   earlier slots would otherwise fall through to the outlook or to `?`.
   NOAA's `observed` and `estimated` bins cover them, and on a storm night the
   nowcast's max over the hour raises them further, so past hours show what
   happened rather than an old forecast. (The web tool drops those bins; the
   merge keeps them.)
3. **Fallback is by freshness, not only presence.** GFZ outranks the NOAA
   3-day forecast only while its file is fresh (`Last-Modified` under three
   hours old). If the fetch failed or the file went stale, its slots fall
   through to rank 3; the cascade is the fallback.
4. **Seams are marked, not smoothed.** The 27-day value is a *daily maximum*,
   while GFZ gives an hourly median, so crossing from rank 2 to rank 4
   typically steps up (median 2 → daily max 4). That is honest — "this day
   may reach 4" — but reads as a spike unless it is marked. Each slot
   carries its `source`, and the display dims outlook slots and marks
   nowcast slots as live, so a step reads as lower confidence from there on.
5. **The nowcast is an overlay for now.** Until
   [calibration](#calibrating-the-nowcast) is done it only raises a slot above
   the cascade's result, never lowers it. After calibration it becomes rank
   0 outright.

### Where the merge runs

At **read time, in the API**, not at ingestion. Ingestion stores each
source's spans raw per night; a pure `mergeAurora(slots, sources, now,
thresholds)` function applies the cascade when the payload is assembled, as
`weather-reader.ts` and `assemble.ts` do for weather, and is unit-tested with
the saved fixtures. Changing the ranking, the freshness limit or the
thresholds then needs no re-ingestion, and the per-slot `source` can be shown
in the web tools for debugging.

Per slot the merge yields the `level`, the `source` and the value it came
from (Kp, or % for the nowcast). The payload row is the levels plus the
dimmed and live marks; the storm-night flag is decided from the same merged
night.

## Recommended Integration

1. Add an aurora job on the cadence in [Fetch Cadence](#fetch-cadence): with
   the six-hourly weather job when quiet, every ten minutes on a flagged
   storm night, plus a daily 27-day fetch. It fetches the GFZ Hp60 JSON, the
   NOAA 3-day Kp JSON (used only if the GFZ fetch or parse fails), the NOAA
   27-day outlook, and on storm nights OVATION. The forecast feeds are global,
   so fetch once and derive every configured location from the same payload.
2. Store each source's spans raw, per location and per night, as one item
   per source under the reserved `#AURORA` suffix:

   ```text
   PK = LOC#wroclaw
   SK = NIGHT#2026-09-28#AURORA#GFZ
   SK = NIGHT#2026-09-28#AURORA#NOAA3
   SK = NIGHT#2026-09-28#AURORA#NOAA27
   SK = NIGHT#2026-09-28#AURORA#OVATION
   ```

   Each holds the spans at the source's own granularity (the NOAA 3-day item
   including its `observed` and `estimated` bins; the OVATION item the latest
   and maximum percentage per slot, with the raw samples), the source's
   `Last-Modified`, `fetchedAt` and `expireAt` — the same pattern as
   `#WEATHER`. `begins_with "NIGHT#<id>#AURORA"` still returns them all in
   one query.
3. Merge at read time as in [Merging Sources](#merging-sources): per slot,
   the best fresh source covering it, one threshold pass, the nowcast as a
   raise-only overlay.
4. Expose an aurora row in the API payload using the existing `0`–`3` level
   characters, the storm-night flag that switches the device to hourly pulls,
   and marks for the slots that come from the 27-day outlook (dimmed) and
   from the nowcast (live).
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
- Calibrate the OVATION % → level thresholds from the logged storm-night
  samples before the nowcast reaches the display; see
  [Calibrating the Nowcast](#calibrating-the-nowcast).
- Confirm the storm-night flag trigger (0.75-quantile ≥ 5 / `P(≥6)` ≥ 25 %)
  does not fire so often that the hourly progress bar becomes a nuisance.
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
