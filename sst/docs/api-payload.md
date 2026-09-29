AstroWeather embedded payload protocol
=====================================

Purpose
-------

GET /astro/{configurationId} returns a small line-oriented payload that can be
parsed on an STM32 without a JSON parser. The payload is UTF-8 but contains
ASCII characters only.

HTTP response
-------------

Status: 200
Content-Type: text/plain; charset=utf-8

Wire example
------------

```
protocol=3
configurationId=krakow
time=2026-09-22T23:22:45.678+02:00
lastWeatherFetchTime=2026-09-22T18:00:04+02:00
refreshIntervalMinutes=60

display=0
board=num4x4_matrix5x21
nightId=2026-09-17
numeric_0=20:30
numeric_1=05:59
matrix_0=333320000000000002333
matrix_1=000023333333333333200
matrix_2=110011122233333221100
matrix_3=0000000111223**211110
matrix_4=000000011122bb2211111
numeric_2=18
numeric_3=9

display=1
board=num4x4_matrix5x21
nightId=2026-09-18
numeric_0=20:28
numeric_1=06:01
matrix_0=333320000000000002333
matrix_1=???????33333333333100
matrix_2=000333333333333333300
matrix_3=000000000000003000000
matrix_4=000000000000011111111
numeric_2=18
numeric_3=9

display=2
board=num4x4_matrix5x21
nightId=2026-09-19
numeric_0=?
numeric_1=?
matrix_0=?
matrix_1=?
matrix_2=?
matrix_3=?
matrix_4=?
numeric_2=?
numeric_3=?

display=3
board=num4x4_matrix5x21
nightId=2026-09-20
numeric_0=20:23
numeric_1=06:04
matrix_0=333320000000000002333
matrix_1=000000000000000000000
matrix_2=000000000000000000000
matrix_3=000000000000000000000
matrix_4=000000000000000000000
numeric_2=16
numeric_3=8

display=4
board=num4x4_matrix5x21
nightId=2026-09-21
numeric_0=20:21
numeric_1=06:06
matrix_0=333320000000000002333
matrix_1=000000000000000000000
matrix_2=000000000000000000000
matrix_3=000000000000000000000
matrix_4=000000000000000000000
numeric_2=16
numeric_3=7

display=5
board=num4x4_matrix5x21
nightId=2026-09-22
numeric_0=20:18
numeric_1=06:07
matrix_0=333320000000000002333
matrix_1=000000000000000000000
matrix_2=000000000000000000000
matrix_3=000000000000000000000
matrix_4=000000000000000000000
numeric_2=15
numeric_3=6
```

This example illustrates the syntax only. Its values are not a coherent
astronomical forecast.

Framing and parsing rules
-------------------------

- Each record is key=value followed by LF (0x0A). A parser may discard a
  preceding CR (0x0D) to tolerate CRLF.
- There are no comments, spaces, quoted strings, or escaped values. One empty
  row follows the header records, before `display=0`, and separates each
  display block.
- Split each record at the first equals sign. Unknown keys must be ignored so
  fields can be added in a later protocol version.
- Header records occur once and in the documented order: `protocol`,
  `configurationId`, `time`, `lastWeatherFetchTime`, `refreshIntervalMinutes`.
- A display=<index> record starts a display block. It is followed by that
  block's records in the documented order.
- Each matrix record contains either 21 slot characters or the single `?`
  character when the entire matrix row is unavailable. Within a 21-character
  row, `?` marks an unavailable individual slot.
- A successful response always has six display blocks, with display indexes 0
  to 5. There is no displayCount or end marker in version 3.
- The device must reject the update if the protocol version is unsupported, a
  required record is malformed or absent, the board is unsupported, display
  indexes are not consecutive, or the HTTP body ends before all required
  records are received. It should retain its previous complete forecast.
- configurationId is restricted by server configuration identifiers and does
  not require escaping.

Field definitions
-----------------

protocol
  Decimal protocol version. The current version is 3, which added the
  blinking levels `a`, `b` and `c` to the matrix cells, the `matrix_4` aurora
  row and the `refreshIntervalMinutes` header record; see Matrix encoding.
  Version 2 (levels and `*` only, no aurora) was retired with it: a device
  accepts only version 3. Version 2 had replaced version 1's on/off cells.

board
  Display format identifier. Version 3 supports only
  `num4x4_matrix5x21`. The board record is repeated in every display block so
  a device can validate each block independently.

configurationId
  Configuration requested in the URL.

time
  Local date and time at which the server rendered the response, in the
  configuration's timezone, formatted as ISO 8601 date-time with a UTC offset
  `YYYY-MM-DDTHH:MM:SS.mmm+HH:MM` (24-hour clock, always three digits of
  milliseconds, the offset always as `+HH:MM` or `-HH:MM`, never `Z`). The
  date and time part is the wall-clock time the device should display,
  already adjusted for DST; the offset is the one in force at that instant
  (`+02:00` for Poland in summer, `+01:00` in winter) and says which zone the
  wall-clock value is in, so the value names one instant even in the hour a
  DST change repeats. The device uses the wall-clock part to set its real-time
  clock; the milliseconds let it compare its clock to about a tenth of a
  second rather than to the second. The clock is read after the forecast is assembled,
  so the value lags the moment the response is sent only by serialization time
  plus network latency. `time` appears only in successful responses, not in
  error payloads.

lastWeatherFetchTime
  Local date and time at which the server last fetched weather successfully,
  as `YYYY-MM-DDTHH:MM:SS+HH:MM` in the configuration's timezone (no
  milliseconds; the UTC offset in force at the fetch, which after a DST change
  differs from the one on `time`). It is the newest `fetchedAt` among the weather items used in
  this response, so it describes the weather source only; other sources get
  their own `<source>FetchTime` record. It is the server's fetch from the
  supplier, not the time the supplier's model ran. `?` means the response
  carries no weather. It appears only in successful responses.

refreshIntervalMinutes
  How often the device should fetch the forecast, in minutes: `360` normally
  (every six hours, at 10 minutes past 00, 06, 12 and 18 local), `60` when
  tonight (display 0) is a storm night with a chance of aurora, so the aurora
  row and its nowcast stay current (every hour at 10 minutes past). The
  server decides it; once set for a night it stays for that night. It
  appears only in successful responses.

display
  Zero-based display index. Display 0 is the current observing night in the
  configuration's timezone; displays 1 to 5 are consecutive nights.

nightId
  Local date in YYYY-MM-DD format for the noon-to-noon observing night.

numeric_0
  Local sunset time in HH:MM, or ? when no sunset occurs.

numeric_1
  Local sunrise time in HH:MM, or ? when no sunrise occurs. This is normally
  the morning after nightId.

matrix_0
  Sun by local-hour slot: how much of the hour the sun is above the horizon.

matrix_1
  Moon by local-hour slot: how much of the hour the moon is above the horizon.

matrix_2
  Total cloud coverage by local-hour slot: `0` for a clear sky, the rest in
  thirds.

matrix_3
  Precipitation probability by local-hour slot, in quarters, with `*` where a
  thunderstorm is predicted.

matrix_4
  Aurora by local-hour slot: the chance of seeing an aurora from the
  configured location, merged from the geomagnetic forecasts and, on storm
  nights, the OVATION nowcast. Blinking marks the hours the nowcast describes
  (now and the next hour). See docs/aurora-forecast-supplier.md for the
  sources and the merge.

numeric_2
  Maximum temperature during the observing night, in whole degrees Celsius,
  with no decimal point. A question mark means weather is unavailable.

numeric_3
  Minimum temperature during the observing night, in whole degrees Celsius,
  with no decimal point. A question mark means weather is unavailable.

Numeric display formats
-------------------------

Every numeric display supports these value formats:

- integer values from `-999` through `9999`;
- fixed-precision numbers from `-999.9` through `999.9`, with a smallest
  representable increment of `0.001`;
- times in `HH:MM` format;
- `?` for unavailable data.

The payload carries values, not segment bitmaps. The server must emit values
within these supported ranges and formats.

For this protocol:

- `numeric_0` and `numeric_1` contain local sunset and sunrise times;
- `numeric_2` and `numeric_3` contain temperatures in whole degrees, such as
  `18` or `-3`.
- `numeric_0` and `numeric_1` use the missing-value sentinel `?` when
  the corresponding astronomical event is unavailable.
- `numeric_2` and `numeric_3` use the missing-value sentinel `?` when
  weather is unavailable.

Matrix encoding
---------------

For board `num4x4_matrix5x21`, each available matrix row has exactly 21
characters, one per LED. A fully unavailable matrix row is represented by the
single `?` character. Character index 0 represents local 14:00-15:00 on
`nightId`. Subsequent indexes represent consecutive full hours. Character
index 20 represents local 10:00-11:00 on the following date. The 11:00-12:00
interval is not displayed.

In physical LED numbering, LED 1 maps to character index 0 and LED 21 maps to
character index 20.

Each character is one LED's brightness level, and optionally blinking:

* `0` means the LED is off.
* `1`, `2` and `3` are increasing brightness levels; `3` is full.
* `a`, `b` and `c` are levels 1, 2 and 3 blinking down: the LED alternates
  between its level and off.
* `*` is the same as `c`: full brightness, blinking. It is kept for the
  precipitation row's thunderstorm.
* `?` means source data is unavailable for the slot; the device shows it off.

Any row may use any of these characters; the table below lists what each
row uses today.

The device maps the levels to its own brightness steps; the payload only
ranks them. What a level means depends on the row:

| Row | `0` | `1` | `2` | `3` | `*` |
|---|---|---|---|---|---|
| `matrix_0` sun and `matrix_1` moon: minutes of the hour above the horizon | 0 | 1-29 | 30-59 | 60 | - |
| `matrix_2` total cloud coverage | 0 % | 1-33 % | 34-66 % | 67-100 % | - |
| `matrix_3` precipitation probability | 0-24 % | 25-49 % | 50-74 % | 75-100 % | thunderstorm predicted, whatever the probability |
| `matrix_4` aurora (Kp relative to the location's `kpMain`) | below `kpMain` − 1 | from `kpMain` − 1: faint, camera | from `kpMain`: naked eye, low | from `kpMain` + 1: bright | - |

In `matrix_4`, `a`/`b`/`c` are levels 1-3 in the hour the nowcast describes
and the one before it: an aurora there now. Slots taken from the 27-day
outlook are `1` ("possible") at most.

For the sun and moon rows, the body's altitude is sampled at the middle of
every minute of the slot, from the slot's start, and the minutes above the
horizon are counted, so a rise or set partway through an hour grades that
hour. All time calculations use the configuration's timezone, including DST
transitions. The protocol still emits 21 wall-clock slots on a DST transition;
the server maps each labeled local hour to the appropriate instant.

For the weather rows, each slot uses the weather record for its corresponding
hour. `?` means the weather record or that hourly value is unavailable; in
`matrix_3` a predicted thunderstorm gives `*` even when the probability is
unavailable.

Missing data
------------

All records remain present even when data is unavailable, preserving a fixed
parser and six-block response:

- missing rise or set time: ?
- missing numerical weather value: ?
- unavailable matrix row: `?`
- unavailable matrix slot in an otherwise available row: `?`

Astronomy and weather degrade independently. An astronomy calculation failure
does not discard available weather; affected times and sun or moon slots use
their unavailable sentinels. Missing, expired, or failed weather retrieval does
not discard available astronomy; weather-derived fields use their unavailable
sentinels. The server fails the request only when it cannot assemble a
trustworthy protocol response at all.

Errors
------

Errors also use text/plain and a small parseable body:

protocol=3
error=configuration_not_found

HTTP status remains authoritative: 404 for an unknown configuration and 500
when no trustworthy protocol response can be assembled. A failure isolated to
astronomy or weather returns 200 with unavailable sentinels for the affected
fields. Error values are stable ASCII identifiers, not human messages.

Transport and integrity
-----------------------

The STM32 can parse one bounded line at a time using a small fixed buffer and
does not need to hold the entire response in RAM.

The endpoint is served over both HTTP and HTTPS. TCP and, when present, the
HTTP Content-Length header detect truncation: the device can reject a body
whose received byte count does not match Content-Length or that is missing
required records, so an application end marker or checksum is not required.

Only HTTPS protects the payload against deliberate modification. Over plain
HTTP, a forecast can be read or altered in transit. This is accepted because
the data is public and non-sensitive; see the API edge section in
architecture.md.
