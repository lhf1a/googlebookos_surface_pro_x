"""Rename the sole registration import in a pinned private allocator copy.

Without the explicit probe DSO this copy cannot link; it cannot register the
default service using the original imported function. Never install as a HAL.
"""
import hashlib
from pathlib import Path
import argparse

p = argparse.ArgumentParser()
p.add_argument('source', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--needed-stub', action='store_true', help='Load libs.so via DT_NEEDED for launchers that clear LD_PRELOAD')
a = p.parse_args()
b = a.source.read_bytes()
if hashlib.sha256(b).hexdigest() != '7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da':
    raise SystemExit('Original allocator hash mismatch')
old = b'AServiceManager_addServiceWithFlags\0'
new = b'SQ2_allocator_probe_registration\0'
if b.count(old) != 1 or len(new) > len(old):
    raise SystemExit('Registration import mismatch')
result = b.replace(old, new.ljust(len(old), b'\0'))
if a.needed_stub:
    if result.count(b'libm.so\0') != 1:
        raise SystemExit('libm dependency mismatch')
    # The probe DSO must itself retain libm.so as DT_NEEDED.
    result = result.replace(b'libm.so\0', b'libs.so\0')
with a.output.open('xb') as f:
    f.write(result)
print(hashlib.sha256(result).hexdigest())
