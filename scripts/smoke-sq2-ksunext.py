#!/usr/bin/env python3
"""Run the established screen-only QEMU checks against the Next candidate."""
from pathlib import Path
import gzip

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / 'scripts/smoke-sq2-screen-only.py'
source = path.read_text()
source = source.replace("Path('/root/.cache/sq2-probe-build')", "Path('/root/.cache/sq2-ksunext')")
source = source.replace("root / 'out/screen-only'", "root / 'out/ksunext'")
source = source.replace("build / 'busybox/busybox'", "out / 'busybox'")
source = source.replace("build / 'kernel-android-out/usr/gen_init_cpio'", "build / 'builtin-build/usr/gen_init_cpio'")
source = source.replace("out / 'Image'", "out / 'Image-candidate'")
source = source.replace('console=ttyAMA0 panic=-1 suspend.screen_only=1 nohibernate',
                        'console=ttyAMA0 panic=-1 lsm=selinux enforcing=0 suspend.screen_only=1 nohibernate')
exec(compile(source, str(path), 'exec'), {'__file__': str(path), '__name__': '__main__'})
device = gzip.decompress((ROOT / 'out/ksunext/device-config.gz').read_bytes()).decode()
candidate = (ROOT / 'out/ksunext/kernel.config').read_text()
def options(s):
    return {line.split('=')[0]: line for line in s.splitlines() if line.startswith('CONFIG_')}
a, b = options(device), options(candidate)
changes = [f'{a.get(k)} -> {b.get(k)}' for k in sorted(a.keys() | b.keys()) if a.get(k) != b.get(k)]
(ROOT / 'out/ksunext/config-diff.txt').write_text('\n'.join(changes) + '\n')
print('Config differences:', len(changes), '(review required)')
