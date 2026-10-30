from pathlib import Path
import urllib.request,base64,subprocess,hashlib
out=Path('/root/.cache/sq2-c2-aidl-generated');(out/'external/android/hardware').mkdir(parents=True,exist_ok=True)
u='https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/hardware/HardwareBuffer.aidl?format=TEXT'
p=out/'external/android/hardware/HardwareBuffer.aidl';data=base64.b64decode(urllib.request.urlopen(u).read());p.write_bytes(data);print('HardwareBuffer.aidl sha256='+hashlib.sha256(data).hexdigest())
root=Path('/root/.cache/sq2-c2-hardware-interfaces');dirs=[root/'media/c2/aidl/aidl_api/android.hardware.media.c2/1',root/'media/bufferpool/aidl/aidl_api/android.hardware.media.bufferpool2/2',root/'common/aidl/aidl_api/android.hardware.common/2',root/'common/fmq/aidl/aidl_api/android.hardware.common.fmq/1']
incs=dirs+[out/'external',Path('/root/.cache/sq2-c2-native-reference/aidl/gui')]
for directory in dirs:
 for src in directory.rglob('*.aidl'):
  cmd=['/root/.cache/sq2-graphics/sdk/android-15/aidl','--lang=ndk','--structured','--stability=vintf','--version='+directory.name,'--hash='+(directory/'.hash').read_text().strip().splitlines()[-1],'--out='+str(out/'src'),'--header_out='+str(out/'include')]+['-I'+str(x) for x in incs]+[str(src)]
  p=subprocess.run(cmd,capture_output=True,text=True)
  if p.returncode: print(p.stderr);raise SystemExit(p.returncode)
print('GENERATED',len(list((out/'include').rglob('*.h'))))
