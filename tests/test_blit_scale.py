#!/usr/bin/env python3
"""Check the nearest-neighbour 4:3 and 2:1 scanout bridges.

The compositor renders at 1080x2280 and B2LFramebuffer upscales into the
1440x3040 panel. These host checks pin the pixel mapping and the bounds
gates; hardware timing is measured separately.
"""
from pathlib import Path
import subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include "B2LBlit.h"

int main()
{
    uint32_t src[4 * 4], dst[6 * 6];
    for (unsigned i = 0; i < 16; ++i) src[i] = 0x10000000u + i;
    memset(dst, 0xee, sizeof dst);

    /* A 3x3 source rect at the top-left covers 4x4 panel pixels. */
    assert(b2l_blit_43(dst, sizeof dst, 6, 6, 6 * 4, src, sizeof src, 4, 4, 4 * 4, 0,
        0, 0, 0, 0, 3, 3));
    const uint32_t expected[4][4] = {
        {0x10000000, 0x10000000, 0x10000001, 0x10000002},
        {0x10000000, 0x10000000, 0x10000001, 0x10000002},
        {0x10000004, 0x10000004, 0x10000005, 0x10000006},
        {0x10000008, 0x10000008, 0x10000009, 0x1000000a},
    };
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            assert(dst[y * 6 + x] == expected[y][x]);
    /* The rest of the panel must stay untouched. */
    for (unsigned y = 0; y < 6; ++y)
        for (unsigned x = 0; x < 6; ++x)
            if (x >= 4 || y >= 4) assert(dst[y * 6 + x] == 0xeeeeeeeeu);

    /* 2:1 doubles every source pixel. */
    uint32_t s2[4] = {1, 2, 3, 4}, d2[6 * 6];
    memset(d2, 0xee, sizeof d2);
    assert(b2l_blit_21(d2, sizeof d2, 6, 6, 6 * 4, s2, sizeof s2, 2, 2, 2 * 4, 0,
        0, 0, 0, 0, 2, 2));
    const uint32_t e2[4][4] = {{1, 1, 2, 2}, {1, 1, 2, 2}, {3, 3, 4, 4}, {3, 3, 4, 4}};
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            assert(d2[y * 6 + x] == e2[y][x]);

    /* Offsets are respected and out-of-bounds sources must be rejected. */
    memset(dst, 0xee, sizeof dst);
    assert(b2l_blit_43(dst, sizeof dst, 6, 6, 6 * 4, src, sizeof src, 4, 4, 4 * 4, 0,
        1, 1, 0, 0, 2, 2));
    assert(dst[0] == src[1 * 4 + 1]);
    assert(!b2l_blit_43(dst, sizeof dst, 6, 6, 6 * 4, src, sizeof src, 4, 4, 4 * 4, 0,
        3, 3, 0, 0, 2, 2));
    assert(!b2l_blit_43(dst, 144, 6, 6, 24, src, 8, 4, 4, 4 * 4, 0,
        0, 0, 0, 0, 3, 3));
    assert(!b2l_blit_43(dst, sizeof dst, 6, 6, 24, src, sizeof src, 4, 4, 4 * 4, 0,
        0, 0, 6, 0, 1, 1));
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'check.cpp'
    src.write_text(code)
    exe = Path(tmp) / 'check'
    subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I', str(ROOT / 'xnu-overlay/iokit/Kernel/arm'),
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: 4:3 and 2:1 nearest-neighbour scanout mapping, offsets and bounds (ASan/UBSan)')
