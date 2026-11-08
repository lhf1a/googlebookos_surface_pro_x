#!/usr/bin/env python3
"""Bound diagnostic logs and extend SSH startup without changing the 6.18 drivers."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pack', ROOT/'scripts/pack-sq2-lts-audio-ssam.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
sha = lambda b: hashlib.sha256(b).hexdigest()

def build(base, logger):
    before = pack.cpio.entries(gzip.decompress(base))
    assert any(n.startswith('lib/modules/6.18.60-sq2-android1/') for n in before)
    assert logger[:6] == b'\x7fELF\x02\x01' and int.from_bytes(logger[18:20], 'little') == 183
    old = b'mkdir -p "$logdir" || fail log-directory\nmount --bind "$logdir" /logs || fail logs-bind\nboot-kmsg /logs/kernel-live.txt || fail kmsg-recorder'
    source = (ROOT/'probe/test2/init-googlebook-native').read_bytes()
    start = source.index(b'if mkdir -p "$logdir"')
    end = source.index(b'\nfi\n# Normal-boot', start)
    new = source[start:end]
    init = before['init'][1]
    assert init.count(old) == 1
    init = init.replace(old, new)
    ssh = (ROOT/'probe/test2/ssh-framework-boot').read_bytes()
    for script in (init, ssh):
        subprocess.run(['sh', '-n'], input=script, check=True)
    changes = {'init': (before['init'][0], init),
               'bin/boot-kmsg': (before['bin/boot-kmsg'][0], logger),
               'bin/ssh-framework-boot': (before['bin/ssh-framework-boot'][0], ssh)}
    raw = b''.join(pack.cpio.entry(n, b, m) for n, (m, b) in changes.items())
    raw += pack.cpio.entry('TRAILER!!!', b'', 0)
    raw += b'\0' * (-len(raw) % 512)
    result = base + gzip.compress(raw, mtime=0) + pack.credential_tail(base)
    assert pack.cpio.entries(gzip.decompress(result)) == before | changes
    return result, changes

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'logger', 'output'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--expected-base-sha256', required=True)
    args = parser.parse_args()
    base = args.base.read_bytes()
    assert sha(base) == args.expected_base_sha256
    result, changes = build(base, args.logger.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('xb') as out:
        out.write(result)
    report = dict(base_sha256=sha(base), sha256=sha(result), bytes=len(result),
                  changed_entries=sorted(changes), kernel_modules_credentials_unchanged=True,
                  logger_max_bytes=8*1024*1024, logger_free_reserve_bytes=32*1024*1024,
                  ssh_wait_seconds=600, deployed=False)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
if __name__ == '__main__':
    main()
