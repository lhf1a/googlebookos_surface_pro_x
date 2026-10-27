#!/usr/bin/env python3
"""Check the new NVMe partition naming/selection and physical /data mount.

QEMU uses a disposable qcow2 overlay: the deployment image remains unchanged.
This does not validate Surface PCIe hardware or audio stability.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import time
import sys

r = Path(__file__).resolve().parents[1]
o = r/'out/internal-ssd'
manifest = json.loads((o/'image-manifest.json').read_text())
for name, digest in manifest['boot_payload_sha256'].items():
    assert hashlib.sha256((o/name).read_bytes()).hexdigest() == digest, name
c = Path('/root/.cache/sq2-probe-build/internal-ssd')
overlay = c/'nvme-smoke.qcow2'
cmd = ['qemu-system-aarch64', '-machine', 'virt', '-cpu', 'max', '-m', '4096', '-smp', '4',
       '-nographic', '-no-reboot', '-nic', 'none', '-kernel', str(o/'Image'),
       '-initrd', str(o/'native-combined-initramfs.cpio.gz'),
       '-append', 'console=ttyAMA0 loglevel=6 panic=0 sq2.native=ssd-smoke sq2.storage=nvme lsm=selinux enforcing=0',
       '-drive', f'if=none,id=ssd,file={overlay},format=qcow2',
       '-device', 'nvme,drive=ssd,serial=SQ2INTERNAL512']
log = o/'nvme-smoke.log'
if '--validate-captured' not in sys.argv:
    assert not overlay.exists(), 'Preserve previous smoke image'
    subprocess.run(['qemu-img', 'create', '-f', 'qcow2', '-F', 'raw', '-b', manifest['image'], str(overlay)], check=True)
    with log.open('wb') as stream:
        p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT)
        deadline = time.monotonic()+120
        try:
            while p.poll() is None and time.monotonic() < deadline:
                time.sleep(2)
                text = log.read_text(errors='replace')
                if 'SQ2_NATIVE_PREP_FAILED' in text or 'Kernel panic' in text:
                    break
        finally:
            if p.poll() is None:
                p.terminate()
                p.wait(timeout=15)
    raw = c/'nvme-smoke-captured.img'
    assert not raw.exists(), 'Preserve previous captured image'
    subprocess.run(['qemu-img', 'convert', '-f', 'qcow2', '-O', 'raw', str(overlay), str(raw)], check=True)
    esp = next(p for p in json.loads((o/'layout.json').read_text())['partitions'] if p['name']=='SQ2PROBE')
    evidence = o/'nvme-smoke-evidence'
    evidence.mkdir(exist_ok=True)
    subprocess.run(['mcopy', '-s', '-i', f'{raw}@@{esp["offset"]}', '::/logs', str(evidence)], check=True)
text = log.read_text(errors='replace')
assert 'SQ2_NATIVE_PREP_FAILED' not in text and 'Kernel panic' not in text, text[-6000:]
assert 'SQ2_NATIVE_HANDOFF' in text, text[-6000:]
bootid = text.split('SQ2_GOOGLEBOOK_NATIVE_PREP bootid=')[1].split()[0]
kernel = (o/'nvme-smoke-evidence/logs'/('googlebook-native-'+bootid)/'kernel-live.txt').read_text(errors='replace')
assert 'init second stage started' in kernel
assert 'EXT4-fs (nvme0n1p1): mounted filesystem' in kernel
assert 'Kernel panic' not in kernel and 'InitFatalReboot' not in kernel
result = dict(nvme_partition_selection=True, android_init_handoff=True, physical_userdata_mounted=True,
              surface_hardware_boot_verified=False, audio_stability_verified=False,
              payload_sha256=manifest['boot_payload_sha256'])
(o/'validation.json').write_text(json.dumps(result, indent=2)+'\n')
print('PASS: NVMe -> physical super -> Android init -> persistent ext4 userdata')
