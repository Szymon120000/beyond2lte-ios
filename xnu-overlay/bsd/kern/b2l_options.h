/* SPDX-License-Identifier: BSD-2-Clause */
/* Runtime option block. The final 4 KiB of the SYSTEM partition holds a small
 * text block so switches can be changed from the phone instead of rebuilding
 * and reflashing the kernel:
 *
 *   offset 0    "B2LOPT1\0"
 *   offset 8    FNV-1a 32 checksum of the 4088 bytes that follow
 *   offset 12   "key=value\n" lines, NUL padded
 *
 * Recognized keys (decimal integers): native_cache, graphics_compat,
 * hfs_compat, system_log, diag, touch. A key that is absent falls back to the
 * matching b2l_<key> boot argument and then to the caller's default, so an
 * empty or missing block keeps the previous behaviour. */
#ifndef _BSD_KERN_B2L_OPTIONS_H_
#define _BSD_KERN_B2L_OPTIONS_H_
#include <sys/types.h>

#define B2L_OPTION_BLOCK_BYTES 4096u
#define B2L_OPTION_TEXT_OFFSET 12u

void b2l_options_load(const void *block, size_t bytes);
int b2l_option_int(const char *key, const char *bootarg, int fallback);

#endif /* _BSD_KERN_B2L_OPTIONS_H_ */
