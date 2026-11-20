# APFS driver bring-up (B2L)

## Safety boundary

This branch contains a read-only host parser, not a mountable filesystem.
Do not format, migrate, or boot the device's USERDATA with it. TWRP only needs
to flash artifacts; the eventual XNU filesystem provider must understand APFS.

## Implemented parser layers

- Bounded NXSB probe with APFS Fletcher-64 complemented checksum validation.
- Checkpoint descriptor-ring discovery for contiguous descriptor areas.
- Fragmented descriptor-area lookup through a fixed-KV B-tree whose leaf values
  are physical ranges; range arithmetic and node traversal are bounded.
- Checkpoint-map validation and multi-block chain aggregation, including
  physical object type, checksum, XID, LAST flag, entry count and entry bounds.
- Newest internally consistent NXSB selection, with matching container UUID,
  block geometry and XID; exports container OMAP address, spaceman/reaper OIDs
  and volume OID array.
- Container OMAP object header, OMAP key/value decoding and fixed-KV OMAP
  B-tree lookup with xid-aware branch selection, deleted-value reporting,
  checksum/offset validation, cycle detection and a 32-node depth limit.
- Synthetic tests for contiguous and fragmented checkpoint descriptors,
  checkpoint-map validation, OMAP structures and a fixed-KV OMAP leaf lookup.

## Deliberate limits

This is still host-side parser code and is not linked into XNU. B-tree traversal
currently accepts fixed-KV nodes only. Variable-KV trees, encrypted values,
snapshots and unknown feature combinations are not supported. The descriptor
mapping B-tree root and OMAP B-tree root are expected as physical block
addresses from the caller; real-image validation is still required. No device
mount, APFS write path or USERDATA migration has been attempted.

## Next work

1. Validate parser behavior against independent APFS images and a real iOS
   26.6.2 container; resolve the exact OMAP-root address semantics.
2. Add variable-KV node support and unsupported-feature policy.
3. Resolve container metadata and volume superblocks, then each volume OMAP.
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
