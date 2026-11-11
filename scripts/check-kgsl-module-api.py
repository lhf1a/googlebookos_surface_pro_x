#!/usr/bin/env python3
"""Exercise KGSL worker lifecycle and mmap callback under the actual 6.18 wrapper."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def extract(code, name):
    matches = re.findall(r"^(?:static )?(?:int|void|unsigned long)\s+(?:__init )?" +
                         re.escape(name) + r"\([^;{]*\)\n\{.*?^\}", code, re.M | re.S)
    if len(matches) != 1:
        raise ValueError("Expected one function: " + name)
    return matches[0]


def check(source, kernel, output):
    output.mkdir(parents=True, exist_ok=False)
    kgsl = (source / "kgsl.c").read_text()
    init = extract(kgsl, "kgsl_core_init")
    close = extract(kgsl, "kgsl_core_exit")
    assert 'result = kgsl_worker_init();\n\tif (result)' in init
    assert init.index('result = kgsl_worker_init()') < init.index('kgsl_events_init()')
    assert 'err:\n\tkgsl_core_exit();\n\treturn result;' in init
    assert close.index('kgsl_worker_close()') < close.index('kgsl_exit_page_pools()')
    assert 'MODULE_IMPORT_NS("DMA_BUF")' in (source / 'adreno.c').read_text()
    functions = [extract(kgsl, n) for n in ('kgsl_worker_init', 'kgsl_worker_close', 'kgsl_get_unmapped_area')]
    functions.append(extract((kernel / 'mm/mmap.c').read_text(), '__get_unmapped_area'))
    header = output / 'kgsl-module-api-functions.h'
    header.write_text('\n\n'.join(functions) + '\n')
    harness = ROOT / 'tests/kgsl_module_api_test.c'
    command = ['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
               '-I', str(output), str(harness), '-o', str(output / 'check')]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / 'check')], capture_output=True, text=True)
    (output / 'check.log').write_text(result.stdout + result.stderr)
    inputs = [source / 'kgsl.c', source / 'adreno.c', kernel / 'mm/mmap.c', kernel / 'kernel/sched/syscalls.c', header, harness, Path(__file__)]
    report = dict(status='host-tested-not-runtime-validated' if result.returncode == 0 else 'failed',
                  input_sha256={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},
                  command=command, exit=result.returncode, result=result.stdout + result.stderr,
                  limits='Actual KGSL helper/callback and core mmap wrapper; scheduler, VMA selection, LSM and thread execution are models. Core-init/exit integration is source-checked; no concurrent producer/unbind proof.')
    (output / 'RESULT.json').write_text(json.dumps(report, indent=2) + '\n')
    result.check_returncode()
    print(result.stdout, end='')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--kernel-source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.kernel_source.resolve(), args.output.absolute())
