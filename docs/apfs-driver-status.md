# APFS driver bring-up (B2L)

## Scope and safety boundary

The branch `feat/apfs-xnu-driver` starts a native XNU APFS implementation.
TWRP is deliberately outside the filesystem-driver boundary: it only needs
to flash the selected boot artifact. XNU itself must understand and mount
USERDATA.

This work is **not yet a mountable filesystem** and must not be used to
format, migrate, or boot the device's USERDATA partition.

## Implemented first slice

`common/apfs/b2l_apfs_probe.[ch]` provides a bounded, read-only NXSB probe:

- reads the candidate partition through a caller-supplied bounded block reader;
- validates the NXSB signature, object type, block-size range and geometry;
- checks that the declared container extent fits the candidate partition;
- verifies the APFS Fletcher-64 object checksum;
- returns the container UUID and feature flags.

The parser has no allocation, no global state, and no write path. It is kept
outside the kernel overlay until the standalone tests and the XNU integration
can be reviewed independently.

## Required next milestones

1. **Host tests:** synthetic valid/invalid NXSB blocks, checksum corruption,
   integer overflow, short partition and short-read cases.
2. **Checkpoint discovery:** scan the checkpoint descriptor area, validate
   checkpoint-map objects and select the newest internally consistent NXSB.
3. **Container object map:** resolve volume-superblock OIDs safely.
4. **Volume object map and filesystem tree:** B-tree traversal, inode,
   directory, extent, xattr and sibling-link records.
5. **Read-only VFS:** vnode operations and correct stat/lookup/readdir/read
   semantics, first against generated macOS APFS images.
6. **iOS metadata compatibility:** ownership, modes, xattrs, resource forks,
   hard links, clone semantics and compression.
7. **Writable APFS:** copy-on-write object updates, transaction/checkpoint
   publication, flush ordering and crash recovery. No write enablement before
   power-loss and corruption tests.
8. **B2L root selection:** register the APFS VFS provider and select the
   validated USERDATA partition without changing the existing HFS default.
9. **Native app installation:** test the actual iOS staging path and its
   desired-state/signature decisions after the filesystem semantics work.

## Compatibility constraints

The first implementation targets the unencrypted APFS container format
generated for the selected iOS 26.6.2 IPSW. It must explicitly reject unknown
incompatible feature bits. Encrypted containers, snapshots, sealed system
volumes, fusion layouts and newer format revisions remain unsupported until
their behavior is implemented and tested. The current B2L development
environment does not provide SEP-backed data protection.

## References

- Apple File System format reference:
  https://developer.apple.com/support/downloads/Apple-File-System-Reference.pdf
- Independent format research and test oracle:
  https://github.com/libyal/libfsapfs
- The libfsapfs project describes itself as experimental and read-only for
  supported APFS v2 formats; it is a validation reference, not a kernel
  dependency.
