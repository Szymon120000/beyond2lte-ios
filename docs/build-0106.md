# Build 0106: charging presence bit and touch sniff mode

Status 2026-11-21. Two reports from the 0105 boot: the status bar showed
"charging" permanently even with the cable removed, and touch was disabled
completely instead of having its ghost contacts filtered.

## Charging: the wrong CHG_INT_OK bit

`B2LPower` published `ExternalConnected` from `CHG_INT_OK & 0x02`. Bit 1 of
`CHG_INT_OK` is **DISQBAT_OK** ("discharge allowed"), not input power. It is
set whenever the pack may discharge, so the battery service reported external
power on every sample and iOS kept the bolt on after unplugging.

Samsung's `max77705_charger.h` defines the register explicitly:

```
MAX77705_CHGIN_OK      0x40   /* bit 6: charger input present */
MAX77705_DISQBAT_OK    0x02   /* bit 1: discharge allowed */
MAX77705_CHG_DTLS_00   CHGIN_DTLS 0x60, shift 5  (0 = none, 1 = USB, 2 = ...)
```

So the fix is one mask: `external = (chgIntOk & 0x40) != 0`. The detail field
`CHG_DETAILS_00[6:5]` is logged alongside as an independent check so a future
regression is visible in the console. `CHG_DETAILS_00` is now sampled with the
same cadence as `CHG_INT_OK`.

Log line:

```
B2L POWER: soc=.. vcell=.. current=.. int_ok=0x.. details=0x.. ext=.. dtls_ext=..
```

`power_dbg=1` in the option block makes the line print on every 5 s tick
instead of every 60 s, which is what the unplug test needs.

## Touch: sniff-only mode for characterizing the ghosts

`touch=0` remains the kill switch: no controller initialization, no HID events,
GPIO keys only. Two new options let the controller run for diagnosis without
ever moving the UI:

| key | default | effect |
| --- | --- | --- |
| `touch_sniff` | 0 | Initialize and poll `sec_ts`, parse records, publish nothing to backboardd. |
| `touch_log` | 0 | Dump raw 8-byte coordinate records (bounded) to the persistent console. |

`touch_sniff=1` overrides `touch=0`; it exists to answer "what do the ghost
contacts look like". `touch_log=1` logs the first 200 records and every 20th
record up to 400, so a deliberate gesture can be picked out of a flood. Each
record carries the full wire frame, the parsed X/Y, pressure, action, touch id,
touch type and the remaining-FIFO count:

```
B2L TOUCH RAW: t=<us> n=<seq> b=<8 bytes> x=.. y=.. p=.. act=.. id=.. type=.. rem=.. res=..
```

Every 10 s a cumulative summary is written:

```
B2L TOUCH SAMPLE: seen=.. logged=.. press=.. move=.. release=.. ignored=.. pmin=.. pmax=..
```

The runtime option re-check now also understands sniff mode: if the block is
read after `B2LInput` starts, `touch=0` still disables, but `touch_sniff=1`
keeps the controller alive in log-only mode.

The next step is to boot with `touch=0 touch_sniff=1 touch_log=1`, perform a
known gesture (slow diagonal swipe, then a tap), pull the console, and compare
pressure/type/sequence of coherent tracks against the random contacts. Only
then is a filter rule committed; no filter ships on guesswork.

## Acceptance

- Unplug the cable while running: `int_ok` loses bit 6, `ext` goes to 0 and the
  iOS status bar stops showing charging within a few samples.
- `touch=0 touch_sniff=1 touch_log=1`: `B2L TOUCH RAW` and `B2L TOUCH SAMPLE`
  lines appear, no `B2L HID: touch` traffic, no UI movement from the digitizer.
- `touch=0` alone: no touch initialization and no raw lines.
