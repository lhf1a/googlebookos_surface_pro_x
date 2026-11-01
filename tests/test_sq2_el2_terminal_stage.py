"""Run real terminal prerequisites: normal boot must not execute the KVM probe."""
from pathlib import Path
import os
import subprocess
import tempfile

R = Path(__file__).resolve().parents[1]
source = (R/'probe/virtualization/el2-terminal-stage').read_text()
for mode, cmdline, probe in [('normal', 'sq2.el2=1', False),
                             ('debug', 'sq2.el2=1 sq2.debug=1', True),
                             ('lab', 'sq2.el2=experimental', True)]:
    with tempfile.TemporaryDirectory() as temporary:
        d = Path(temporary)
        (d/'cmdline').write_text(cmdline)
        (d/'guard').write_text('Y')
        (d/'mounts').write_text('overlay /newroot/vendor overlay rw 0 0\n')
        (d/'manifest').write_text('<name>android.hardware.security.secretkeeper</name>')
        for name in ('core', 'decoder', 'kvm', 'vsock'):
            (d/name).touch()
        code = source.replace('test -c ', 'test -e ')
        for original, name in [('/proc/cmdline', 'cmdline'),
                               ('/sys/module/suspend/parameters/screen_only', 'guard'),
                               ('/proc/mounts', 'mounts'), ('/dev/kvm', 'kvm'),
                               ('/dev/vhost-vsock', 'vsock'),
                               ('/etc/sq2-video-native/venus-core.ko', 'core'),
                               ('/etc/sq2-video-native/venus-dec.ko', 'decoder'),
                               ('/newroot/vendor/etc/vintf/manifest/android.hardware.security.secretkeeper.trusty.xml', 'manifest')]:
            code = code.replace(original, str(d/name))
        code = code.replace('/bin/kvm-arm64-witness', 'witness')
        stubs = 'modprobe() { echo "$1" >> "$FIXTURE/modules"; }\nwitness() { echo WITNESS; }\n'
        result = subprocess.run(['sh', '-c', stubs+code], env=dict(os.environ, FIXTURE=temporary),
                                capture_output=True, text=True, timeout=5)
        assert result.returncode == 0, result.stderr
        assert ('WITNESS' in result.stdout) == probe, mode
        assert 'STAGE_READY' in result.stdout
        assert (d/'modules').read_text().splitlines() == ['sch_ingress', 'cls_bpf']
        assert not (d/'manifest').exists()
print('PASS: terminal prerequisites preserved; KVM probe only runs in explicit lab/debug boots')
