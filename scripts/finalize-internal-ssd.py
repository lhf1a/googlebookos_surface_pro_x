#!/usr/bin/env python3
"""Add reproducibility/status files to EFI and refresh deployment extents."""
from pathlib import Path
import importlib.util
import json
import shutil
import subprocess

r=Path(__file__).resolve().parents[1]
o=r/'out/internal-ssd'
c=Path('/root/.cache/sq2-probe-build/internal-ssd')
m=json.loads((o/'image-manifest.json').read_text())
p=json.loads((o/'layout.json').read_text())
parts={p['name']:p for p in p['partitions']}
d=c/'efi-tree/sq2-port'
d.mkdir(parents=True,exist_ok=True)
subprocess.run(['tar','-czf',str(d/'port-sources.tar.gz'),'--exclude=__pycache__','-C',str(r),'scripts','configs','probe'],check=True)
for source, dest in [(o/'kernel.config','kernel.config'),(o/'validation.json','nvme-validation.json'),
                     (r/'out/audio-lte/progress.json','audio-status-before-install.json'),
                     (r/'out/audio-lte/audio-hal-sq2.patch.json','audio-hal-patch.json'),
                     (r/'out/touch/ghost-touch-investigation.json','touch-status.json')]:
    shutil.copyfile(source,d/dest)
(d/'INSTALL-NOTES.txt').write_text('''SQ2 Googlebook OS replacement SSD - 2026-10-27
Target: 512110190592 bytes, internal NVMe or USB-attached boot.
GRUB default: entry 1, timeout 3 seconds.
1: desktop with experimental physical audio. 2: desktop without audio.
3: SSH rescue (Android userdata not mounted). 4: shutdown.
Includes input posture fix, IPTSD state fix + AspectMax=2.0 with default
detection thresholds, Freedreno linear buffers, Wi-Fi framework adapter,
LAN-only first setup, root/debug kernel and permissive SELinux.
Audio: correct PD mapper, physical ALSA stereo routing, capped -18dB
digital gain, pinned Cuttlefish HAL emulator pacing workaround.
Audio remains experimental: reported occasional noise, weak bass and a
board reboot during playback; crash root cause has NOT been established.
No speaker protection tuning, no verified microphone or LTE data service.
QEMU verified NVMe selection, Android init and physical ext4 data mount.
Surface internal PCIe/NVMe hardware startup still needs on-device testing.
The original 128GB system disk and workstation internal disk were not used
as installation targets. This is a fresh Android data filesystem.
Firmware/stock Android partitions retained from mica recovery 16471258.
Stock OTA disabled for this development port. Wi-Fi/SSH private settings
are included in the boot initramfs; do not distribute that file publicly.
''')
fat=f"{m['image']}@@{parts['SQ2PROBE']['offset']}"
subprocess.run(['mcopy','-o','-s','-i',fat,str(d),'::/'],check=True)
spec=importlib.util.spec_from_file_location('builder',r/'scripts/build-googlebook-ssd.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
b.C=c;b.SIZE=m['image_bytes']
m['extents']=b.write_records(Path(m['image']),parts)
m['written_bytes']=sum(e['bytes'] for e in m['extents'])
(o/'image-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print('Final image manifest refreshed:',m['written_bytes'],'bytes to write')
