# Build 0105: ghost-touch kill switch and the battery power source

Status 2026-11-21. Two user-visible gaps from the 0104 boot.

## Input: `touch` option

The digitizer on this unit is damaged and emits contacts with no finger
present. They are indistinguishable from real contacts by coordinate: the
raw sec_ts range is 12-bit on both axes (0..4095), the observed contacts span
almost the whole range, and down/up stay balanced, so a region filter would
only punch holes in the panel.

`B2LInput` now reads the runtime option `touch` (boot argument `b2l_touch`,
default 1):

* `touch=0` never initialises the touch controller and never reads it, so
  nothing reaches backboardd. The power, volume and home GPIO keys are
  unaffected.
* The option is also re-checked every ten seconds, because the option block
  is read from storage and may be applied after this driver starts. A late
  change turns the controller off at runtime.
* `touch=1` (or an absent key) keeps the previous behaviour.

## Power: an `IOPMPowerSource`

0104 had charger and fuel-gauge drivers (`B2LPower`, MAX77705) but no Apple
battery driver, so nothing published a power source and the iOS status bar
had no charging state. `B2LPower` now creates and registers an
`IOPMPowerSource` subclass named `AppleSmartBattery` (the name the closed
driver uses; the class still matches `IOPMPowerSource` queries).

Properties published every five seconds from the existing sample loop:

| key | source |
| --- | --- |
| `BatteryInstalled` | always true |
| `ExternalConnected`, `ExternalChargeCapable` | `CHG_INT_OK` bit 1 (`CHGIN_OK`) |
| `IsCharging` | external and not full and below 60 C |
| `CurrentCapacity` / `MaxCapacity` | gauge SOC percent / 100 |
| `Voltage` | gauge VCELL |
| `Amperage` | gauge current magnitude, positive while charging |
| `FullyCharged`, `AtWarnLevel`, `AtCriticalLevel` | SOC and external state |
| `TimeRemaining` | 120 min while charging |
| `DesignCapacity`, `NominalChargeCapacity`, `CycleCount` | 4100 mAh, 4100 mAh, 0 |

The gauge on this unit reports charge current as a negative number, so the
published `Amperage` follows Apple's sign convention (positive = charging).

`CHG_INT_OK` is also added to the 60-second power line as `int_ok=0x..` so a
wrong cable-presence bit is visible in the next boot.

## Option block

`touch` joins `native_cache`, `graphics_compat`, `hfs_compat`, `system_log`,
`diag` in the last-4 KiB block; `b2l-opt.py` knows its default (1).

## Acceptance

* `B2L INPUT: touch disabled by option` and no `B2L HID: touch` lines.
* `B2L BATTERY: IOPMPowerSource published as AppleSmartBattery` and
  `B2L BATTERY: soc=.. ext=1 chg=1`.
* The iOS status bar shows the charge level and the charging state.
