#!/usr/bin/env python3
"""Build the Camera2 front/rear/front preview and JPEG diagnostic APK."""
from pathlib import Path
import subprocess as sp
import tempfile,zipfile
root=Path(__file__).resolve().parents[1]
sdk=Path('/mnt/spx-workspace/toolchains/sq2-graphics/sdk')
tools=sdk/'android-15';platform=sdk/'android-35/android.jar'
src=root/'probe/camera/app';out=root/'out/camera-android-20261115/camera-test.apk'
key=root/'private/camera-test/signing.keystore'
key.parent.mkdir(parents=True,exist_ok=True)
if not key.exists():
 sp.run(['keytool','-genkeypair','-keystore',str(key),'-storepass','android','-keypass','android','-alias','camera-test','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Local SPX Camera Test'],check=True,capture_output=True)
with tempfile.TemporaryDirectory(dir=root/'work',prefix='camera-apk-') as td:
 w=Path(td);(w/'classes').mkdir();(w/'dex').mkdir()
 sp.run(['javac','--release','8','-classpath',str(platform),'-d',str(w/'classes'),str(src/'MainActivity.java')],check=True)
 sp.run([str(tools/'d8'),'--min-api','28','--lib',str(platform),'--output',str(w/'dex'),*map(str,(w/'classes').rglob('*.class'))],check=True)
 sp.run([str(tools/'aapt2'),'link','--manifest',str(src/'AndroidManifest.xml'),'-I',str(platform),'-o',str(w/'unsigned.apk')],check=True)
 with zipfile.ZipFile(w/'unsigned.apk','a',zipfile.ZIP_DEFLATED) as z:z.write(w/'dex/classes.dex','classes.dex')
 sp.run([str(tools/'zipalign'),'-f','-P','16','4',str(w/'unsigned.apk'),str(w/'aligned.apk')],check=True)
 sp.run([str(tools/'apksigner'),'sign','--ks',str(key),'--ks-pass','pass:android','--key-pass','pass:android','--out',str(out),str(w/'aligned.apk')],check=True)
 sp.run([str(tools/'apksigner'),'verify',str(out)],check=True)
print(out)
