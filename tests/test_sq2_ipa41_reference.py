#!/usr/bin/env python3
"""Compile and execute the actual early compatible guard, without kernel I/O.

The fixture tests only this early branch, not the rest of ipa_probe or LTE.
Accepts pristine, candidate, and independently restored ipa_main.c identically.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

def check(path, compatible):
    source = Path(path).read_text()
    probe = source[source.index('static int ipa_probe('):]
    early = probe[:probe.index('ipa_validate_build();')]
    match = re.search(r'if \(of_device_is_compatible\(dev->of_node, "qcom,sc8180x-ipa"\)\)\s*'
                      r'return dev_err_probe\(dev, -EOPNOTSUPP,\s*"[^"\n]*"\);', early)
    guard = match[0] if match else ''
    if 'qcom,sc8180x-ipa' in source:
        assert guard, 'new compatible must fail before build/power/MMIO initialization'
        assert early.index(guard) < len(early)
        # No bypass parameter and no side effect before this hard refusal.
        assert not re.search(r'clk_|ioremap|readl|writel|dma_|request_irq|module_param', early)
    code = r'''
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
struct device { const char *of_node; };
static int of_device_is_compatible(const char *a, const char *b) { return !strcmp(a,b); }
static int dev_err_probe(struct device *dev, int err, const char *fmt, ...) { (void)dev; (void)fmt; return err; }
static int check(struct device *dev) {
''' + guard + r'''
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    struct device dev = { argv[1] };
    printf("FIXTURE_EARLY_GUARD_RETURN=%d\n", check(&dev));
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='sq2-ipa41-guard-') as d:
        c, exe = Path(d)/'guard.c', Path(d)/'guard'
        c.write_text(code)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-parameter',
                        '-fsanitize=undefined', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe), compatible], check=True)
    print('FIXTURE_ONLY: actual early branch; no clocks/MMIO/DMA; not an LTE/probe success test')

if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: test_sq2_ipa41_reference.py IPA_MAIN_C COMPATIBLE')
    check(*sys.argv[1:])
