#!/usr/bin/env python3
"""Execute exact adreno input/unbind bodies with deterministic pthread leaves.

Kernel input synchronization, scheduling and provider cleanup remain models.
The real full probe, device hardware and complete unbind are NOT validated.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def function(text, name):
    match = re.search(r'static void ' + re.escape(name) + r'\([^;{}]*\)\s*\{', text)
    assert match, name
    start = match.start()
    opening = text.index('{', start)
    depth = 1
    pos = opening + 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    source = args.source.resolve().read_text()
    args.out.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    harness = (root / 'tests/kgsl_input_worker_test.c').read_text()
    funcs = '\n\n'.join(function(source, x) for x in
                        ('adreno_input_work', 'adreno_input_event', 'adreno_unbind'))
    # Source wiring, not a claim of executing the full probe:
    probe = source[source.index('int adreno_device_probe('):source.index('static int adreno_bind(')]
    assert probe.index('adreno_setup_device(adreno_dev);') < probe.index('dev_set_drvdata(dev, device);')
    assert 'INIT_WORK(&adreno_dev->input_work, adreno_input_work);' in source
    assert 'dev_set_drvdata(dev, NULL);' in probe[probe.index('\nerr:'):]
    error_tail = probe[probe.index('\nerr:')+len('\nerr:'):probe.rindex('}')]
    input_block = probe[probe.index('#ifdef CONFIG_INPUT'):probe.index('#endif',probe.index('#ifdef CONFIG_INPUT'))+len('#endif')]
    funcs += '\nstatic int probe_error_cleanup(struct device *dev, struct kgsl_device *device, int status) {' + error_tail + '}\n'
    funcs += '\nstatic void probe_input_register(struct kgsl_device *device) {\n' + input_block + '\n}\n'
    variants = {'actual': funcs}
    if args.mutations:
        cancel = '\tcancel_work_sync(&adreno_dev->input_work);'
        assert cancel in funcs
        variants.update({
            'no-cancel': funcs.replace(cancel, ''),
            'late-cancel': funcs.replace(cancel, '').replace('\t/* The memory is fading */', cancel+'\n\t/* The memory is fading */'),
            'cancel-before-unregister': funcs.replace(cancel, '').replace('#ifdef CONFIG_INPUT', cancel+'\n#ifdef CONFIG_INPUT', 1),
            'no-private-clear': funcs.replace('\t\tadreno_input_handler.private = NULL;', '', 1),
            'wrong-owner': funcs.replace('adreno_input_handler.private == device', 'adreno_input_handler.private'),
            'no-unregister': funcs.replace('\t\tinput_unregister_handler(&adreno_input_handler);', ''),
        })
    cases = ['idle', 'pending', 'running', 'running-no-remove', 'register-failed',
             'input-disabled', 'other-owner', 'last-event', 'producer',
             'null-drvdata', 'probe-error', 'nonabs', 'wake-flag', 'no-remove', 'repeat']
    noinput = ['idle', 'pending', 'running', 'running-no-remove', 'register-failed',
               'input-disabled', 'other-owner', 'null-drvdata', 'probe-error', 'no-remove', 'repeat']
    ledger = []
    failed = False
    def run(command, **kw):
        result = subprocess.run(command, text=True, capture_output=True, timeout=20, **kw)
        ledger.append({'command': command, 'stdout': result.stdout, 'stderr': result.stderr,
                       'exit_status': result.returncode})
        return result
    for variant, body in variants.items():
        for enabled in ([True, False] if variant == 'actual' else [True]):
            name = variant + ('-input' if enabled else '-noinput')
            cfile = args.out / (name + '.c')
            binary = args.out / name
            cfile.write_text(harness.replace('/* GENERATED_FUNCTIONS */', body))
            cmd = ['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-Wno-unused-parameter', '-pthread', '-fno-pie', '-no-pie',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            if enabled:
                cmd += ['-DCONFIG_INPUT=1']
            compiled = run(cmd + [str(cfile), '-o', str(binary)])
            if compiled.returncode:
                (args.out / 'LEDGER.json').write_text(json.dumps(ledger, indent=2))
                raise RuntimeError('compile failed: ' + compiled.stderr)
            statuses = []
            for case in (cases if enabled else noinput):
                result = run([str(binary), case], env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
                statuses.append(result.returncode)
            summary = {'variant': name, 'pass': statuses.count(0), 'fail': len(statuses)-statuses.count(0),
                       'compile_status': compiled.returncode}
            print(json.dumps(summary))
            failed |= (any(statuses) if variant == 'actual' else not any(statuses))
    (args.out / 'LEDGER.json').write_text(json.dumps(ledger, indent=2)+'\n')
    (args.out / 'SOURCE.sha256').write_text(hashlib.sha256(args.source.read_bytes()).hexdigest()+'\n')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
