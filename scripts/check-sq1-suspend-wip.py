#!/usr/bin/env python3
"""Check the archived SQ1 patch series on temporary copies; never deploy it."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path, help='historical source layout in series.json')
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    bundle = Path(__file__).resolve().parents[1] / 'patches/sq1-suspend-wip'
    entries = json.loads((bundle / 'series.json').read_text())['entries']
    records = []

    def run(command, cwd=None, expected=0, env=None):
        command = [str(x) for x in command]
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
        records.append(dict(command=command, exit=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        check(result.returncode == expected, json.dumps(records[-1]))
        return result.stdout

    with tempfile.TemporaryDirectory(prefix='sq1-suspend-check-') as directory:
        tree = Path(directory)
        originals = {}
        for entry in entries:
            target = PurePosixPath(entry['target'])
            check(not target.is_absolute() and '..' not in target.parts, 'unsafe target')
            check(Path(entry['patch']).name == entry['patch'], 'unsafe patch name')
            check(sha(bundle / entry['patch']) == entry['patch_sha256'], 'patch hash mismatch')
            if entry['target'] not in originals:
                source = (baseline / target).resolve()
                check(source.is_relative_to(baseline), 'baseline path escapes supplied root')
                check(sha(source) == entry['before_sha256'], 'baseline hash mismatch: ' + str(target))
                originals[entry['target']] = sha(source)
                dest = tree / target
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, dest)

        def gate_test(label):
            source = (tree / 'kernel/kernel/power/suspend.c').read_text()
            def function(name):
                start = source.index('static bool ' + name + '(')
                return source[start:source.index('\n}', start) + 2]
            modified = 'static bool sq1_trial_take(' in source
            prologue = '''
#include <stdbool.h>
#include <stdio.h>
#define CONFIG_PM_DEBUG 1
#define PM_SUSPEND_TO_IDLE 1
#define TEST_NONE 0
#define TEST_FREEZER 1
#define TEST_DEVICES 2
#define TEST_PLATFORM 3
typedef int suspend_state_t;
static bool sq2_screen_only_test=true, sq2_screen_only=true;
static bool sq1_screen_only_trial=true;
static int pm_test_level=0, sq1_trial_pid=42, current=42;
static unsigned long jiffies=1, sq1_trial_deadline=500;
#define task_pid_nr(x) (x)
#define time_after(a,b) ((long)((b)-(a))<0)
#define pr_info(...) printf(__VA_ARGS__)
'''
            policy = (function('sq2_screen_only_test_allowed') + '\n' + function('sq1_trial_take')) if modified else '''
static bool sq2_screen_only_test_allowed(int state) { return false; }
static bool sq1_trial_take(int state) { return false; }
'''
            body = '''
static int allowed(void) {
 return !sq2_screen_only || sq2_screen_only_test_allowed(1) || sq1_trial_take(1);
}
int main(void) {
 int first=allowed(), second=allowed();
 printf("first=%d second=%d guard=%d\\n",first,second,sq2_screen_only);
 return 0;
}
'''
            cfile = tree / (label + '.c')
            cfile.write_text(prologue + policy + body)
            executable = tree / label
            run(['gcc', '-O2', cfile, '-o', executable])
            output = run([executable])
            check(('first=1 second=0 guard=1' if modified else 'first=0 second=0 guard=1') in output,
                  'gate behavior mismatch')

        def bridge_test(label, modified):
            executable = tree / (label + '-bridge')
            run(['gcc', '-O2', tree / 'probe/sq2-hci-bridge.c', bundle / 'hci-handoff-fixture.c',
                 '-Wl,--wrap=socket,--wrap=ioctl,--wrap=bind,--wrap=sleep,--wrap=posix_openpt',
                 '-o', executable])
            output = run([executable, '0', '/dev/test-h4'], expected=0 if modified else 1,
                         env=dict(os.environ, CASE='success'))
            check(('downs=0 binds=3' if modified else 'downs=1 binds=1') in output, 'handoff mismatch')
            if modified:
                for case in ['denied', 'busy']:
                    output = run([executable, '0', '/dev/test-h4'], expected=1,
                                 env=dict(os.environ, CASE=case))
                    check('downs=0' in output, 'unexpected forced controller shutdown')

        def ssam_policy_test(label, modified):
            source = (tree / 'probe/power-input-boot').read_text()
            run(['sh', '-n', tree / 'probe/power-input-boot'])
            start = source.index('for module in ')
            start = source.index('\ndone', start) + len('\ndone')
            end = source.index('\nfor n in ', start)
            # Exercise only the exact boot wake-policy block, without loading
            # modules or starting Android/watchers. Driver default is disabled.
            block = source[start:end]
            fixture = tree / (label + '-sysfs') / 'serial1-0'
            (fixture / 'power').mkdir(parents=True)
            (fixture / 'modalias').write_text('of:Nsurface-aggregatorT(null)Csurface,aggregator\n')
            (fixture / 'power/wakeup').write_text('disabled\n')
            script = tree / (label + '-policy.sh')
            script.write_text('set -eu\n' + block.replace('/sys/bus/serial/devices', str(fixture.parent)) +
                              '\ncat "' + str(fixture / 'power/wakeup') + '"\n')
            check(run(['sh', script]).strip() == ('disabled' if modified else 'enabled'),
                  'SSAM wake policy fixture mismatch')

        gate_test('BASELINE')
        bridge_test('BASELINE', False)
        ssam_policy_test('BASELINE', False)
        for entry in entries:
            dest = tree / entry['target']
            check(sha(dest) == entry['before_sha256'], 'series input mismatch')
            run(['patch', '--batch', '--fuzz=0', '-p1', '-i', bundle / entry['patch']], cwd=tree)
            check(sha(dest) == entry['after_sha256'], 'series output mismatch')
        gate_test('MODIFIED')
        bridge_test('MODIFIED', True)
        ssam_policy_test('MODIFIED', True)
        for entry in reversed(entries):
            dest = tree / entry['target']
            check(sha(dest) == entry['after_sha256'], 'rollback input mismatch')
            run(['patch', '--batch', '--fuzz=0', '-R', '-p1', '-i', bundle / entry['patch']], cwd=tree)
            check(sha(dest) == entry['before_sha256'], 'rollback output mismatch')
        gate_test('ROLLBACK')
        bridge_test('ROLLBACK', False)
        ssam_policy_test('ROLLBACK', False)
        for name, digest in originals.items():
            check(sha(tree / name) == digest and sha(baseline / name) == digest,
                  'rollback mismatch or original modified')
    print(json.dumps(dict(scope='host patch roundtrip, extracted gate and mocked HCI only; not physical wake',
                          result='PASS', commands=records), indent=2))


if __name__ == '__main__':
    main()
