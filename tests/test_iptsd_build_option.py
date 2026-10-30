"""Offline selection checks, not an ARM build or device validation."""
import json
from pathlib import Path
import subprocess
import sys

script = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]/'scripts/build-test3-iptsd.py'
# Legacy builder has no side-effect-free parser; never execute its build here.
if '--ghost-touch-fix' not in script.read_text():
    print('LEGACY: both patches unconditional; no combined option')
    raise SystemExit(1)
for mode in ('on', 'off'):
    p = subprocess.run([sys.executable, str(script), '--ghost-touch-fix', mode, '--plan-json'], capture_output=True, text=True)
    assert p.returncode == 0, p.stderr
    result = json.loads(p.stdout)
    assert result == {'ghost_touch_fix': mode, 'patches': ['patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py'] if mode == 'on' else []}
    print(mode + ': ' + ','.join(result['patches']))
p = subprocess.run([sys.executable, str(script), '--plan-json'], capture_output=True, text=True)
assert p.returncode == 0 and json.loads(p.stdout)['ghost_touch_fix'] == 'on'
p = subprocess.run([sys.executable, str(script), '--ghost-touch-fix', 'partial', '--plan-json'], capture_output=True, text=True)
assert p.returncode == 2
print('PASS: default on; on=both, off=neither; partial rejected')
