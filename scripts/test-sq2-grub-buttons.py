#!/usr/bin/env python3
"""Compile the actual old/new EFI translators and check their input contract."""
from pathlib import Path
import importlib.util
import subprocess
import sys
import tempfile

R = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('buttons', R/'scripts/patch-sq2-grub-buttons.py')
p = importlib.util.module_from_spec(spec); spec.loader.exec_module(p)
src = Path(sys.argv[1])
original = (src/'grub-core/term/efi/console.c').read_bytes()
modified = p.patch(original)
for bad in (modified, original+b'\n'):
    try:
        p.patch(bad)
    except ValueError:
        pass
    else:
        raise AssertionError('Unpinned/repeated input accepted')

def extract(data, name):
    s = data.decode()
    s = s[s.index('const unsigned efi_codes[]'):s.index('static int\ngrub_console_getkey_con')]
    return s.replace('efi_codes', name+'_codes').replace('grub_efi_translate_key',name)

defines = '\n'.join(line for line in (src/'include/grub/term.h').read_text().splitlines()
                    if line.startswith('#define GRUB_TERM_'))
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { uint16_t scan_code, unicode_char; } grub_efi_input_key_t;
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
'''+defines+'\n'+extract(original,'baseline')+extract(modified,'candidate')+r'''
static void check(unsigned scan, unsigned unicode) {
  grub_efi_input_key_t key = {scan, unicode};
  int old = baseline(key), now = candidate(key);
  if (unicode == 0 && scan == 0x80) assert(now == GRUB_TERM_KEY_UP);
  else if (unicode == 0 && scan == 0x81) assert(now == GRUB_TERM_KEY_DOWN);
  else if (unicode == 0 && scan == 0x102) assert(now == '\r');
  else assert(now == old);
}
int main(void) {
  const unsigned chars[] = {0,8,9,10,13,31,32,65,127,128,0x4e2d,0xffff};
  for (unsigned scan=0; scan<=0xffff; ++scan)
    for (unsigned j=0; j<ARRAY_SIZE(chars); ++j) check(scan,chars[j]);
  for (unsigned ch=0; ch<=0xffff; ++ch) {
    check(0x80,ch); check(0x81,ch); check(0x102,ch);
  }
  assert(candidate((grub_efi_input_key_t){1,0}) == GRUB_TERM_KEY_UP);
  assert(candidate((grub_efi_input_key_t){2,0}) == GRUB_TERM_KEY_DOWN);
  assert(candidate((grub_efi_input_key_t){13,13}) == '\r');
  assert(candidate((grub_efi_input_key_t){0,13}) == '\r');
  assert(candidate((grub_efi_input_key_t){0,0}) == GRUB_TERM_NO_KEY);
  puts("PASS: actual EFI translator; only three non-printable button keys differ; keyboard/Unicode unchanged");
}
'''
with tempfile.TemporaryDirectory(prefix='sq2-grub-keys-') as td:
    d = Path(td); (d/'test.c').write_text(harness)
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-O2',str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
