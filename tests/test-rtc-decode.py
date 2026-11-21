#!/usr/bin/env python3
"""Decode the Exynos 9820 RTC registers the same way the platform expert does."""
from pathlib import Path
import subprocess, tempfile

root = Path(__file__).resolve().parents[1]
header = root / 'xnu-overlay/iokit/Kernel/arm/b2l_rtc.h'


def bcd(value: int) -> int:
    return ((value // 10) << 4) | (value % 10)


def raw(y, mo, d, h, mi, s, hi=0):
    return (bcd(s), bcd(mi), bcd(h), bcd(d), bcd(mo), bcd(y % 100), hi)


cases = {
    'epoch_2026_11_21': (raw(2026, 11, 21, 7, 0, 0), 1795244400),
    'epoch_2024_02_29': (raw(2024, 2, 29, 23, 59, 59), 1709251199),
    'epoch_2000_01_01': (raw(2000, 1, 1, 0, 0, 0), 0),
    'epoch_2100_01_01': (raw(2100, 1, 1, 0, 0, 0), 0),
    'bad_month': (raw(2026, 13, 1, 0, 0, 0), 0),
    'bad_day': (raw(2026, 1, 0, 0, 0, 0), 0),
    'bad_hour': (raw(2026, 1, 1, 24, 0, 0), 0),
    'bad_nibble': ((0x6a, 0, 0, 1, 1, 0x26, 0), 0),
    'high_year_byte': (raw(2026, 1, 1, 0, 0, 0, hi=1), 0),
}

decls = []
body = []
for name, (values, expected) in cases.items():
    array = ','.join(str(v) for v in values)
    decls.append('static const uint8_t %s[7] = {%s};\n' % (name, array))
    body.append(
        '\tif (b2l_rtc_decode(%s) != %dULL) { printf("%s: got %%llu\\n", '
        '(unsigned long long)b2l_rtc_decode(%s)); return 1; }\n'
        % (name, expected, name, name))

source = (r'''
#include <stdint.h>
#include <stdio.h>
#include "b2l_rtc.h"
''' + ''.join(decls) + r'''
int main(void)
{
''' + ''.join(body) + r'''
	puts("RTC BCD decode, leap day, range and malformed-register checks PASS");
	return 0;
}
''')

with tempfile.TemporaryDirectory(prefix='b2l-rtc-') as td:
    tmp = Path(td)
    src = tmp / 'rtc.c'
    binary = tmp / 'rtc'
    src.write_text(source)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(header.parent),
                    str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
