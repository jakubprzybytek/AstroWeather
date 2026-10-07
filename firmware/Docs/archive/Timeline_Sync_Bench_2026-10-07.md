> Archived 2026-10-07. Current state: [TimelineSync.md](../TimelineSync.md).

# Timeline Sync Bench, 2026-10-07

How the timeline sync's first version behaved on the bench, what was wrong
with it and what was changed. The current rules and their reasons are in
[TimelineSync.md](../TimelineSync.md).

## The first version

The host sent a burst of four syncs at 0, 10, 30 and 60 s and then one every
5 minutes. The board stamped the sync when its last data byte arrived, took a
rate from every interval of 5 s or more, carried the change between two
5-minute rates on over the next interval (the trend), stepped HSITRIM above
2000 ppm and unlocked, asking for a burst, whenever a sync found it more than
10 ms out. On 2026-10-06 two 5-minute syncs found 7.6 and -2.0 ms.

## The measurement

Board `0x11`, hard-reset over SWD while the host kept running, `g_displayStats`
read every 2 s as `DisplayController/tools/stats_log.py` does now, and the
host's console log with the PC's arrival time on every line.

The board locked 51 s after the reset. The 5-minute syncs then found +35,
+36, +32, -49, +9, -3, -18, -17 and +39 ms, five of nine over the 10 ms lock
limit, and HSITRIM moved 8 times in 90 minutes.

## Causes

1. **A correction applied twice.** `stampNow()` took the pending correction
   from the frame record, which `startFrame()` wrote at the frame start and
   `applySync()` did not touch. A sync stamped later in the same frame saw
   "nothing pending" and the board corrected again. The host's poll, one
   minute apart and drifting against the 5-minute syncs, started a burst a few
   milliseconds after a 5-minute sync, so it happened: -16.9 ms became
   +16.5 ms 10 s later (the rate read as -1652 ppm, which stepped the trim),
   then -32.7 ms. Three times in 90 minutes, each costing 40-86 ms.
2. **Burst rates 100-160 ppm off.** Each stamp is a millisecond or two out,
   which is 100-300 ppm over a 10 s interval and 30-50 ppm over 30 s. The
   board replaced its 5-minute rate with the burst's, and the next 5-minute
   interval ended 30-50 ms out, which asked for another burst.
3. **HSITRIM stepping with 350 ppm of margin.** After a step the servo had up
   to 1650 ppm left and the threshold was 2000 ppm.
4. **The stamp after the data bytes.** The host's refresh interrupt runs up to
   850 us and its USB interrupt more, and the polled transfer waits for them
   between bytes: the seven data bytes took 690-1840 us where 630 us was
   assumed.
5. **A frame record not renumbered at a jump.** A sync stamped within the
   frame after a jump carried the old frame number and jumped again.
6. **The jump test on the raw error.** A sync 20 ms after one that found
   144 ms saw most of it still pending and jumped.

## Changes

The board stamps at the address match with the live pending correction, the
record is renumbered with the frames, the jump test takes the pending
correction off, the rate is measured from 25 s, a short interval only nudges
it, the regular interval sets it, the trim steps above 2500 ppm, a miss no
longer asks for a burst, the host's burst is 0/10/40/100 s with a sync sent in
the last 10 s counting as its first, and the interval is 2 minutes.

## The trend

With the first four causes fixed, 5-minute syncs found -14.9, -13.7, +3.6,
-4.9 and +14.7 ms while the board warmed up (the trend helped: it cut the
error to a third at 14:02) and then +89.6 ms: the host's HSI16 had stepped
300 ppm within one interval, and the trend carried that step on into the
next, which found -88.9 ms. A trend halves a ramp's error and doubles a step's;
the rate then moved in steps about as often as in ramps, so the trend was
removed.

## The interval

After `display all test` lit every element at 14:27 the board's rate swung by
+-150 ppm per 5-minute interval for half an hour (+1826, +1680, +1526, +1592,
+1755, +1811, +1891 ppm), and the 5-minute syncs found +44, +46, -20, -49, -17
and -24 ms. The rate being one interval stale, the error scales with the
square of the interval's length, so the interval became 2 minutes. The first
14 regular 2-minute syncs found at worst +16.5 and -22.4 ms, nine of them
within 10 ms, with no burst and no trim step.

## Host clock

In the same windows the host's HSI16 against the PC's clock ran +3164, +3100,
+3052, +3240, +3181, +3067, +3251, +3138 and +3145 ppm in 2-minute windows
(and +2639 to +3252 ppm in 5-minute ones), with nothing in the host's log
at those times. The board's clock against the host's moved the same amount
the other way, so the host is the one wandering.
