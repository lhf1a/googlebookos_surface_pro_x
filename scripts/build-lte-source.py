#!/usr/bin/env python3
"""Compile LTE/Radio from source and assemble a private delta, no donor Radio04."""
from pathlib import Path
import importlib.util, json, os, shutil, subprocess, sys

def build(root, inputs):
    root, inputs = Path(root), Path(inputs)
    out = root/'out/lte-radio-20261103'
    out.mkdir(parents=True, exist_ok=True)
    for n in ('libbinder_ndk.so', 'modem_fsg.bin', 'ipa_fws.elf', 'ipa_sq2_lab.ko', 'rmnet.ko', 'android-el2.dtb'):
        if not (inputs/n).is_file(): raise ValueError('Missing private/build-inputs/lte/'+n)
    shutil.copyfile(inputs/'libbinder_ndk.so', out/'libbinder_ndk.so')
    subprocess.run([sys.executable, str(root/'scripts/build-sq2-radio.py')], check=True)
    sdk=Path(os.environ.get('SQ2_ANDROID_SDK','/root/.cache/sq2-graphics/sdk'))
    for name in ('qmi-data','qmi-mode','rmnet-create'):
        subprocess.run(['aarch64-linux-gnu-gcc','-O2','-static','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(root/f'probe/lte/sq2-{name}.c'),'-o',str(out/f'sq2-{name}')],check=True)
    # Radio owns the bearer. The legacy Java NetworkAgent is not started.
    for n in ('classes','stubs','dex'): (out/n).mkdir(exist_ok=True)
    subprocess.run(['javac','--release','8','-cp',str(sdk/'android-35/android.jar'),'-d',str(out/'stubs')]+list(map(str,(root/'probe/lte/stubs/android/net').glob('*.java'))),check=True)
    subprocess.run(['javac','--release','8','-cp',str(out/'stubs')+':'+str(sdk/'android-35/android.jar'),'-d',str(out/'classes'),str(root/'probe/lte/Sq2Lte.java')],check=True)
    subprocess.run([str(sdk/'android-15/d8'),'--min-api','34','--lib',str(sdk/'android-35/android.jar'),'--classpath',str(out/'stubs'),'--output',str(out/'dex')]+list(map(str,(out/'classes').glob('*.class'))),check=True)
    subprocess.run(['jar','cf',str(out/'sq2-lte.jar'),'-C',str(out/'dex'),'classes.dex'],check=True)
    res=out/'overlay-res';(res/'values').mkdir(parents=True,exist_ok=True);(res/'xml').mkdir(exist_ok=True)
    shutil.copyfile(root/'probe/lte/overlay/res/values/config.xml',res/'values/config.xml')
    config=json.loads((inputs/'unit.json').read_text()) if (inputs/'unit.json').exists() else {}
    if config.get('operator_numeric') and config.get('apn'):
        spec=importlib.util.spec_from_file_location('apn',root/'scripts/prepare-sq2-radio-apn.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        apns=m.apn_resource(config['operator_numeric'],config['apn'])
    else: apns=b'<?xml version="1.0" encoding="utf-8"?><apns version="8"/>\n'
    (res/'xml/apns.xml').write_bytes(apns)
    def run(*args): subprocess.run(list(map(str,args)),check=True)
    run(sdk/'android-15/aapt2','compile','--dir',res,'-o',out/'overlay-res.zip')
    run(sdk/'android-15/aapt2','link','--manifest',root/'probe/lte/overlay/AndroidManifest.xml','-I',sdk/'android-35/android.jar','--auto-add-overlay','-o',out/'overlay-unsigned.apk',out/'overlay-res.zip')
    key=inputs/'local.keystore'
    if not key.exists():run('keytool','-genkeypair','-keystore',key,'-storepass','android','-keypass','android','-alias','sq2-radio','-dname','CN=Local Radio Overlay','-keyalg','RSA','-keysize','2048','-validity','3650')
    run(sdk/'android-15/zipalign','-f','-P','16','4',out/'overlay-unsigned.apk',out/'overlay-aligned.apk')
    run(sdk/'android-15/apksigner','sign','--ks',key,'--ks-pass','pass:android','--key-pass','pass:android','--out',out/'sq2-radio-overlay.apk',out/'overlay-aligned.apk')
    return out, config
