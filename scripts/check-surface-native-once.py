#!/usr/bin/env python3
"""Compile the actual native admission function, with no device operations."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
a = p.parse_args()
s = a.source.read_text()
once = 'static bool sq1_autosuspend_take(' in s
name = 'sq1_autosuspend_take' if once else 'sq1_autosuspend_allowed'
m = re.search(r'^static bool ' + name + r'\([^;{]*\n\{.*?\n}', s, re.M | re.S)
if not m:
    p.error('native admission function missing')
code = r'''
#include <stdbool.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define PM_SUSPEND_TO_IDLE 1
#define TEST_NONE 0
typedef int suspend_state_t;
struct pid { int unused; };
static struct pid first, other;
static bool sq1_screen_only_autosuspend;
static struct pid *sq1_autosuspend_owner, *current;
static int sq1_autosuspend_tgid, pm_test_level, releases;
static long long sq1_autosuspend_deadline, now;
#define task_tgid(x) (x)
#define ktime_get_boottime_seconds() now
#define put_pid(p) (assert((p)==&first), releases++)
''' + m[0] + r'''
#define admit ''' + name + r'''
static void setup(void) {
    sq1_screen_only_autosuspend=true;
    sq1_autosuspend_owner=&first; current=&first;
    sq1_autosuspend_tgid=42; sq1_autosuspend_deadline=220;
    now=100; pm_test_level=0; releases=0;
}
int main(void) {
#ifndef CONFIG_PM_DEBUG
    setup(); assert(!admit(1) && releases==0);
    puts("PM_DEBUG off: admission denied");
#else
    for (int fault=0; fault<5; fault++) {
        setup(); int state=1;
        switch(fault) {
        case 0: sq1_screen_only_autosuspend=false; break;
        case 1: sq1_autosuspend_owner=NULL; break;
        case 2: state=2; break;
        case 3: pm_test_level=1; break;
        case 4: current=&other; break;
        }
        struct pid *owner=sq1_autosuspend_owner;
        assert(!admit(state) && releases==0 && sq1_autosuspend_owner==owner);
    }
    setup(); current=&other; assert(!admit(1)); current=&first;
    assert(admit(1));
    if (ONCE) {
        assert(!sq1_autosuspend_owner && !sq1_autosuspend_tgid);
        assert(!sq1_autosuspend_deadline && releases==1 && !admit(1));
    } else {
        assert(admit(1) && sq1_autosuspend_owner==&first && releases==0);
    }
    for (long long time=219; time<=221; time++) {
        setup(); now=time; assert(admit(1)==(!ONCE || time<220));
        if (ONCE) assert(releases==1 && !sq1_autosuspend_owner && !admit(1));
    }
    setup(); sq1_autosuspend_owner=NULL; sq1_autosuspend_tgid=0;
    assert(!admit(1) && releases==0);
    setup(); assert(admit(1)); /* Explicit fresh registration can admit again. */
    puts(ONCE?"single request, expiry, cancellation, wrong-owner/state/debug guards passed":
              "baseline persistent owner behavior reproduced");
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='native-once-check-') as tmp:
    f=Path(tmp)/'test.c'; binary=Path(tmp)/'test'
    f.write_text(code)
    for debug in (True, False):
        subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                        '-Wno-unused-variable','-O1','-fsanitize=address,undefined',
                        '-fno-pie','-no-pie',f'-DONCE={int(once)}',
                        *(['-DCONFIG_PM_DEBUG=1'] if debug else []),
                        str(f),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
