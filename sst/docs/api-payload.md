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

display=0
board=num4x4_matrix5x21
nightId=2026-09-17
numerical_0=20:30
numerical_1=05:59
matrix_0=******........*******
matrix_1=****.......**********
matrix_2=.....**********......
matrix_3=......*..............
numerical_3=18.5
numerical_4=9.2

display=1
board=num4x4_matrix5x21
nightId=2026-09-18
numerical_0=20:28
numerical_1=06:01
matrix_0=******........*******
matrix_1=???????********......
matrix_2=...****************..
matrix_3=..............*......
numerical_3=17.8
numerical_4=8.9

display=2
board=num4x4_matrix5x21
nightId=2026-09-19
numerical_0=--:--
numerical_1=--:--
matrix_0=?????????????????????
matrix_1=?????????????????????
matrix_2=?????????????????????
matrix_3=?????????????????????
numerical_3=-
numerical_4=-

display=3
board=num4x4_matrix5x21
nightId=2026-09-20
numerical_0=20:23
numerical_1=06:04
matrix_0=******........*******
matrix_1=.....................
matrix_2=.....................
matrix_3=.....................
numerical_3=16.4
numerical_4=7.5

display=4
board=num4x4_matrix5x21
nightId=2026-09-21
numerical_0=20:21
numerical_1=06:06
matrix_0=******........*******
matrix_1=.....................
matrix_2=.....................
matrix_3=.....................
numerical_3=15.9
numerical_4=6.8

display=5
board=num4x4_matrix5x21
nightId=2026-09-22
numerical_0=20:18
numerical_1=06:07
matrix_0=******........*******
matrix_1=.....................
matrix_2=.....................
matrix_3=.....................
numerical_3=15.2
numerical_4=6.1
```

This example illustrates the syntax only. Its values are not a coherent
astronomical forecast.

Framing and parsing rules
-------------------------

- Each record is key=value followed by LF (0x0A). A parser may discard a
  preceding CR (0x0D) to tolerate CRLF.
- There are no comments, spaces, quoted strings, escaped values, or blank lines
  on the wire.
- Split each record at the first equals sign. Unknown keys must be ignored so
  fields can be added in a later protocol version.
- Header records occur once and in the documented order.
- A display=<index> record starts a display block. It is followed by that
  block's records in the documented order.
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

display
  Zero-based display index. Display 0 is the current observing night in the
  configuration's timezone; displays 1 to 5 are consecutive nights.

nightId
  Local date in YYYY-MM-DD format for the noon-to-noon observing night.

numerical_0
  Local sunset time in HH:MM, or --:-- when no sunset occurs.

numerical_1
  Local sunrise time in HH:MM, or --:-- when no sunrise occurs. This is normally
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

numerical_2
  Unused in version 1. It is intentionally absent and reserved for the physical
  display channel with that number.

numerical_3
  Maximum temperature during the observing night, in degrees Celsius, with one
  decimal place. A hyphen means weather is unavailable.

numerical_4
  Minimum temperature during the observing night, in degrees Celsius, with one
  decimal place. A hyphen means weather is unavailable.

Numerical display formats
-------------------------

Every numerical display supports these value formats:

- integer values from `-999` through `9999`;
- fixed-precision numbers from `-999.9` through `999.9`, with a smallest
  representable increment of `0.001`;
- times in `HH:MM` format.

The payload carries values, not segment bitmaps. The server must emit values
within these supported ranges and formats.

For this protocol:

- `numerical_0` and `numerical_1` contain local sunset and sunrise times;
- `numerical_3` and `numerical_4` contain temperatures with one decimal place.
- `numerical_0` and `numerical_1` use the missing-value sentinel `--:--` when
  the corresponding astronomical event is unavailable.
- `numerical_3` and `numerical_4` use the missing-value sentinel `-` when
  weather is unavailable.

Matrix encoding
---------------

For board `num4x4_matrix5x21`, each matrix has exactly 21 characters, one per
LED. Character index 0 represents local 14:00-15:00 on `nightId`. Subsequent
indexes represent consecutive full hours. Character index 20 represents local
10:00-11:00 on the following date. The 11:00-12:00 interval is not displayed.

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

- missing rise or set time: --:--
- missing numerical weather value: -
- unavailable matrix slot: ?

An astronomy calculation failure fails the request. Missing or expired weather
does not fail it; weather-derived fields use their unavailable sentinels.

Errors
------

Errors also use text/plain and a small parseable body:

protocol=1
error=configuration_not_found

HTTP status remains authoritative: 404 for an unknown configuration and 500
for an assembly failure. Error values are stable ASCII identifiers, not human
messages.

Implementation assessment
-------------------------

This protocol is practical to implement. The Lambda can calculate six astronomy
windows, query six WEATHER records from DynamoDB, aggregate min/max temperature,
build fixed-size matrices, and serialize the result with string joins. The STM32
can parse one bounded line at a time using a small fixed buffer and does not need
to hold the entire response in RAM.

HTTPS/TCP and HTTP Content-Length already provide transport integrity. The
device can reject a syntactically truncated body when the received byte count
does not match Content-Length or when required records are missing, so an
application end marker or checksum is not required while payloads remain inside
HTTP.

The endpoint can be implemented with the current line protocol.
