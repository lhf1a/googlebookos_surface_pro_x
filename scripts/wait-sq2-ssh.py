#!/usr/bin/env python3
"""Wait up to ten minutes for authenticated SSH after a normal reboot.

The JSON input is an existing SSH argv, including host verification options.
This tool never sends a reboot or modifies remote state.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time
import uuid


def wait_for_ssh(ssh, previous_boot=None, timeout=600, report=print):
    start = time.monotonic()
    deadline = start + timeout
    attempts = 0
    last_error = ''
    while time.monotonic() < deadline:
        attempts += 1
        try:
            result = subprocess.run(ssh + ['cat /proc/sys/kernel/random/boot_id'],
                capture_output=True, text=True, timeout=min(15, deadline - time.monotonic()))
            boot = result.stdout.strip()
            valid = False
            if result.returncode == 0:
                try:
                    valid = str(uuid.UUID(boot)) == boot and boot != previous_boot
                except ValueError:
                    pass
            if valid:
                event = dict(ready=True, boot_id=boot, attempts=attempts,
                             elapsed_seconds=round(time.monotonic() - start, 1))
                report(event)
                return event
            last_error = result.stderr.strip() or ('old boot still running' if boot == previous_boot else 'invalid boot response')
        except subprocess.TimeoutExpired:
            last_error = 'SSH attempt timed out'
        report(dict(ready=False, pending=True, attempt=attempts,
                    elapsed_seconds=round(time.monotonic() - start, 1), error=last_error))
        time.sleep(max(0, min(10, deadline - time.monotonic())))
    event = dict(ready=False, pending=False, attempts=attempts, timeout_seconds=timeout,
                 elapsed_seconds=round(time.monotonic() - start, 1), error=last_error)
    report(event)
    return event


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssh-json', type=Path, required=True)
    parser.add_argument('--previous-boot')
    parser.add_argument('--timeout', type=float, default=600)
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    ssh = json.loads(args.ssh_json.read_text())
    if not isinstance(ssh, list) or not ssh or not all(isinstance(x, str) for x in ssh):
        parser.error('SSH JSON must be a nonempty argv string list')
    if not 0 < args.timeout <= 3600:
        parser.error('timeout must be between 0 and 3600 seconds')
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open('x') as log:
        def report(event):
            line = json.dumps(event)
            print(line, flush=True)
            log.write(line + '\n')
            log.flush()
        result = wait_for_ssh(ssh, args.previous_boot, args.timeout, report)
    raise SystemExit(0 if result['ready'] else 1)

if __name__ == '__main__':
    main()
