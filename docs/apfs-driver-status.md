# APFS driver bring-up (B2L)

## Safety boundary

This branch contains a read-only host parser, not a mountable filesystem.
Do not format, migrate, or boot the device's USERDATA with it. TWRP only needs
to flash artifacts; the eventual XNU filesystem provider must understand APFS.

## Implemented

- Bounded NXSB probe with APFS Fletcher-64 complemented checksum validation.
- Checkpoint descriptor-ring scan for contiguous descriptor areas.
- Checkpoint-map object validation (physical object type, checksum, XID,
  flags, bounded entry count and entry-field sanity).
- Newest candidate NXSB selection, requiring a preceding complete checkpoint
  map with the same XID.
- Container OMAP object header, OMAP key, and OMAP value decoders.
- NXSB metadata extraction for container OMAP physical address, spaceman and
  reaper OIDs, and volume OID array.

## Deliberate limits

This first checkpoint slice supports only a contiguous descriptor ring and a
single-block checkpoint map. It rejects fragmented descriptor areas and
multiple map blocks rather than guessing. It does not yet traverse the
descriptor-area mapping B-tree, aggregate checkpoint-map chains, walk the
OMAP B-tree, resolve OIDs to physical blocks, or decode volume OMAPs. No code
is linked into XNU and no device or APFS image test has been performed.

## Next parser work

1. Fragmented checkpoint-area B-tree resolution and checkpoint-map chains.
2. OMAP B-tree traversal with xid-version selection for virtual OIDs.
3. Container object resolution, volume superblocks and volume OMAPs.
4. Filesystem B-tree traversal and file data extents.
5. Read-only XNU VFS integration and generated APFS image validation.
6. iOS metadata semantics and app-install validation.
7. Writable APFS only after COW, checkpoint publication, flush-ordering and
   crash-recovery tests.

## References

- Apple File System Reference:
  https://developer.apple.com/support/downloads/Apple-File-System-Reference.pdf
- libfsapfs format research:
  https://github.com/libyal/libfsapfs/blob/main/documentation/Apple%20File%20System%20(APFS).asciidoc
