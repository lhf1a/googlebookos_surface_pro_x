#!/usr/bin/env python3
"""Build the Surface framework overlay that exposes accelerometer rotation policy."""
from pathlib import Path
import argparse,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('toolchains',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--keystore',type=Path,help='explicit local build signing key; never packaged')
a=p.parse_args();root=Path(__file__).resolve().parents[1]
sdk=a.toolchains.resolve()/'sq2-graphics/sdk';tools=sdk/'android-15'
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
src=root/'probe/sensors/overlay';key=(a.keystore or root/'private/spx-sensors/overlay.keystore').resolve()
def run(*args):subprocess.run(list(map(str,args)),check=True)
key.parent.mkdir(parents=True,exist_ok=True)
if not key.exists():
 run('keytool','-genkeypair','-keystore',key,'-storepass','android','-keypass','android','-alias','spx-sensors','-dname','CN=Local Surface Sensors Overlay','-keyalg','RSA','-keysize','2048','-validity','10000')
run(tools/'aapt2','compile','--dir',src/'res','-o',out/'resources.zip')
run(tools/'aapt2','link','--manifest',src/'AndroidManifest.xml','-I',sdk/'android-35/android.jar','--auto-add-overlay','-o',out/'unsigned.apk',out/'resources.zip')
run(tools/'zipalign','-f','-P','16','4',out/'unsigned.apk',out/'aligned.apk')
run(tools/'apksigner','sign','--ks',key,'--ks-pass','pass:android','--key-pass','pass:android','--out',out/'spx-sensors-overlay.apk',out/'aligned.apk')
run(tools/'apksigner','verify',out/'spx-sensors-overlay.apk')
