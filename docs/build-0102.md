# Build 0102: replace stale owned payloads and decouple rendering compatibility

## Problem

The installed 0101 kernel reached userspace but reported
`B2L APPS: native migration bootstrap publication error=22`. Because
`b2l_apps_cache_ready` stayed zero, the exec hook never injected the runtime
compatibility libraries, so `b2l-graphics-compat-v1.dylib` was never loaded
into SpringBoard or backboardd. The known NULL-colorspace rendering failures
therefore remained unfixed even though the code was already present.

The publication path treated any existing owned file that failed the exact
compare as a permanent error. A copy left by a different build (random
`LC_UUID`/size) or by an earlier native-cache rewrite that the allowlist could
not certify wedged publication on every subsequent boot.

## Change

- `b2l_apps_publish` now replaces a stale owned final copy: it removes the
  owned file and its owned staging link, then republishes from the embedded
  payload. Unrelated paths and directories are never removed; a directory at
  an owned path is refused with `EPERM`.
- A staging file whose prefix no longer matches is discarded and rewritten
  once, instead of returning `EIO` forever.
- Publication of the executable and both compatibility dylibs now sets a
  separate `b2l_compat_ready` flag. The exec hook gates injection on that flag,
  not on the full migration job/launch-cache success. A rejected launch cache
  can no longer disable rendering compatibility.
- Each publication step logs its own failure (`directory`, `executable`,
  `hfs-compat`, `graphics-compat`, `job`, `launch-cache`) so the next capture
  identifies the exact stage.

The runtime libraries are unchanged. The kernel still requires
`b2l_userspace_bootstrap=1`, a root-owned writable HFS root, and the existing
`b2l_amfi_active()` gate. Native cache translations that the production
converter can certify still compare equal and are not rewritten.

## Tests

`tests/test-apps-publish.py` exercises the actual `b2l_apps_remove`,
`b2l_apps_sync_payload` and `b2l_apps_publish` code over an in-memory FS under
ASan/UBSan: first publication, idempotent replay, stale-content replacement,
stale-mode replacement, stale staging-prefix recovery and directory refusal.
Existing seed, payload-comparator and persona tests still pass.

## Runtime expectation

A fresh capture must show `B2L APPS: runtime compatibility libraries published`
and `B2L COMPAT: runtime compatibility library enabled for
/System/Library/CoreServices/SpringBoard.app/SpringBoard` (and backboardd).
If the migration job or launch cache is still rejected, the generic
`publication error=` line identifies that stage while rendering compatibility
remains active.
