/* SPDX-License-Identifier: MIT */
#ifndef B2L_UFS_AUX_H
#define B2L_UFS_AUX_H
#include "b2l_ufs_core.h"
#include "../kern/b2l_options.h"
#define B2L_SYSTEM_LOG_BYTES (16ULL * 1024 * 1024)
/* Runtime option block: the final 4 KiB of SYSTEM, carved out of the log tail
 * so the provisioned HFS/cache volume keeps its exact geometry. */
struct b2l_ufs_aux_layout { uint64_t partition_base,partition_size,hfs_size,log_base,log_size,opt_base,opt_size; unsigned index; };
int b2l_ufs_aux_layout(const struct b2l_gpt *,unsigned,uint32_t,uint64_t,struct b2l_ufs_aux_layout *);
int b2l_ufs_aux_hfs(const unsigned char *,const unsigned char *,uint64_t);
#endif
