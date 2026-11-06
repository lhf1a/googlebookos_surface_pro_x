#!/usr/bin/env python3
"""Stage operator-built persistent-owner kernel inputs; no device operations.

Use the unified Linux 6.18 builder with reviewed source, not the retired diagnostic
02-native-single-request patch. QEMU/physical validation is a separate step.
"""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('image','launcher','spi_module','system_suspend'):
    p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
files={'Image':a.image.read_bytes(),'native.efi':a.launcher.read_bytes(),
       'spi-hid.ko':a.spi_module.read_bytes(),
       'system-suspend.sha256':(hashlib.sha256(a.system_suspend.read_bytes()).hexdigest()+'\n').encode()}
if not all(files.values()) or not a.system_suspend.stat().st_size:p.error('Empty input')
a.output.mkdir(parents=True,exist_ok=False)
for n,b in files.items():(a.output/n).write_bytes(b)
(a.output/'power-bundle.json').write_text(json.dumps(dict(policy='android-persistent-owner',
    sha256={n:hashlib.sha256(b).hexdigest() for n,b in files.items()},
    hardware_tested=False,scope='packaged inputs; requires matching kernel/module and persistent admission validation'),indent=2)+'\n')
