#!/usr/bin/env python3
"""Execute real SDE release statements and Linux kfree_sensitive/explicit erase.

Allocator, KASAN and final kfree leaves are models. This checks statement/API
semantics, not complete callers, destructor ownership, hardware or DMA retirement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def extract(text, name):
    m = re.search(r'^(?:static inline )?void ' + name + r'\([^;{]+\)\s*\{', text, re.M)
    if not m:
        raise ValueError('Missing real function ' + name)
    end, level = m.end(), 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--kernel-source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--expected-sites', type=int, default=21,
                        help='Pinned complete donor has21 release sites; reject missing or extra sites')
    args = parser.parse_args()
    source, kernel = args.source.resolve(), args.kernel_source.resolve()
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    base = source / 'techpack/display/msm' if (source / 'techpack/display/msm').is_dir() else source
    sites, inputs = [], {}
    for path in sorted(base.rglob('*.c')):
        text = path.read_text()
        found = list(re.finditer(r'\b(kzfree|kfree_sensitive)\(\s*([a-zA-Z_]\w*)\s*\);', text))
        if found:
            inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
            for m in found:
                sites.append(dict(path=str(path), line=text.count('\n', 0, m.start())+1,
                                  statement=m.group(), variable=m.group(2)))
    if args.expected_sites <= 0 or len(sites) != args.expected_sites:
        (output/'RESULT.json').write_text(json.dumps(dict(
            status='rejected-site-count', expected=args.expected_sites, actual=len(sites),
            sites=sites, inputs=inputs, runs=[], hardware_validated=False),indent=2)+'\n')
        print('Rejected release-site count:',len(sites),'expected',args.expected_sites)
        return 2
    slab = kernel / 'mm/slab_common.c'
    string = kernel / 'include/linux/string.h'
    bodies = extract(string.read_text(), 'memzero_explicit') + '\n' + extract(slab.read_text(), 'kfree_sensitive')
    prelude = r'''
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static unsigned char memory[4112];
static size_t allocated;
static unsigned freed, unpoisoned, barrier_hits;
static void *active;
static size_t ksize(const void *p) { if ((uintptr_t)p<=16) return 0; assert(p==active); return allocated; }
static void kasan_unpoison_range(void *p,size_t n) { assert(p==active && n==allocated && !freed); unpoisoned++; }
static void barrier_leaf(void *p) { assert(p==active && !freed); barrier_hits++; }
#define barrier_data(p) barrier_leaf(p)
static void kfree(const void *p) {
    assert(++freed==1);
    if ((uintptr_t)p<=16) { assert(!unpoisoned && !barrier_hits); return; }
    assert(p==active && unpoisoned==1 && barrier_hits==1);
    for(size_t i=0;i<allocated;i++) assert(((const unsigned char *)p)[i]==0);
    for(size_t i=0;i<8;i++) assert(memory[i]==0xa5 && memory[8+allocated+i]==0xa5);
}
'''
    wrappers = []
    for i, site in enumerate(sites):
        wrappers.append('static void call%d(void *input) { void *%s=input; %s }' % (i,site['variable'],site['statement']))
    tail = r'''
int main(void) {
    size_t sizes[]={1,8,17,32,48,64,512,4096}; unsigned cases=0;
    for(unsigned c=0;c<sizeof(calls)/sizeof(calls[0]);c++) {
        for(unsigned s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++) {
            memset(memory,0xa5,sizeof(memory)); allocated=sizes[s];active=memory+8;
            freed=unpoisoned=barrier_hits=0; calls[c](active);
            assert(freed==1);cases++;
        }
        for(unsigned z=0;z<2;z++) {
            active=(void *)(uintptr_t)(z?16:0);allocated=0;
            freed=unpoisoned=barrier_hits=0;calls[c](active);assert(freed==1);cases++;
        }
    }
    printf("PASS %u sensitive-free cases across %zu actual statements; allocator/KASAN leaves modeled\n",cases,sizeof(calls)/sizeof(calls[0]));
    return 0;
}
'''
    code = prelude+bodies+'\n'+'\n'.join(wrappers)+'\nstatic void (*calls[])(void *)={' + ','.join('call%d'%i for i in range(len(sites))) + '};\n'+tail
    unit, binary = output/'probe.c', output/'probe'
    unit.write_text(code)
    runs = []
    for cmd in [['gcc','-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-pie','-no-pie',str(unit),'-o',str(binary)], [str(binary)]]:
        r = subprocess.run(cmd,capture_output=True,text=True,timeout=30)
        runs.append(dict(command=cmd,exit=r.returncode,stdout=r.stdout,stderr=r.stderr))
        if r.returncode:
            break
    for p in [slab,string,Path(__file__)]:inputs[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
    ok=len(runs)==2 and all(r['exit']==0 for r in runs)
    (output/'RESULT.json').write_text(json.dumps(dict(inputs=inputs,sites=sites,runs=runs,pass_all=ok,hardware_validated=False,scope=__doc__),indent=2)+'\n')
    print('sensitive-free', 'pass' if ok else 'fail', 'sites',len(sites))
    return 0 if ok else 1


if __name__=='__main__':
    raise SystemExit(main())
