#!/usr/bin/env python3
"""Compile the actual no-TZ stop helper with failed-reset and partial-unmap faults."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'probe/video/sq2-venus-fw.c').read_text()
begin = source.index('static int sq2_fw_no_tz_stop(')
end = source.index('\n/* Fixed Surface', begin)
stop = source[begin:end]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
struct sq2_venus_fw { bool cpu_touched, mapped; void *no_tz_base, *domain; };
static int reset_error, resets, unmaps;
static size_t unmap_result;
static int sq2_no_tz_stop_cpu(void *base) { (void)base; resets++; return reset_error; }
static size_t iommu_unmap(void *domain, unsigned long start, size_t len)
{ (void)domain; assert(!start && len == 0x500000); unmaps++; return unmap_result; }
#define pr_info(...) ((void)0)
'''
harness += stop
harness += r'''
int main(void) {
 for (int touched=0; touched<2; touched++)
 for (int mapped=0; mapped<2; mapped++)
 for (int reset_fail=0; reset_fail<2; reset_fail++)
 for (int partial=0; partial<2; partial++) {
  struct sq2_venus_fw f={.cpu_touched=touched,.mapped=mapped};
  resets=unmaps=0; reset_error=reset_fail ? -EBUSY : 0;
  unmap_result=partial ? 0x400000 : 0x500000;
  int ret=sq2_fw_no_tz_stop(&f);
  assert(resets==touched);
  if (touched && reset_fail) {
   assert(ret==-EBUSY && !unmaps && f.cpu_touched && f.mapped==mapped);
  } else {
   assert(!f.cpu_touched && unmaps==mapped);
   if (mapped && partial) assert(ret==-EIO && f.mapped);
   else assert(!ret && !f.mapped);
  }
 }
 puts("NO_TZ_LIFETIME_PASS: 16 reset/unmap combinations; failures keep mapping ownership");
}
'''
with tempfile.TemporaryDirectory() as name:
    d = Path(name)
    (d / 'check.c').write_text(harness)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-no-pie', str(d / 'check.c'),
                    '-o', str(d / 'check')], check=True)
    subprocess.run([str(d / 'check')], check=True)
