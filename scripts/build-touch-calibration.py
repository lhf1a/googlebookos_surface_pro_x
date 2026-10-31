#!/usr/bin/env python3
"""Build the offline Android wizard and strict AArch64 calibration profile tool."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile,zipfile

def build(root, output=None):
    root=Path(root);src=root/'probe/touch-calibration'
    sdk=Path(os.environ.get('SQ2_ANDROID_SDK', '/root/.cache/sq2-graphics/sdk'))
    platform=sdk/'android-35/android.jar';tools=sdk/'android-15'
    key=Path(os.environ.get('SQ2_CALIBRATION_KEYSTORE',str(root/'private/touch-calibration/signing.keystore')))
    key.parent.mkdir(parents=True,exist_ok=True)
    if not key.exists():
        subprocess.run(['keytool','-genkeypair','-keystore',str(key),'-storepass','android','-keypass','android',
                        '-alias','touch-calibration','-keyalg','RSA','-keysize','2048','-validity','10000',
                        '-dname','CN=Local Surface Calibration'],check=True,capture_output=True)
    with tempfile.TemporaryDirectory(prefix='sq2-calibration-apk-') as td:
        w=Path(td);(w/'classes').mkdir();(w/'dex').mkdir()
        subprocess.run(['javac','--release','8','-encoding','UTF-8','-classpath',str(platform),'-d',str(w/'classes'),
                        str(src/'Calibration.java'),str(src/'ThumbCheck.java'),str(src/'MainActivity.java')],check=True)
        subprocess.run([str(tools/'d8'),'--min-api','26','--lib',str(platform),'--output',str(w/'dex'),
                        *map(str,sorted((w/'classes').rglob('*.class')))],check=True)
        subprocess.run([str(tools/'aapt2'),'link','--manifest',str(src/'AndroidManifest.xml'),'-I',str(platform),'-o',str(w/'unsigned.apk')],check=True)
        with zipfile.ZipFile(w/'unsigned.apk','a',zipfile.ZIP_DEFLATED) as z:z.write(w/'dex/classes.dex','classes.dex')
        subprocess.run([str(tools/'zipalign'),'-f','-P','16','4',str(w/'unsigned.apk'),str(w/'aligned.apk')],check=True)
        subprocess.run([str(tools/'apksigner'),'sign','--ks',str(key),'--ks-pass','pass:android','--key-pass','pass:android','--out',str(w/'wizard.apk'),str(w/'aligned.apk')],check=True)
        subprocess.run([str(tools/'apksigner'),'verify',str(w/'wizard.apk')],check=True)
        subprocess.run(['aarch64-linux-gnu-g++','-std=c++17','-static','-O2','-Wall','-Wextra','-Werror',str(src/'profile-tool.cpp'),'-o',str(w/'profile-tool')],check=True)
        subprocess.run(['aarch64-linux-gnu-strip',str(w/'profile-tool')],check=True)
        files={'etc/sq2-touch-calibration/wizard.apk':(0o100644,(w/'wizard.apk').read_bytes()),
               'bin/touch-calibration-profile':(0o100755,(w/'profile-tool').read_bytes()),
               'bin/touch-calibration-boot':(0o100755,(root/'probe/test2/touch-calibration-boot').read_bytes().replace(b'\r\n',b'\n'))}
    sha=lambda b:hashlib.sha256(b).hexdigest()
    paths=[*src.glob('*'),root/'probe/test2/sq2-touch-calibration.hpp',root/'probe/test2/touch-calibration-boot',Path(__file__)]
    audit={'source_sha256':{str(p.relative_to(root)):sha(p.read_bytes().replace(b'\r\n',b'\n')) for p in paths if p.is_file()},
           'payload_sha256':{n:sha(b) for n,(_,b) in files.items()}}
    if output:
        output=Path(output);output.mkdir(parents=True,exist_ok=True)
        for n,(_,b) in files.items():(output/Path(n).name).write_bytes(b)
        (output/'build.json').write_text(json.dumps(audit,indent=2)+'\n')
    return files,audit

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=Path('out/touch-calibration'))
    args=p.parse_args();_,audit=build(Path(__file__).resolve().parents[1],args.output);print(json.dumps(audit,indent=2))
