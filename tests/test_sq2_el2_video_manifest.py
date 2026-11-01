"""Exercise the packer's actual manifest refresh without private firmware inputs."""
from pathlib import Path
import hashlib

source = (Path(__file__).resolve().parents[1] / 'scripts/pack-sq2-el2-android.py').read_text()
code = source.split('# Refresh the runtime integrity manifest', 1)[1]
code = code[code.index('\n') + 1:code.index('# End runtime integrity manifest refresh.')]
prefix = 'etc/sq2-video-native/'
sha = lambda b: hashlib.sha256(b).hexdigest()

def run(rows, original=b'old module'):
    before = {
        prefix + 'SHA256SUMS': (0o100644, rows),
        prefix + 'venus-core.ko': (0o100644, original),
        prefix + 'run-eos.sh': (0o100755, b'unchanged helper'),
    }
    e = dict(before)
    e[prefix + 'venus-core.ko'] = (0o100644, b'new module')
    scope = dict(Path=Path, hashlib=hashlib, before=before, e=e,
                 changed=[], module_report={prefix + 'venus-core.ko': sha(b'new module')})
    exec(code, scope)
    return scope

rows = (sha(b'old module') + '  venus-core.ko\n' +
        sha(b'unchanged helper') + '  run-eos.sh\n').encode()
result = run(rows)
updated = result['e'][prefix + 'SHA256SUMS'][1]
assert updated == rows.replace(sha(b'old module').encode(), sha(b'new module').encode())
assert result['changed'] == [prefix + 'SHA256SUMS']
for invalid, original in [(rows, b'corrupt input'), (rows + rows, b'old module'),
                          (rows.replace(b'venus-core.ko', b'../venus-core.ko'), b'old module'),
                          (rows.splitlines(keepends=True)[1], b'old module')]:
    try:
        run(invalid, original)
    except AssertionError:
        pass
    else:
        raise AssertionError('invalid/stale/incomplete manifest accepted')
print('PASS: rebuilt module hashes, unchanged helper, corrupt input, duplicate/path and missing-module rejection')
