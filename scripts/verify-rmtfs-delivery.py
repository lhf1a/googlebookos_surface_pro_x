#!/usr/bin/env python3
"""Offline validation of opt-in RMTFS delivery observation; never accesses a device.

Runs the same synthetic fault/sequence harness natively with sanitizers and under
AArch64 QEMU. Produces production libraries separately from test instrumentation.
This is not RF, boot, modem-visibility or LTE acceptance evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        parser.error('output exists; choose fresh space so previous evidence is preserved')
    out.mkdir(parents=True)
    src = ROOT / 'probe/lte/rmtfs-delivery-observe.c'
    test = ROOT / 'tests/test_rmtfs_delivery.c'
    records = []
    hashes = {}
    for p in (src, test, Path(__file__)):
        hashes[str(p)] = hashlib.sha256(p.read_bytes()).hexdigest()

    def save():
        (out / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
        (out / 'source-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')

    def run(name, command, env=None):
        command = list(map(str, command))
        result = subprocess.run(command, capture_output=True, timeout=120, env=env)
        (out / (name + '.stdout.txt')).write_bytes(result.stdout)
        (out / (name + '.stderr.txt')).write_bytes(result.stderr)
        (out / (name + '.exit.txt')).write_text(str(result.returncode) + '\n')
        records.append({'name': name, 'command': command, 'exit': result.returncode,
                        'stdout': str(out / (name + '.stdout.txt')),
                        'stderr': str(out / (name + '.stderr.txt')),
                        'environmentOverrides': None if env is None else {
                            k: env[k] for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LD_PRELOAD', 'RMTFS_DELIVERY_OBSERVE', 'RMTFS_DELIVERY_FSG_SHA256') if k in env}})
        save()
        print(f'{name}: exit={result.returncode}', flush=True)
        if result.returncode:
            raise RuntimeError(f'{name} failed; literal output saved in {out}')
        return result.stdout.decode(errors='replace')

    common = ['-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-pthread']
    host = out / 'observer-test-host'
    arm = out / 'observer-test-arm64'
    run('host-compile', ['gcc', *common, '-O1', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', test, '-ldl', '-o', host])
    env = os.environ.copy()
    env.update(ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    native = run('host-test', [host], env)
    run('arm64-compile', ['aarch64-linux-gnu-gcc', *common, '-static', test, '-ldl', '-o', arm])
    emulated = run('arm64-test', ['qemu-aarch64', arm])
    if native != emulated:
        raise RuntimeError('Host and ARM64 harness results differ')
    if 'ALL_TESTS_PASS' not in native:
        raise RuntimeError('Harness did not report complete test run')
    for compiler, arch in [('gcc', 'host'), ('aarch64-linux-gnu-gcc', 'arm64')]:
        lib = out / f'librmtfs-delivery-{arch}.so'
        run(arch + '-production-compile', [compiler, *common, '-fPIC', '-shared',
                                          '-Wl,-z,relro,-z,now', src, '-ldl', '-o', lib])
        symbols = run(arch + '-production-symbols', ['readelf', '-W', '--dyn-syms', lib])
        if not re.search(r'\bGLOBAL\s+DEFAULT\s+\d+\s+pwrite\s*$', symbols, re.M):
            raise RuntimeError(f'{arch} production library does not export pwrite')
        if re.search(r'\b(?:test_|mock_|rdo_test)', symbols):
            raise RuntimeError(f'{arch} production dynamic symbols contain test seam')
        run(arch + '-production-versions', ['readelf', '-W', '--version-info', lib])
        hashes[str(lib)] = hashlib.sha256(lib.read_bytes()).hexdigest()
    smoke_source = out / 'production-pwrite-smoke.c'
    smoke_source.write_text(r'''#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    unsigned char input[512], output[512];
    for (unsigned i=0;i<512;i++) input[i]=(unsigned char)(i*17u+(i>>8));
    int fd=memfd_create("rmtfs-offline-pwrite-smoke",MFD_CLOEXEC);
    if(fd<0)return 2;
    errno=123;
    ssize_t n=pwrite(fd,input,sizeof(input),0);
    int saved=errno;
    ssize_t rd=pread(fd,output,sizeof(output),0);
    int equal=rd==512 && !memcmp(input,output,512);
    close(fd);
    printf("pwrite=%zd errno=%d readback=%zd equal=%d\n",n,saved,rd,equal);
    return !(n==512 && saved==123 && equal);
}
''')
    smoke = out / 'production-pwrite-smoke'
    run('production-smoke-compile', ['gcc', *common, smoke_source, '-o', smoke])
    phase_output = {}
    for phase in ('BASELINE', 'MODIFIED', 'ROLLBACK'):
        smoke_env = os.environ.copy()
        for key in ('LD_PRELOAD', 'RMTFS_DELIVERY_OBSERVE', 'RMTFS_DELIVERY_FSG_SHA256'):
            smoke_env.pop(key, None)
        if phase == 'MODIFIED':
            smoke_env['LD_PRELOAD'] = str(out / 'librmtfs-delivery-host.so')
        phase_output[phase] = run(phase + '-production-smoke', [smoke], smoke_env)
    if len(set(phase_output.values())) != 1:
        raise RuntimeError('Production default-opt-out pwrite A/B/R results differ')
    if phase_output['BASELINE'] != 'pwrite=512 errno=123 readback=512 equal=1\n':
        raise RuntimeError('Unexpected production forwarding result')
    (out / 'production-pwrite-transaction.json').write_text(json.dumps({
        'scope': 'Real libc pwrite on anonymous regular file; production observer default opt-out',
        'phases': phase_output, 'equal': True, 'noDeviceAccess': True}, indent=2) + '\n')
    for p, expected in list(hashes.items()):
        if hashlib.sha256(Path(p).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Source or output changed during verification: {p}')
    save()
    (out / 'result.json').write_text(json.dumps({
        'scope': 'Offline synthetic fixture only; no RF or device acceptance',
        'hostSanitizers': ['ASan', 'UBSan'], 'arm64QEMU': True,
        'hostARM64OutputsEqual': True, 'productionSeparateFromTestSeams': True,
        'productionDefaultOptOutPwriteBaselineModifiedRollbackEqual': True,
        'sourceAndOutputSHA256': hashes, 'status': 'pass'}, indent=2) + '\n')
    print(native, end='')
    print('OFFLINE_ONLY production library built; no hardware changes')


if __name__ == '__main__':
    main()
