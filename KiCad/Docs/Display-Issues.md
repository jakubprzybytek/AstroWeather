# Display Issues

Hardware problems found on the LED displays, with what has been measured, the
likely causes and what is still to check. The design-level issues are in
[Hardware_Review.md](Hardware_Review.md); this page collects
what shows on the displays.

| Issue | Board | Status |
| --- | --- | --- |
| [Matrix column faults around `U505`](#matrix-column-faults-around-u505) | Prototype host board | Open: column 1 stays lit |
| [Off digits and matrix rows can glow](#other-known-issues) | Every board | Not seen on the prototype; next PCB revision |
| [Random contents at power-up](#other-known-issues) | Host board | Fixed on the display boards; host firmware fix open |

## Matrix Column Faults Around `U505`

Faults on the prototype host board's dot matrix, all traced to the area around
`U505`, the SCT2024 that drives matrix columns 1 to 16. Status on 2026-10-01:
**open**. Column 1 stays lit and its brightness sometimes changes.

### What Has Been Seen

| When | Symptom | Finding |
| --- | --- | --- |
| Before 2026-10-01 | Column 16 very bright and lit all the time | A short between `U505` pad 20 (`Out15`, column 16) and pad 21 (`~SCT_EN`, OE/). Removed; column 16 has worked since. |
| 2026-10-01 | Column 1 lit from boot | Power off, in circuit: **200 Ω between `U505` pads 4 and 5**, 2 MΩ between pads 5 and 6. |
| 2026-10-01, during rework of pads 3–6 | Only numeric displays 1 and 2 worked. The rest of the matrix and numeric displays 3 and 4 were dark; column 1 was dark at first and lit a few seconds later | Recovered by itself moments later, with every display working. |
| 2026-10-01, after that | Column 1 lit again, its brightness sometimes changes | Not measured again yet. |

The console was unavailable during these observations (the USB port problem then, see
[Development.md](../../firmware/Docs/Development.md#com-port-troubleshooting)), so
none of the checks with `display` commands below have been run yet.

### How the Drivers Are Wired

The four LED drivers form one shift-register chain. Clock (`SCT_CLK_F`), latch
(`~SCT_LA`, PB6) and output enable (`~SCT_EN`, PB7) are shared by all four:

| Order | Driver | Drives | Data in | Data out |
| --- | --- | --- | --- | --- |
| 1 | `U501` SCT2024 | numeric displays 1 and 2 | `SCT_SDI` from the MCU | `SCT1_SDO` |
| 2 | `U505` SCT2024 | matrix columns 1–16 (`DIS3_1`…`DIS3_16`) | `SCT1_SDO` | `SCT2_SDO` |
| 3 | `U510` SCT2167 | matrix columns 17–21 | `SCT2_SDO` | `SCT3_SDO` |
| 4 | `U513` SCT2024 | numeric displays 3 and 4 | `SCT3_SDO` | `SCT_SDO` (not connected) |

`U505` sits on the bottom side (`B.Cu`) at (101, 54.2), rotated −90°.
Coordinates in this document are KiCad PCB editor millimetres. Its SSOP-24
pads, at 0.635 mm pitch:

| Pad | Net | Pad | Net |
| --- | --- | --- | --- |
| 1 | `GND` | 24 | `SCT2_VDD` (from `+3.3V` through `R505`, 10 Ω) |
| 2 | `SCT1_SDO` (data in) | 23 | `SCT2_REXT` (sets the output current) |
| 3 | `SCT_CLK_F` | 22 | `SCT2_SDO` (data out) |
| 4 | `~SCT_LA` | 21 | `~SCT_EN` |
| 5 | `DIS3_1` (column 1) | 20 | `DIS3_16` (column 16) |
| 6–19 | `DIS3_2`…`DIS3_15` | | |

Column 1 is the first output, next to the latch pin, just as column 16 is the
last output, next to the output-enable pin.

### What the Symptoms Point To

**Column 1 always lit.** The SCT outputs are LED cathode sinks. The latch
line sits low between its short pulses, which is almost all the time, so a
leak from column 1 to it holds the column's cathodes low whatever the data
says. Through 200 Ω that is roughly (3.7 V − about 2 V LED drop) / 200 Ω ≈
8 mA in every row slot. The SCT gives a full-brightness pixel about the same
(3.7–7.5 mA peak by the estimate in
[Hardware_Review.md](Hardware_Review.md)), so the column lights
at full brightness, in all five rows, as soon as the board multiplexes. The
current is not regulated by the SCT and flows through PB6. A leak whose
resistance changes, with temperature or humidity for example, would explain
the changing brightness.

**The temporary blackout.** `U501` kept working, so the shared clock, latch
and enable lines, the MCU and the firmware were all fine. Everything from
`U505` onward went dark, so `U505` stopped shifting or passing on data: a
fault on its data input, its own clock joint, its supply, or the chip itself.
It cleared by itself, which points to something temporary, such as
contamination that dried or a joint that makes contact only some of the time,
rather than a broken part.

### Candidate Spots

The gaps are the smallest edge-to-edge distances between copper of different
nets on the same layer: pads, tracks and vias, computed from
`KiCad/AstroWeather.kicad_pcb`.

#### Column 1 leak and changing brightness

| # | Where | Gap | Notes |
| --- | --- | --- | --- |
| 1 | `U505` pads 4 and 5 | 0.235 mm | Where the 200 Ω was measured. A solder whisker or flux residue. |
| 2 | **Under `U505`'s body, near (99.5, 54.3)** | 0.27 mm | The column 1 track runs alongside the latch track underneath the chip. Flux from two reworks can stay trapped there, where a brush cannot reach and nothing can be seen, and its resistance changes with temperature and humidity. The leading suspect for a leak that survives cleaning and varies. |
| 3 | `U505` itself | – | One output can be left leaky by the earlier OE/ short, when that pin sank unregulated current, or by rework heat. |

Column 1 is also 0.235 mm from column 2 at pads 5 and 6, and their tracks run
0.27 mm apart near (101.2, 55.6). A leak there would make the two columns
show the same pattern; 2 MΩ was measured, so it is not one now.

#### Chain break at `U505`

| # | Where | Gap | Notes |
| --- | --- | --- | --- |
| 4 | `U505` pads 1–3 | 0.235 mm | The data input (pad 2) sits between GND (pad 1) and the clock (pad 3). A leak or bridge to either, or a cracked joint on pad 2 or 3, stops `U505` shifting, so `U505`, `U510` and `U513` go dark together. Next to where the rework was done. |
| 5 | `U505` supply: pad 24 and `R505` near (94.5, 55.6) | 0.235 mm to pad 23 | An open joint on pad 24 or `R505` stops `U505` altogether. A leak from pad 24 to pad 23 (`REXT`) would change the brightness of all 16 columns, not only column 1. |
| 6 | The data line from `U501` pad 22 to `U505`, near (61.9, 57.0) | 0.22 mm to GND vias | Also breaks the chain at `U505` while `U501` works. Unlikely, as it is away from the rework. |

#### Not matching this event, for future reference

| # | Where | Gap | Notes |
| --- | --- | --- | --- |
| 7 | The data line from `U505` pad 22 to `U510`, near (97.6, 60.0) | 0.20 mm to a clock via; 0.23 mm to GND and OE vias | The tightest gap found. A fault here would darken only columns 17–21 and numeric displays 3 and 4, with columns 1–16 working. |
| 8 | The shared clock, latch and enable lines | – | Ruled out for the blackout, since `U501` kept working on the same lines. |

### Checks Still To Do

With the board running, once the console works:

- `display clear`: if column 1 still glows with everything off, it is the
  leak. If it goes dark, part of the changing brightness was real data:
  column 1 is the first hour of the sun row, whose level changes through the
  evening.
- `display test`: every column and all four numeric displays should light.

With the power off:

- Reverse the meter leads on pads 4–5. A reading that is the same in both
  directions is resistive, such as solder or contamination; one that changes
  with polarity goes through silicon, which points to the chip.
- Measure pads 4–5 of `U501` or `U513` for reference. Pad 4 is the same latch
  net there. Megohms would confirm that `U505`'s 200 Ω is a defect, not an
  in-circuit path.
- Inspect pads 1–5 and 20–24 under magnification for lifted leads, dull joints
  and bridges.

### Recommended Repair

Remove `U505` with hot air. Clean its pads and the board under it, especially
around (99.5, 54.3), and look for residue and solder balls. Then measure pad 4
to pad 5 with the chip off:

- **Still low:** the leak is in the board (spots 1 and 2). Clean further, or
  cut and re-route the track.
- **Megohms:** the old chip was leaking.

Fit a new SCT2024 either way. The old one has been reworked twice and had an
output shorted to OE/. Clean the area with IPA and let it dry fully before
power is applied: residue that is still wet conducts, which may explain the
temporary blackout.

## Other Known Issues

Both are described in full in
[Hardware_Review.md](Hardware_Review.md).

- **Off digits and matrix rows can glow (H-3).** The slot P-FETs (`Q501`–`Q505`)
  cannot be fully turned off: their gates are driven from 3.3 V while their
  sources sit at about 3.7 V. A FET with a low threshold conducts enough to
  make "off" digits and rows glow. No glow was visible on the prototype host
  board on 2026-09-24; the fix is planned for the next PCB revision.
- **Random contents at power-up (L-3).** On the host, `MX_GPIO_Init` enables
  the SCT outputs (`SCT_ENABLE`, PB7, low) before any data has been latched,
  which can briefly show whatever the drivers hold. The DisplayController
  already starts PB7 high (blanked) and enables the outputs after the first
  frame is prepared; the host needs the same initial level in its CubeMX
  configuration.
