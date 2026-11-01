#!/usr/bin/env python3
"""Run the actual extended stream scan against a bounded fake GR0 page."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'probe/video/sq2_venus_smmu_snapshot.c').read_text()
scan = source[source.index(' id0 = readl('):source.index('\nout:\n')]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;
#define BIT(n) (1U<<(n))
static u32 page[1024], reads;
static u32 readl(void *p) {
 uintptr_t n=(unsigned char *)p-(unsigned char *)page;
 assert(!(n&3) && (n==0x20 || (n>=0x800 && n<0xffc)));
 reads++; return page[n/4];
}
#define pr_info(...) ((void)0)
static unsigned scan(void) {
 void *gr0=page;
 u32 id0,count,i,smr,s2cr,mask,matches=0;
 int ret;
'''
harness += scan + '\nout: (void)s2cr; assert(!ret); return matches;\n}\n'
harness += r'''
int main(void) {
 page[0x20/4]=BIT(27)|255;
 page[(0x800+4*14)/4]=BIT(31)|0x2342;
 page[(0x800+4*170)/4]=BIT(31)|0x2342;
 page[(0x800+4*254)/4]=BIT(31)|(0x40<<16)|0x2302;
 page[(0x800+4*169)/4]=BIT(31)|0x2300;
 assert(scan()==3 && reads==1+255+3);
 puts("SMMU_SNAPSHOT_PASS: low/extended exact and masked matches, full bounded read set");
}
'''
with tempfile.TemporaryDirectory() as name:
    d=Path(name)
    (d/'check.c').write_text(harness)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-no-pie',str(d/'check.c'),'-o',str(d/'check')],check=True)
    subprocess.run([str(d/'check')],check=True)
