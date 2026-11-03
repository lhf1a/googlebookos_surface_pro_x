#!/usr/bin/env python3
"""Compile exact framework-owner admission predicate; no device writes."""
from pathlib import Path
import argparse, subprocess, tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);a=p.parse_args()
s=a.source.read_text();start=s.find('static bool sq1_autosuspend_allowed(')
candidate=start>=0
function=s[start:s.index('\n}',start)+2] if candidate else 'static bool sq1_autosuspend_allowed(int state) { return false; }'
c=r"""
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define CONFIG_PM_DEBUG 1
#define PM_SUSPEND_TO_IDLE 1
#define TEST_NONE 0
typedef int suspend_state_t;
static bool sq1_screen_only_autosuspend=true;
static void *sq1_autosuspend_owner=(void*)42;
static int pm_test_level=TEST_NONE;
static void *current=(void*)42;
#define task_tgid(x) (x)
"""+function+r"""
int main(void) {
 int first=sq1_autosuspend_allowed(1), second=sq1_autosuspend_allowed(1);
 current=(void*)43;
 int other=sq1_autosuspend_allowed(1);
 printf("owner_first=%d owner_second=%d other=%d\n",first,second,other);
 assert(first=="""+str(int(candidate))+r""" && second=="""+str(int(candidate))+r""" && !other);
"""
if candidate:
    c+=r"""
 current=(void*)42; sq1_screen_only_autosuspend=false; assert(!sq1_autosuspend_allowed(1));
 sq1_screen_only_autosuspend=true; sq1_autosuspend_owner=NULL; assert(!sq1_autosuspend_allowed(1));
 sq1_autosuspend_owner=(void*)42; pm_test_level=1; assert(!sq1_autosuspend_allowed(1));
 pm_test_level=0; assert(!sq1_autosuspend_allowed(2));
 puts("owner_edges=4 PASS");
"""
c+='return 0; }'
with tempfile.TemporaryDirectory() as d:
    f=Path(d)/'test.c';exe=Path(d)/'test';f.write_text(c)
    subprocess.run(['gcc','-O2',str(f),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
