#!/usr/bin/env python3
"""Verify original and added files from a raw readback of the deployed USB partition."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
readback = ROOT / 'private/sq2-usb-final-readback-20261026'
destination = Path(tempfile.mkdtemp(prefix='final-readback-', dir='/root/.cache/sq2-usb-deploy-20261026'))
subprocess.run(['mcopy', '-i', str(readback / 'partition2-original.img'), '-s', '::*', str(destination)], check=True)
actual = {p.relative_to(destination).as_posix().casefold(): p for p in destination.rglob('*') if p.is_file()}
original = json.loads((ROOT / 'out/usb-preservation.json').read_text())['originalFiles']
payload = json.loads((ROOT / 'out/usb-payload-manifest.json').read_text(encoding='utf-8-sig'))
for entry in original + payload:
    file = actual[entry['path'].casefold()]
    assert file.stat().st_size == entry['bytes'], entry['path']
    assert hashlib.sha256(file.read_bytes()).hexdigest() == entry['sha256'], entry['path']
assert hashlib.sha256((readback / 'disk-first-1MiB.bin').read_bytes()).hexdigest() == '31dd4a521499b1ae70f30ba7c794c26ff560e3b9f29cbc99cf642e7d70fea1f4'
raw_result = (ROOT / 'out/usb-deployment-result.json').read_bytes()
result = json.loads(raw_result.decode('utf-16' if raw_result.startswith(b'\xff\xfe') else 'utf-8-sig'))
result.update({'rawReadbackFilesVerified': True, 'diskFirstMiBUnchanged': True,
               'hiddenFAT16TypeRestored': True,
               'partitionSHA256': hashlib.sha256((readback / 'partition2-original.img').read_bytes()).hexdigest()})
(ROOT / 'out/usb-deployment-result.json').write_text(json.dumps(result, indent=2)+'\n')
print(f'PASS: raw readback verifies {len(original)} original files, {len(payload)} new files, and unchanged disk first MiB.')
