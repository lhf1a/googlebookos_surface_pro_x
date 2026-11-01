"""Exercise the real source generator against the boot-tested EFI source hashes.

Usage: python3 tests/test_sq2_venus_efi_prepare.py PRIVATE_VENUS_MBN
Requires the pinned upstream qebspil checkout; never accesses a device.
"""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
firmware = Path(sys.argv[1]).resolve()
image = firmware.read_bytes()
assert hashlib.sha256(image).hexdigest() == '4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928'
command = [sys.executable, str(ROOT / 'scripts/prepare-sq2-el2-preboot.py')]
# Independent golden hashes from the source of the deployed e3675e36… EFI.
expected = {
    'Makefile': 'c53f0e135f62ed971d19637a27123c3756b00416469c122c279b641a4d638fab',
    'src/sq2-preboot-rmtfs.c': '0de722242dfcd1f176c90c8fd0a5d3a78e43a374621e760a42875c5e9f232150',
    'src/sq2-venus-metadata.h': 'a844556559b95d34cef99417d326402f5c8f17009b20fcb5446283c7cf15ec48',
    'src/sq2-venus-preboot.c': '64a3b7fbce8eadae538555ea43dffb2e94081442c2ab57b51837476de001618a',
    'src/qebspil.h': '30bb412500e4c8ca6dba33e35b4e33b82ae0439aa1a3241e3e16de1d0de62b48',
    'src/main.c': '7d8d673154f3fb1cac4faf46edf3a41737f3a3050134d49a9456738b0b53b036',
    'src/scm.c': 'f137ca7bcd0b17f999b533164146ea70a2535f3a3389bccb23b9053d559ff1c1',
}
with tempfile.TemporaryDirectory(prefix='sq2-venus-efi-prepare-') as temporary:
    directory = Path(temporary)
    output = directory / 'candidate'
    bad = directory / 'bad.mbn'
    for data in (image[:-1], bytes([image[0] ^ 1]) + image[1:]):
        bad.write_bytes(data)
        result = subprocess.run(command + ['--output', str(output), '--venus-firmware', str(bad)],
                                capture_output=True, text=True)
        assert result.returncode != 0 and 'does not match' in result.stderr
        assert not output.exists(), 'bad firmware created a partial candidate'
    subprocess.run(command + ['--output', str(output), '--venus-firmware', str(firmware)], check=True)
    for name, digest in expected.items():
        assert hashlib.sha256((output / name).read_bytes()).hexdigest() == digest, name
    result = subprocess.run(command + ['--output', str(output), '--venus-firmware', str(firmware)],
                            capture_output=True)
    assert result.returncode != 0, 'existing candidate overwritten'
    for name, digest in expected.items():
        assert hashlib.sha256((output / name).read_bytes()).hexdigest() == digest, name
    baseline = directory / 'baseline'
    subprocess.run(command + ['--output', str(baseline)], check=True)
    assert not (baseline / 'src/sq2-venus-preboot.c').exists()
    assert 'sq2_venus_' not in (baseline / 'src/main.c').read_text()
    assert 'sq2_pas_trace' not in (baseline / 'src/scm.c').read_text()
print('PASS: deployed sources reproduced, bad firmware/output refused, default retained')
