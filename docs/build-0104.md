# Build0104: runtime option block instead of one kernel per switch

Every experiment until now required a fresh CI build and a boot image flash just
to turn one behaviour on or off: native translation cache, the injected
graphics/HFS compatibility libraries, the persistent console and the diagnostic
traces. The kernel now reads a small option block from the phone, so switches
change on the next boot without touching the kernel.

## Where the block lives

The final 4 KiB of the SYSTEM partition. The existing 16 MiB tail keeps its
layout: the provisioned HFS/cache volume still ends at `partition_size - 16 MiB`,
and the framed console owns `16 MiB - 4 KiB` of the tail. Only the last page is
reserved for the block, so the native-cache volume identity and any learned
translation records stay valid across this upgrade.

```
"B2LOPT1\0"                     8 bytes
FNV-1a-32 of the next 4088     4 bytes
"key=value\n" text, NUL padded 4088 bytes
```

`b2l_options_load()` refuses a block with a wrong magic, wrong size or a broken
checksum, so stale or partially written data never changes behaviour. Every key
is optional: a missing key falls back to the matching `b2l_<key>` boot argument
and then to the built-in default. An erased block therefore reproduces the
previous boot-argument behaviour exactly.

## Options

| key | boot argument fallback | default | effect |
| --- | --- | --- | --- |
| `native_cache` | `b2l_native_cache` | 0 | ARM64e persistent translation cache: apply and learn |
| `graphics_compat` | `b2l_graphics_compat` | 1 | `DYLD_INSERT_LIBRARIES` for SpringBoard, backboardd, Preferences, iconservicesagent |
| `hfs_compat` | `b2l_hfs_compat` | 1 | `DYLD_INSERT_LIBRARIES` for installd and the MobileInstallation helper |
| `system_log` | `b2l_system_log` | 0 | framed persistent console in the SYSTEM tail |
| `diag` | `b2l_diag` | 0 | progress/backtrace traces and `CG_CONTEXT_SHOW_BACKTRACE` |

The kernel prints one line per boot:

```
B2L OPTIONS: block applied; native_cache=0 graphics_compat=1 hfs_compat=1 system_log=1 diag=0
```

`block empty` or `block unavailable error=...` means the boot arguments applied.

## Changing options on the phone

The host helper reads and rewrites the last 4 KiB through recovery `dd`; it
merges with the current block, writes it back with `conv=fsync` and reads it
again for verification:

```
./b2l-opt.sh show
./b2l-opt.sh set native_cache=0
./b2l-opt.sh set system_log=1 diag=1
./b2l-opt.sh clear
```

The change takes effect on the next boot. Nothing else on the phone is written:
SYSTEM BSD devices stay read-only for processes, and the block is only ever read
by the kernel.

## Why this is the next experiment

The 2026-11-18 boot that reached the lock screen used the stock Android SYSTEM
partition, so no native cache existed. Every boot since runs with a provisioned
SYSTEM and `applied=5459` translations learned by different kernel builds, and
the compositor stops after two to three frames. With the block in place the same
image can boot once with `native_cache=1`, then with `native_cache=0`, and the
only difference is the option file.

## Tests

`tests/test-option-file.py` compiles the parser that ships in the kernel and
covers key values, negative values, precedence over boot arguments, the caller
default, and rejection of bad magic, broken checksums and malformed lines.
`tests/test-ufs-aux.c` now asserts that the option block ends at the partition
boundary and that console plus block plus HFS still add up to the partition size.
`tests/test-persistent-log.sh` decodes the shrunk console region.

## Compatibility

`scripts/b2l-log-region.py` decodes both the new 16 MiB - 4 KiB headers and
legacy 16 MiB regions. `scripts/pull-b2l-log.py` keeps reading the last 16 MiB of
SYSTEM, which now includes the block after the last console record.
