from pathlib import Path
import subprocess,hashlib
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('--codec', choices=('avc','vp8','vp9'), default='avc')
args=parser.parse_args()
r=Path(__file__).resolve().parents[1];d=r/'out/video-decode/codec2-integration';o=d/('component-backend' if args.codec=='avc' else 'component-'+args.codec);ndk=Path('/root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64');av=Path('/root/.cache/sq2-codec2-av-reference/media/codec2')
# Build only against the pinned headers and copied runtime libraries verified in this lab.
if subprocess.check_output(['git','-C','/root/.cache/sq2-codec2-av-reference','rev-parse','HEAD'],text=True).strip()!='e2f098935447ca4945946de5cb69db843fe3f003':
 raise SystemExit('Codec2 header revision mismatch')
o.mkdir(parents=True,exist_ok=True)
if hashlib.sha256((d/'libcodec2_soft_common.so').read_bytes()).hexdigest()!='32ddd7d492e17d367346ff82c5a0a48cc47c073e4f15af7624f344e0d3a595bb': raise SystemExit('runtime library mismatch: libcodec2_soft_common.so')
if hashlib.sha256((d/'libcodec2_vndk.so').read_bytes()).hexdigest()!='7ee32cb2032677152148260095e348289f96c89ee40a6f3bd77e12d41fdcb105': raise SystemExit('runtime library mismatch: libcodec2_vndk.so')
if hashlib.sha256((d/'libcodec2.so').read_bytes()).hexdigest()!='65ff181b4b3d834363ce37508d7250749fbcb9dba829b2922478d5848de1670e': raise SystemExit('runtime library mismatch: libcodec2.so')
if hashlib.sha256((d/'libc++.so').read_bytes()).hexdigest()!='cb118e98c74d3454858921123b9b51ba13df9cad7dfa141dd892c453661a4d78': raise SystemExit('runtime library mismatch: libc++.so')
cmd=[str(ndk/'bin/aarch64-linux-android35-clang++'),'-std=c++17','-fno-rtti','-nostdinc++','-nostdlib++','-isystem','/root/.cache/sq2-c2-platform-libcxx-headers']
for name,revision,includes in [
 ('core','1d37c4499249b17f143df12cebc5ee9ea3600ce3',['libutils/include','libcutils/include','libsystem/include']),
 ('logging','37cb6ed458c92bc488b60963fd2cb8351ff30677',['liblog/include']),
 ('base','5d87ea136b23dd152f00a81ecbb78e460b87ce94',['include']),
]:
 root=Path('/root/.cache')/f'sq2-c2-{name}-reference'
 if root.exists():
  if subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()!=revision:
   raise SystemExit(f'Platform header revision mismatch: {name}')
  for include in includes:cmd+=['-I',str(root/include)]
for p in [Path('/root/.cache/sq2-codec2-av-reference/media/module/foundation/include'),av/'core/include',av/'vndk/include',av/'components/base/include',Path('/root/.cache/sq2-quest2-hidl/include'),Path('/root/.cache/sq2-graphics/src/mesa-26.2.4/include/android_stub')]:cmd+=['-I',str(p)]
cmd += ['-I',str(r/'out/video-decode/h264-vector'),str(r/'probe/video/sq2-c2-component-test.cpp'),str(d/'libcodec2_soft_common.so'),str(d/'libcodec2_vndk.so'),str(d/'libcodec2.so'),str(d/'libc++.so'),'-Wl,--allow-shlib-undefined','-o',str(o/'interface-test')]
cmd+=['-llog','-landroid']
if args.codec!='avc':
 cmd[cmd.index(str(r/'out/video-decode/h264-vector'))]=str(r/'out/video-decode'/f'{args.codec}-vector')
 cmd+=['-DSQ2_VPX_VECTOR']
p=subprocess.run(cmd,capture_output=True,text=True);(o/'build.txt').write_text(repr(cmd)+'\n'+p.stdout+p.stderr+'\nexit='+str(p.returncode));print(p.stdout,p.stderr);raise SystemExit(p.returncode)
