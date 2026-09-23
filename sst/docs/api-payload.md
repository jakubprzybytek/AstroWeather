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
protocol=1
configurationId=krakow
time=2026-09-22T23:22:45.678
lastWeatherFetchTime=2026-09-22T18:00:04

display=0
board=num4x4_matrix5x21
nightId=2026-09-17
numeric_0=20:30
numeric_1=05:59
matrix_0=******........*******
matrix_1=****.......**********
matrix_2=.....**********......
matrix_3=......*..............
numeric_2=18.5
numeric_3=9.2

display=1
board=num4x4_matrix5x21
nightId=2026-09-18
numeric_0=20:28
numeric_1=06:01
matrix_0=******........*******
matrix_1=???????********......
matrix_2=...****************..
matrix_3=..............*......
numeric_2=17.8
numeric_3=8.9

display=2
board=num4x4_matrix5x21
nightId=2026-09-19
numeric_0=?
numeric_1=?
matrix_0=?
matrix_1=?
matrix_2=?
matrix_3=?
numeric_2=?
numeric_3=?

display=3
board=num4x4_matrix5x21
nightId=2026-09-20
numeric_0=20:23
numeric_1=06:04
matrix_0=******........*******
matrix_1=.....................
matrix_2=.....................
matrix_3=.....................
numeric_2=16.4
numeric_3=7.5

display=4
board=num4x4_matrix5x21
nightId=2026-09-21
numeric_0=20:21
numeric_1=06:06
matrix_0=******........*******
matrix_1=.....................
matrix_2=.....................
matrix_3=.....................
numeric_2=15.9
numeric_3=6.8

display=5
board=num4x4_matrix5x21
nightId=2026-09-22
numeric_0=20:18
numeric_1=06:07
matrix_0=******........*******
matrix_1=.....................
matrix_2=.....................
matrix_3=.....................
numeric_2=15.2
numeric_3=6.1
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
  `configurationId`, `time`.
- A display=<index> record starts a display block. It is followed by that
  block's records in the documented order.
- Each matrix record contains either 21 slot characters or the single `?`
  character when the entire matrix row is unavailable. Within a 21-character
  row, `?` marks an unavailable individual slot.
- A successful response always has six display blocks, with display indexes 0
  to 5. There is no displayCount or end marker in version 1.
- The device must reject the update if the protocol version is unsupported, a
  required record is malformed or absent, the board is unsupported, display
  indexes are not consecutive, or the HTTP body ends before all required
  records are received. It should retain its previous complete forecast.
- configurationId is restricted by server configuration identifiers and does
  not require escaping.

Field definitions
-----------------

protocol
  Decimal protocol version. The initial version is 1.

board
  Display format identifier. Version 1 supports only
  `num4x4_matrix5x21`. The board record is repeated in every display block so
  a device can validate each block independently.

configurationId
  Configuration requested in the URL.

time
  Local date and time at which the server rendered the response, in the
  configuration's timezone, formatted as ISO 8601 local date-time
  `YYYY-MM-DDTHH:MM:SS.mmm` (24-hour clock, always three digits of
  milliseconds). The device uses it to set its real-time clock; the
  milliseconds let it compare its clock to about a tenth of a second rather
  than to the second. It carries no UTC offset: it is the
  wall-clock time the device should display,
  already adjusted for DST. The clock is read after the forecast is assembled,
  so the value lags the moment the response is sent only by serialization time
  plus network latency. `time` appears only in successful responses, not in
  error payloads. It was added within protocol version 1; a parser that
  predates it ignores it as an unknown header key. The milliseconds were
  added later still: the firmware accepts `time` with or without them.

lastWeatherFetchTime
  Local date and time at which the server last fetched weather successfully,
  as `YYYY-MM-DDTHH:MM:SS` in the configuration's timezone (no milliseconds,
  no UTC offset). It is the newest `fetchedAt` among the weather items used in
  this response, so it describes the weather source only; other sources get
  their own `<source>FetchTime` record. It is the server's fetch from the
  supplier, not the time the supplier's model ran. `?` means the response
  carries no weather. It appears only in successful responses, and was added
  within protocol version 1: a parser that predates it ignores it as an
  unknown header key.

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
  Sun state by local-hour slot.

matrix_1
  Moon state by local-hour slot.

matrix_2
  Total cloud coverage by local-hour slot. A slot is on when total cloud
  coverage is greater than or equal to 10 percent.

matrix_3
  Thunderstorm prediction by local-hour slot.

matrix_4
  Unused in version 1 and omitted from the response. It is reserved for a
  future matrix channel.

numeric_2
  Maximum temperature during the observing night, in degrees Celsius, with one
  decimal place. A question mark means weather is unavailable.

numeric_3
  Minimum temperature during the observing night, in degrees Celsius, with one
  decimal place. A question mark means weather is unavailable.

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
- `numeric_2` and `numeric_3` contain temperatures with one decimal place.
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

Characters have these meanings:

* * means the condition is on for the slot.
* . means the condition is off for the slot.
* ? means source data is unavailable for the slot.

For sun and moon matrices, state is sampled at the midpoint of each slot. This
makes rise/set transitions deterministic when an event occurs partway through
an hour. All time calculations use the configuration's timezone, including DST
transitions. The protocol still emits 21 wall-clock slots on a DST transition;
the server maps each labeled local-hour midpoint to the appropriate instant.

For weather matrices, each slot uses the weather record for its corresponding
hour. In `matrix_2`, `*` means total cloud coverage is at least 10 percent and
`.` means it is below 10 percent. In `matrix_3`, `*` means a thunderstorm is
predicted and `.` means none is predicted. In either matrix, `?` means the
weather record or that hourly value is unavailable.

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

protocol=1
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
