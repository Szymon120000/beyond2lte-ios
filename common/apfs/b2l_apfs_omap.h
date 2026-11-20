/* SPDX-License-Identifier: MIT
 * APFS object-map structures; bounded decoding only, no tree traversal yet.
 */
#ifndef B2L_APFS_OMAP_H
#define B2L_APFS_OMAP_H
#include <stddef.h>
#include <stdint.h>
enum b2l_apfs_omap_result {
 B2L_APFS_OMAP_OK=0, B2L_APFS_OMAP_EINVAL=-40,
 B2L_APFS_OMAP_ECHECKSUM=-41, B2L_APFS_OMAP_ETYPE=-42,
 B2L_APFS_OMAP_EBOUNDS=-43, B2L_APFS_OMAP_EDELETED=-44
};
struct b2l_apfs_omap_info {
 uint64_t oid,xid,tree_oid,snapshot_tree_oid,most_recent_snapshot;
 uint32_t flags,snapshot_count,tree_type,snapshot_tree_type;
};
struct b2l_apfs_omap_key { uint64_t oid,xid; };
struct b2l_apfs_omap_value { uint32_t flags,size; uint64_t paddr; };
int b2l_apfs_decode_omap(const void *block,size_t block_size,
    struct b2l_apfs_omap_info *out);
int b2l_apfs_decode_omap_key(const void *bytes,size_t length,
    struct b2l_apfs_omap_key *out);
int b2l_apfs_decode_omap_value(const void *bytes,size_t length,
    struct b2l_apfs_omap_value *out);
#endif
