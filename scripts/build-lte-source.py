#!/usr/bin/env python3
"""Compile LTE/Radio from source and assemble a private delta, no donor Radio04."""
from pathlib import Path
import os, shutil, subprocess, sys

def build(root, inputs):
    root, inputs = Path(root), Path(inputs)
    out = root/'out/lte-radio-20261103'
    out.mkdir(parents=True, exist_ok=True)
    for n in ('libbinder_ndk.so', 'ipa_fws.elf', 'ipa_sq2_lab.ko', 'rmnet.ko', 'android-el2.dtb'):
        if not (inputs/n).is_file(): raise ValueError('Missing private/build-inputs/lte/'+n)
    shutil.copyfile(inputs/'libbinder_ndk.so', out/'libbinder_ndk.so')
    subprocess.run([sys.executable, str(root/'scripts/build-sq2-radio.py')], check=True)
    sdk=Path(os.environ.get('SQ2_ANDROID_SDK','/root/.cache/sq2-graphics/sdk'))
    for name in ('qmi-data','qmi-mode','rmnet-create','fsg-cache'):
        subprocess.run(['aarch64-linux-gnu-gcc','-O2','-static','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(root/f'probe/lte/sq2-{name}.c'),'-o',str(out/f'sq2-{name}')],check=True)
    res=out/'overlay-res';(res/'values').mkdir(parents=True,exist_ok=True);(res/'xml').mkdir(exist_ok=True)
    shutil.copyfile(root/'probe/lte/overlay/res/values/config.xml',res/'values/config.xml')
    config={}
    apns=(root/'probe/lte/overlay/res/xml/apns.xml').read_bytes()
    (res/'xml/apns.xml').write_bytes(apns)
    def run(*args): subprocess.run(list(map(str,args)),check=True)
    run(sdk/'android-15/aapt2','compile','--dir',res,'-o',out/'overlay-res.zip')
    run(sdk/'android-15/aapt2','link','--manifest',root/'probe/lte/overlay/AndroidManifest.xml','-I',sdk/'android-35/android.jar','--auto-add-overlay','-o',out/'overlay-unsigned.apk',out/'overlay-res.zip')
    key=inputs/'local.keystore'
    if not key.exists():run('keytool','-genkeypair','-keystore',key,'-storepass','android','-keypass','android','-alias','sq2-radio','-dname','CN=Local Radio Overlay','-keyalg','RSA','-keysize','2048','-validity','3650')
    run(sdk/'android-15/zipalign','-f','-P','16','4',out/'overlay-unsigned.apk',out/'overlay-aligned.apk')
    run(sdk/'android-15/apksigner','sign','--ks',key,'--ks-pass','pass:android','--key-pass','pass:android','--out',out/'sq2-radio-overlay.apk',out/'overlay-aligned.apk')
    return out, config
