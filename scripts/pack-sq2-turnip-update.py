#!/usr/bin/env python3
"""Replace only Turnip in the verified microphone image; never writes a device."""
import argparse, gzip, hashlib, importlib.util, json, zlib
from pathlib import Path

spec = importlib.util.spec_from_file_location('cpio', Path(__file__).with_name('pack-sq2-vulkan.py'))
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('base', type=Path)
p.add_argument('driver', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--driver-sha256', required=True)
a = p.parse_args()
assert not a.output.exists()
sha = lambda b: hashlib.sha256(b).hexdigest()
base = a.base.read_bytes()
assert sha(base) == '9dd3ff7a00f75cf5862a176042f7563a52aa50d7bb67b95ed2c9a58117a2959e'
before = cpio.entries(gzip.decompress(base))
driver = a.driver.read_bytes()
assert sha(driver) == a.driver_sha256
assert driver[:6] == b'\x7fELF\x02\x01' and int.from_bytes(driver[18:20], 'little') == 183
name = 'etc/googlebook-security/lib64/hw/vulkan.freedreno.so'
assert name in before and str(Path(name).parent) in before
rest, credential = base, None
while rest:
    dec = zlib.decompressobj(31)
    files = cpio.entries(dec.decompress(rest))
    assert dec.eof
    if 'etc/shadow' in files and 'bin/dropbear' in files:
        credential = rest[:len(rest)-len(dec.unused_data)]
    rest = dec.unused_data
assert credential
archive = cpio.entry(name, driver, before[name][0]) + cpio.entry('TRAILER!!!', b'', 0)
archive += b'\0'*(-len(archive)%512)
output = base + gzip.compress(archive, mtime=0) + credential
after = cpio.entries(gzip.decompress(output))
assert {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)} == {name}
with a.output.open('xb') as f:
    f.write(output)
audit = dict(base_sha256=sha(base), output_sha256=sha(output), changed=[name],
             driver_sha256=sha(driver), password_addon_last=True, reboot_validated=False)
a.output.with_suffix('.audit.json').write_text(json.dumps(audit, indent=2)+'\n')
print(json.dumps(audit, indent=2))
