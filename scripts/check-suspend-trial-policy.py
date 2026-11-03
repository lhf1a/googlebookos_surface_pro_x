#!/usr/bin/env python3
"""Compile and exercise the exact suspend admission predicates; no device writes."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def extract(source, name):
    start = source.index('static bool ' + name + '(')
    return source[start:source.index('\n}', start) + 2]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    a = p.parse_args()
    source = a.source.read_text()
    candidate = 'static bool sq1_trial_take(' in source
    policy = ('\n'.join(extract(source, n) for n in
              ('sq2_screen_only_test_allowed', 'sq1_trial_take'))) if candidate else ''
    guard = source[source.index('int pm_suspend('):].split('if (', 1)[1].split(') {', 1)[0]
    admission = 'false'
    if candidate:
        start = source.index('if (sq2_screen_only && !sq2_screen_only_test_allowed(state)')
        condition = source[start + 4:source.index(') {', start)]
        admission = '!(' + condition + ')'
    program = r"""
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#define CONFIG_PM_DEBUG 1
#define PM_SUSPEND_TO_IDLE 1
#define TEST_NONE 0
#define TEST_FREEZER 1
#define TEST_DEVICES 2
#define TEST_PLATFORM 3
#define TEST_CPUS 4
typedef int suspend_state_t;
static bool sq2_screen_only=true, sq2_screen_only_test=true, sq1_screen_only_trial=true;
static int sq1_trial_pid=42, current=42, pm_test_level=TEST_NONE;
static unsigned long jiffies=1, sq1_trial_deadline=500;
#define task_pid_nr(x) (x)
#define time_after(a,b) ((long)((b)-(a))<0)
#define pr_info(...) ((void)0)
""" + policy + r"""
static bool allowed(int state) {
 if (""" + guard + r""") return false;
 return """ + admission + r""";
}
static void reset(void) {
 sq2_screen_only=true; sq2_screen_only_test=true; sq1_screen_only_trial=true;
 sq1_trial_pid=42; current=42; pm_test_level=TEST_NONE;
 jiffies=1; sq1_trial_deadline=500;
}
int main(void) {
 int first=allowed(1), second=allowed(1);
 printf("first=%d second=%d guard=%d\n",first,second,sq2_screen_only);
 assert(first==""" + str(int(candidate)) + r""" && second==0 && sq2_screen_only);
"""
    if candidate:
        program += r"""
 reset(); sq2_screen_only_test=false; assert(!allowed(1));
 reset(); sq1_screen_only_trial=false; assert(!allowed(1));
 reset(); current=43; assert(!allowed(1) && sq1_trial_pid==42);
 reset(); jiffies=501; assert(!allowed(1) && sq1_trial_pid==0);
 reset(); sq1_trial_pid=0; assert(!allowed(1));
 reset(); assert(!allowed(2) && sq1_trial_pid==42);
 for (int level=TEST_FREEZER; level<=TEST_PLATFORM; level++) {
   reset(); pm_test_level=level; assert(allowed(1));
 }
 reset(); pm_test_level=TEST_CPUS; assert(!allowed(1));
 reset(); sq2_screen_only_test=false; sq1_screen_only_trial=false;
 pm_test_level=TEST_PLATFORM; assert(!allowed(1));
 puts("edge_cases=11 PASS; extracted policy only, not physical suspend");
"""
    program += 'return 0;\n}\n'
    with tempfile.TemporaryDirectory(prefix='suspend-policy-') as d:
        c = Path(d) / 'policy.c'; binary = Path(d) / 'policy'
        c.write_text(program)
        subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', str(c), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    main()
