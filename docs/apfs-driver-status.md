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
- Fixed-key/value OMAP B-tree lookup with checksum validation, offset checks,
  xid-aware branch selection, deleted-value handling and cycle/depth guards.
- NXSB metadata extraction for container OMAP physical address, spaceman and
  reaper OIDs, and volume OID array.

## Deliberate limits

Checkpoint discovery currently supports only contiguous descriptor rings and
single-block checkpoint maps. Fragmented descriptor areas and map chains are
rejected. OMAP traversal currently supports fixed-KV B-tree nodes; variable-KV
nodes, snapshots, encrypted values and unknown object-map features are not
supported. The caller must supply the physical root-node address. No code is
linked into XNU and no device or real APFS image test has been performed.

## Next parser work

1. Fragmented checkpoint-area B-tree resolution and checkpoint-map chains.
2. Resolve the container OMAP root from NXSB and use lookup to resolve
   spaceman, reaper and volume object identifiers.
3. Parse volume superblocks and each volume's OMAP.
4. Filesystem B-tree traversal and file data extents.
5. Read-only XNU VFS integration and generated APFS image validation.
6. iOS metadata semantics and app-install validation.
7. Writable APFS only after COW, checkpoint publication, flush-ordering and
   crash-recovery tests.

## References

- Apple File System Reference:
  https://developer.apple.com/support/downloads/Apple-File-System-Reference.pdf
- libfsapfs format research:
  https://github.com/libyal/libfsapfs/blob/main/documentation/Apple-File-System-Reference.asciidoc
