---
name: sync-docs
description: Bring the AstroWeather documentation in line with the code at the end of a major chunk of work (a feature, a refactor, a bench investigation, before pushing). Finds the docs the change affects, rewrites them to the current state, moves history to archive folders, keeps the README feature tables and the console help text in step, and runs the link and structure checks. Use when the user says "sync docs", "update the docs", "are the docs up to date", or when a large change is about to be committed.
---

# Sync Docs

Make the docs describe the code as it is now, in the agreed shape. Work from
the code, never from the old docs: every number, name and priority you write
is checked against the source, the `.ioc` or the built ELF.

## The Shape

### Where documents live

| Content | Place |
| --- | --- |
| Shared firmware docs: development workflow (build, flash, USB CDC connection, COM-port troubleshooting, CubeMX rules), the `Common` code, I2C, the native test kit | `firmware/Docs/` (`README.md`, `Development.md`, `Display.md`, `I2C.md`, `Testing.md`, `Utilities.md`) |
| One firmware project's docs | `firmware/<Project>/Docs/` (`HostControllerA`, `DisplayController`, `Bypass`) |
| Each project's entry point | `firmware/<Project>/README.md`; `firmware/Common/README.md` points at `firmware/Docs` |
| Hardware: design review, display faults, purchasing, BOM | `KiCad/Docs/` with its `README.md` |
| Plans, investigations, measurement logs, superseded designs | the `archive/` folder next to the current doc |
| Server | `sst/docs/` (its own convention; only check its links unless the server changed) |

Folders are `Docs`, never `docs`. A topic documented once for both boards
(display code, I2C) lives in `firmware/Docs`; a project doc keeps only what is
specific to that project and links the shared one.

### What a current document says

- The state of the code now: what it does, how, with which values, and why
  where the reason is not obvious (one or two sentences).
- No road to it: no "until 2026-10-05", "used to", "was fixed", "turned out",
  phases, steps of an investigation, or withdrawn suspicions. Those go to the
  archive.
- Dates only as data: when a measurement was taken ("measured on the
  2026-10-06 Debug build"), prices checked, or inside sample output.
- Open items are short and current; each project README collects them in one
  list.
- Write plainly, in full sentences, matching the surrounding docs.

### Archive

- A history-only document moves whole into `archive/` (use `git mv`); history
  inside a current document is cut out into a new archive file.
- Every archive file starts, after its title, with
  `> Archived <YYYY-MM-DD>. Current state: [<Doc>.md](<relative link>).`
- Each `archive/README.md` lists every file in its folder.
- Archive text is otherwise frozen: fix only links that moved.

### Project READMEs

`HostControllerA/README.md` and `DisplayController/README.md` each have:

- `## Features`: a status table (status legend at the top) in which every row
  links the document that describes it, or says why there is none.
- `## Known Limitations and Open Items`: one list, linking each doc's own open
  items.
- `## Documentation`: this project's docs, then the shared ones in
  `firmware/Docs`, then hardware.

### Facts that appear in several places

Change all of them together:

| Fact | Places |
| --- | --- |
| Interrupt priorities (host: USB 1, rest 3; display board: I2C1 1, rest 3) | both `Architecture.md` (`#interrupt-priorities`), `HostControllerA/Docs/Console.md#usb-device`, `firmware/Docs/I2C.md#interrupt-priority`, `firmware/Docs/Development.md` CubeMX table, both READMEs |
| Console commands and replies | `HostControllerA/Docs/Console.md` and the help text in `User/Src/Console/HelpCommand.cpp` (the help is documentation too) |
| Task stacks, object sizes, RAM | `HostControllerA/Docs/Architecture.md` (tasks table), `Firmware-RAM-Usage.md`, `Console.md` fixed limits |
| CubeMX settings that must survive regeneration | `firmware/Docs/Development.md#cubemx-compliance`, the `Architecture.md` pin tables |
| Error log size, log line length | `Console.md` (error log, fixed limits), `ErrorLog.hpp`, help text |
| Hardware issue IDs (C-1, H-1, ...) | `KiCad/Docs/Hardware_Review.md`, cited from the READMEs and feature docs |

## Procedure

### 1. Find what changed

```bash
git log --oneline -20
git diff --stat <base>..HEAD      # base: the commit before the work chunk, or ask
git status --short                # uncommitted work counts too
```

List the changed areas and map them to documents:

| Code | Documents |
| --- | --- |
| `firmware/Common/Src/Display/**`, `Device/SCT2xxx` | `firmware/Docs/Display.md` |
| `DisplayI2cProtocol`, `DisplayAddress`, `Device/I2cBus`, `BufferedDisplayBoard`, `DisplayController/User/*I2c*`, `FrameAssembler` | `firmware/Docs/I2C.md` |
| `firmware/Common/Src/Utils/**`, `Common/Src/Debug/**` | `firmware/Docs/Utilities.md` |
| `*/tests/**`, `NativeTest.cmake`, presets, CI workflow | `firmware/Docs/Testing.md`, the project's `Testing.md` |
| `*.ioc`, linker scripts, `USER CODE` sections, `CMakeLists.txt` | `firmware/Docs/Development.md#cubemx-compliance`, `Architecture.md` (boot, pins, priorities) |
| `HostControllerA/User/Src/Console/**`, `Debug/LogService`, `Debug/ErrorLog` | `Console.md`, `HelpCommand.cpp` |
| `User/Src/Display/**`, `Astro/AstroDisplayMapper`, `LowBrightness` | `HostControllerA/Docs/Display.md`, `AstroRefresh.md` |
| `User/Src/Astro/**` | `AstroRefresh.md` |
| `User/Src/WiFi/**`, `Appli/App/*` | `WiFi.md` |
| `User/Src/Clock/**` | `RTC.md` |
| `User/Src/Settings/**`, `Device/Eeprom*` | `Settings.md` |
| `User/Src/Sensors/**` | `CurrentSense.md` |
| `Task<N>` sizes, new static objects, heap | `Architecture.md` tasks table, `Firmware-RAM-Usage.md` |
| `DisplayController/User/**` | `DisplayController/Docs/Architecture.md` |
| `firmware/Bypass/**` | `firmware/Bypass/Docs/` |
| `KiCad/**` | `KiCad/Docs/` |

Then add the cross-cutting facts from the table above that the change touched,
and the README feature rows and open items of every affected project.

### 2. Read the code, then the docs

For each affected document, read the changed code first and note the facts the
doc must state. Then read the doc section and mark what is missing, wrong or
historical. Verify values at the source:

```bash
grep -n "HAL_NVIC_SetPriority" firmware/*/Core/Src/*.c firmware/HostControllerA/USB_Device/Target/usbd_conf.c
grep -n "NVIC\." firmware/*/*.ioc
arm-none-eabi-size -A firmware/HostControllerA/build/Debug/HostControllerA.elf
arm-none-eabi-nm -S -C --size-sort firmware/HostControllerA/build/Debug/HostControllerA.elf | tail -40
```

Stack headroom comes from the board (`stats on`), not from guesses; if the
board is not available, say the figure is unmeasured.

### 3. Rewrite

- Edit the current docs to the present state, in place. Keep section anchors
  that other documents or code comments link to; if one must change, update
  every link to it.
- Move any history found on the way to the archive, as above.
- Shared vs project: if the change made something common to both boards, move
  its description to `firmware/Docs` and leave a link behind.
- Update the README feature tables, statuses and open items, and the
  documentation index when a document is added or moved.
- Keep the console help text and `Console.md` identical in substance.
- Code comments, test headers and tools that cite a document by path must
  still point at it.

### 4. Check

From the repository root:

```bash
python .claude/skills/sync-docs/scripts/doc_audit.py .            # structure: must report 0 findings
python .claude/skills/sync-docs/scripts/linkcheck.py .            # links and anchors outside archives
python .claude/skills/sync-docs/scripts/linkcheck.py . --all      # including archives
python .claude/skills/sync-docs/scripts/doc_audit.py . --history  # lines to judge by eye
```

- `doc_audit.py` checks the folder layout, the archive headers and indexes,
  the README sections and feature-table links, and the document paths cited
  in code, tests and tools.
- `linkcheck.py` resolves every relative link and `#anchor`. The known
  pre-existing failure is `sst/docs/api.md` → `api-payload.md#matrix-encoding`.
- `--history` lists lines in current docs that look like history or plans.
  Judge each: sample output and measurement dates are fine; narrative goes to
  the archive.

If code, help text or comments changed, build every affected project and run
the native tests (see `firmware/Docs/Development.md`; the native builds need
`/c/Progs/msys64/ucrt64/bin` on `PATH`, or they fail silently and `ctest`
runs stale binaries). Flash the host only if the user's workflow calls for it.

### 5. Finish

- Commit the docs in the root checkout on `main`, separately from code
  changes when practical, with a message saying which documents changed and
  why. Push only when the user asks.
- If a memory note cites a moved document, update its path.
- Report: the documents updated, what was archived, facts corrected, the
  check results, and anything left open (for example a figure that needs a
  measurement on the board).
