"""Run the actual driver's preflight for Linux-started and adopted ADSP."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
helper = (ROOT / 'probe/test2/audio-drivers-start').read_text()
guard = helper.split('# The 6.3 NGD child', 1)[0]
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    guard = guard.replace('/sys/class/remoteproc/remoteproc2', temporary)
    guard = guard.replace('export PATH=/bin:/sbin', 'export PATH=/usr/bin:/bin')
    for name, state in [('17300000.remoteproc', s) for s in
                        ('running', 'attached', 'offline', 'detached', 'crashed', 'suspended', '')] + [
                            ('4080000.remoteproc', 'attached')]:
        (root / 'name').write_text(name + '\n')
        (root / 'state').write_text(state + '\n')
        result = subprocess.run(['sh', '-c', guard + '\necho PREFLIGHT_OK\n'],
                                capture_output=True, text=True, timeout=5)
        accepted = name == '17300000.remoteproc' and state in ('running', 'attached')
        assert (result.returncode == 0) == accepted, (name, state, result)
        assert ('PREFLIGHT_OK' in result.stdout) == accepted
    (root / 'name').write_text('17300000.remoteproc\n')
    (root / 'state').unlink()
    result = subprocess.run(['sh', '-c', guard], capture_output=True, timeout=5)
    assert result.returncode != 0
source = (ROOT / 'scripts/pack-sq2-el2-android.py').read_text()
code = source.split('# Refresh the shared audio guard', 1)[1].split('\n', 1)[1]
code = code.split("old=e[wifi][1]", 1)[0]
name = 'bin/audio-drivers-start'
before = {name: (0o100755, b'old guard')}
e = dict(before)
changed = []
exec(code, dict(r=ROOT, before=before, e=e, changed=changed, subprocess=subprocess))
assert e[name] == (0o100755, helper.encode()) and changed == [name]
print('PASS: running/attached accepted; inactive/wrong/missing ADSP refused; packer refreshes helper')
