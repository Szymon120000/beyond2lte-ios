#!/usr/bin/env python3
"""Exercise the SYSTEM option block parser that the kernel reads at boot."""
from pathlib import Path
import subprocess, tempfile

r = Path(__file__).resolve().parents[1]
source = (r / 'xnu-overlay/bsd/dev/b2l_ufs_disk.c').read_text()
begin = '/* BEGIN B2L OPTION BLOCK */'
end = '/* END B2L OPTION BLOCK */'
region = source[source.index(begin) + len(begin):source.index(end)]


def checksum(payload: bytes) -> int:
    h = 2166136261
    for byte in payload:
        h = ((h ^ byte) * 16777619) & 0xffffffff
    return h


def block(text: str, magic: bytes = b'B2LOPT1\x00', bad_sum: bool = False) -> bytes:
    payload = text.encode()
    body = payload + bytes(4096 - 12 - len(payload))
    total = (checksum(body) ^ (1 if bad_sum else 0)) & 0xffffffff
    return magic + total.to_bytes(4, 'little') + body


def array(name: str, data: bytes) -> str:
    return 'static const unsigned char %s[4096] = {%s};\n' % (
        name, ','.join(str(byte) for byte in data))


harness = '#include "%s"\n' % (r/'xnu-overlay/bsd/kern/b2l_options.h') + r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
static int boot_value, boot_present;
static int boot_calls;
static int PE_parse_boot_argn(const char *name, void *out, int size)
{
	assert(size == (int)sizeof(int));
	(void)name;
	boot_calls++;
	if (!boot_present) return 0;
	memcpy(out, &boot_value, sizeof(boot_value));
	return 1;
}
''' + region + r'''
static unsigned char buf[4096];
static void load(const unsigned char *data)
{
	memcpy(buf, data, sizeof(buf));
	b2l_options_load(buf, sizeof(buf));
}
'''

cases = r'''
int main(void)
{
	boot_present = 0;
	load(valid);
	assert(b2l_option_int("native_cache", "b2l_native_cache", 0) == 0);
	assert(b2l_option_int("graphics_compat", "b2l_graphics_compat", 1) == 0);
	assert(b2l_option_int("system_log", "b2l_system_log", 0) == 1);
	assert(b2l_option_int("diag", "b2l_diag", 0) == 1);
	assert(b2l_option_int("hfs_compat", "b2l_hfs_compat", 1) == 1);
	assert(b2l_option_int("native_cache_restore", "b2l_cache_restore", 0) == 5);
	assert(b2l_option_int("unix_time", "b2l_unix_time", 0) == 1795244400);
	assert(b2l_option_int("bogus", "b2l_bogus", 3) == 3);
	load(bad_sum);
	assert(b2l_option_int("native_cache", "b2l_native_cache", 9) == 9);
	load(bad_magic);
	assert(b2l_option_int("native_cache", "b2l_native_cache", 9) == 9);
	load(tampered);
	assert(b2l_option_int("native_cache", "b2l_native_cache", 9) == 9);
	load(malformed);
	assert(b2l_option_int("native_cache", "b2l_native_cache", 4) == 4);
	assert(b2l_option_int("graphics_compat", "b2l_graphics_compat", 1) == -1);
	assert(b2l_option_int("system_log", "b2l_system_log", 0) == 0);
	assert(b2l_option_int("unix_time", "b2l_unix_time", 7) == 7);
	/* File values win over boot arguments, including an explicit 0. */
	boot_value = 1; boot_present = 1;
	load(valid);
	assert(b2l_option_int("native_cache", "b2l_native_cache", 0) == 0);
	boot_value = 0; boot_present = 1;
	assert(b2l_option_int("system_log", "b2l_system_log", 0) == 1);
	boot_present = 0;
	/* Empty block: boot argument wins, then the caller default. */
	boot_value = 1; boot_present = 1;
	load(malformed);
	assert(b2l_option_int("bogus", "b2l_bogus", 3) == 1);
	boot_present = 0;
	assert(b2l_option_int("bogus", "b2l_bogus", 3) == 3);
	assert(boot_calls >= 1);
	puts("option block: keys, checksum, magic, size, malformed lines, precedence PASS");
	return 0;
}
'''

text = ('unix_time=1795244400\n'
        'native_cache=0\n'
        'graphics_compat=0\n'
        'system_log=1\n'
        'diag=1\n'
        'native_cache_restore=5\n'
        'hfs_compat=1\n')
malformed_text = ('unix_time=9999999999\n'
                  'native_cache = 0\n'
                  'graphics_compat=-1\n'
                  'system_log=1x\n'
                  '# diag=1\n'
                  'diag=\n'
                  '\n')

_tampered = bytearray(block(text))
_tampered[3000] ^= 0xff
tampered = bytes(_tampered)

blocks = {
    'valid': block(text),
    'bad_sum': block(text, bad_sum=True),
    'bad_magic': block(text, magic=b'B2LOPT2\x00'),
    'tampered': tampered,
    'malformed': block(malformed_text),
}

with tempfile.TemporaryDirectory(prefix='b2l-options-') as d:
    tmp = Path(d)
    source_path = tmp / 'test.c'
    binary = tmp / 'test'
    source_path.write_text(
        harness + ''.join(array(name, data) for name, data in blocks.items()) + cases)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(source_path), '-o', str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
