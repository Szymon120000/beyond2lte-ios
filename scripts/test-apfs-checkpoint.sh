#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC="${CC:-cc}"
CFLAGS="-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"
"$CC" $CFLAGS -I"$ROOT/common/apfs" \
  "$ROOT/common/apfs/b2l_apfs_probe.c" \
  "$ROOT/common/apfs/b2l_apfs_checkpoint.c" \
  "$ROOT/common/apfs/b2l_apfs_omap.c" \
  "$ROOT/tests/apfs/test_apfs_checkpoint.c" -o /tmp/b2l-apfs-checkpoint-test
/tmp/b2l-apfs-checkpoint-test
rm -f /tmp/b2l-apfs-checkpoint-test
