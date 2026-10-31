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
for mode in ('on', 'off', 'edge-only'):
    for calibration in ('on', 'off'):
        p = subprocess.run([sys.executable, str(script), '--ghost-touch-fix', mode, '--touch-calibration', calibration, '--plan-json'], capture_output=True, text=True)
        assert p.returncode == 0, p.stderr
        result = json.loads(p.stdout)
        expected = {'on': ['patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py'],
                    'off': [], 'edge-only': ['patch-iptsd-sq2-heatmap.py']}[mode]
        if calibration == 'on': expected += ['patch-iptsd-sq2-calibration.py']
        assert result == {'ghost_touch_fix': mode, 'touch_calibration': calibration, 'patches': expected}
p = subprocess.run([sys.executable, str(script), '--plan-json'], capture_output=True, text=True)
assert p.returncode == 0 and json.loads(p.stdout) == {
    'ghost_touch_fix': 'on',
    'touch_calibration': 'on',
    'patches': ['patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py', 'patch-iptsd-sq2-calibration.py'],
}
p = subprocess.run([sys.executable, str(script), '--ghost-touch-fix', 'partial', '--plan-json'], capture_output=True, text=True)
assert p.returncode == 2
print('PASS: all six ghost/calibration combinations; both default on; partial rejected')
