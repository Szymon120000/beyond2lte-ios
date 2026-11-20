# TWRP incremental USERDATA updates

The delta package compares a prepared base root with a new prepared root and
contains only changed/new regular files. At install time the recovery updater
hashes the packaged files and compares each destination file against the
expected new SHA-256. Files already at the expected hash are skipped. Changed
files are staged beside their destination, verified, and renamed into place.
Removed files are intentionally retained. Changed/new symlinks are rejected by
the packager rather than silently omitted.

## Build

```sh
python3 scripts/package-userdata-delta.py BASE_ROOT NEW_ROOT out/beyond2lte-userdata-delta.zip
```

Install the resulting ZIP from TWRP. The updater resolves only the GPT
`by-name/userdata` node and requires the HFS+ volume to mount read-write.
It aborts before modifying files if the HFS+ driver is absent, the partition is
ambiguous, or the mount is read-only.

## Important HFS+ / TWRP limitation

The CI-created B2LRoot volume is journaled HFS+ (`newfs_hfs -J`). Linux/TWRP
HFS+ drivers commonly cannot write journaled HFS+ volumes. Therefore this
updater is deliberately fail-closed: a failed or read-only mount means **zero
files are changed**. Do not disable journaling, reformat USERDATA, or try a
raw block-delta workaround to bypass this check. Actual write support must be
validated on the exact TWRP build and target device before a successful
incremental install can be claimed.

This is a file-level updater, not a line-based patcher. Binary files must be
replaced as whole files. It does not flash BOOT, format USERDATA, or delete
files absent from the new tree.
