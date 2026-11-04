import gzip,hashlib,importlib.util,json,os,struct,subprocess,tempfile,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
def load(n):
 s=importlib.util.spec_from_file_location(n,R/'scripts'/n);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
p=load('surface-power-payload.py');u=load('unrestricted-power-runtime.py');c=load('pack-sq2-vulkan.py')
def archive(e):return gzip.compress(b''.join(c.entry(n,b,m) for n,(m,b) in e.items())+c.entry('TRAILER!!!',b'',0),mtime=0)
class Unrestricted(unittest.TestCase):
 def test_runtime_features_do_not_require_flag(self):
  body=b'#!/bin/sh\nset -eu\ntest "$(cat /sys/module/suspend/parameters/screen_only)" = Y\ntest -f /ABI_REQUIRED\n'
  after=u.transform('bin/video-runtime-stage',body)
  self.assertNotIn(b'/parameters/screen_only',after);self.assertIn(b'test -f /ABI_REQUIRED',after)
  self.assertEqual(after,u.transform('bin/video-runtime-stage',after))
 def test_owner_framework_and_lab_wakelock_not_used_when_disabled(self):
  for name,tag in [('framework-suspend-start','NO_OWNER_REGISTRATION'),('framework-suspend-stage','STOCK_FRAMEWORK'),('lab-keep-awake','NO_LAB_WAKELOCK')]:
   body=(R/'probe/test2'/name).read_text()
   for flag in ('N',None):
    with tempfile.TemporaryDirectory() as td:
     d=Path(td);g=d/'screen_only'
     if flag is not None:g.write_text(flag)
     code=body.replace('/sys/module/suspend/parameters/screen_only',str(g))
     q=subprocess.run(['sh','-c',code],capture_output=True,text=True,timeout=3)
     self.assertEqual(q.returncode,0,q.stderr);self.assertIn(tag,q.stdout)
 def test_effective_payload_credentials_and_guards(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);mod=b'\x7fELF\x00vermagic=fixture\x00resume_count\x00'
   files={'Image':b'kernel','native.efi':b'efi','spi-hid.ko':mod,'system-suspend.sha256':b'a'*64+b'\n'}
   for n,b in files.items():(d/n).write_bytes(b)
   (d/'power-bundle.json').write_text(json.dumps({'policy':'android-persistent-owner','sha256':{n:p.digest(b) for n,b in files.items()}}))
   helper=b'#!/bin/sh\nset -eu\ntest "$(cat /sys/module/suspend/parameters/screen_only)" = Y\ngrep -q ABI /manifest\n'
   e={'init':(0o100755,b'#!/bin/sh\nlab-keep-awake > /logs/lab-keep-awake.txt 2>&1 &\n'),'lib/modules/fixture/spi-hid.ko':(0o100644,mod),'lib/modules/fixture/surface_aggregator.ko':(0o100644,b'stock SSAM'),'bin/lte-unit-seed':(0o100755,helper),'bin/unrelated':(0o100755,b'unmodified')}
   identity=archive({'etc/shadow':(0o100600,b'fixture'),'bin/dropbear':(0o100755,b'fixture')})
   result,a=p.integrate(archive(e)+identity,R,d,c,policy='unrestricted');after=c.entries(gzip.decompress(result))
   self.assertTrue(result.endswith(identity));self.assertFalse(a['screen_only_enabled']);self.assertFalse(a['owner_gate']);self.assertIsNone(a['request_budget']);self.assertIsNone(a['admission_deadline'])
   self.assertNotIn(b'screen_only',after['bin/lte-unit-seed'][1]);self.assertIn(b'grep -q ABI',after['bin/lte-unit-seed'][1]);self.assertEqual(after['bin/unrelated'],e['bin/unrelated'])
   self.assertIn(b'NO_LAB_WAKELOCK',after['bin/lab-keep-awake'][1]);self.assertIn(b'NO_OWNER_REGISTRATION',after['bin/framework-suspend-start'][1])
   self.assertNotIn('etc/surface-pm/desktop-services.jar',after)
   self.assertNotIn(b'framework-suspend-start >',after['init'][1]);self.assertNotIn(b'lab-keep-awake >',after['init'][1])
 def test_changed_helpers_refresh_nested_manifests_without_bypassing_integrity(self):
  helper=b'#!/bin/sh\nset -eu\ntest "$(cat /sys/module/suspend/parameters/screen_only)" = Y\ntest -f /ABI_REQUIRED\n'
  digest=lambda b:hashlib.sha256(b).hexdigest().encode()
  inner=digest(helper)+b'  install.sh\n'
  outer=digest(inner)+b'  child/SHA256SUMS\n'
  entries={'etc/bundle/child/install.sh':(0o100755,helper),'etc/bundle/child/SHA256SUMS':(0o100644,inner),'etc/bundle/SHA256SUMS':(0o100644,outer)}
  after,changed=u.integrate_entries(entries)
  self.assertEqual(after['etc/bundle/child/SHA256SUMS'][1],digest(after['etc/bundle/child/install.sh'][1])+b'  install.sh\n')
  self.assertEqual(after['etc/bundle/SHA256SUMS'][1],digest(after['etc/bundle/child/SHA256SUMS'][1])+b'  child/SHA256SUMS\n')
  self.assertIn('etc/bundle/child/SHA256SUMS',changed);self.assertIn(b'test -f /ABI_REQUIRED',after['etc/bundle/child/install.sh'][1])
  self.assertEqual(after,u.integrate_entries(after)[0])
  bad=dict(entries);bad['etc/bundle/child/SHA256SUMS']=(0o100644,b'0'*64+b'  install.sh\n')
  with self.assertRaisesRegex(ValueError,'Baseline manifest mismatch'):u.integrate_entries(bad)

 def test_nested_codec_guard_migrates_without_overwriting_legacy_version(self):
  import io,tarfile
  sha=lambda b:hashlib.sha256(b).hexdigest()
  blobs={'start.sh':b'#!/system/bin/sh\nset -eu\ntest "$(cat /sys/module/suspend/parameters/screen_only)" = Y\ncd /data/vendor/sq2-codec2/v1\nsha256sum -c SHA256SUMS\nexec ./launch "$1"\n','launch':b'old ELF','service':b'unchanged codec','runtime.json':b'{"version":1,"path":"/data/vendor/sq2-codec2/v1"}'}
  blobs['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(blobs.items())).encode()
  out=io.BytesIO()
  with tarfile.open(fileobj=out,mode='w') as t:
   for n,b in blobs.items():
    m=tarfile.TarInfo(n);m.size=len(b);t.addfile(m,io.BytesIO(b))
  root='etc/sq2-c2-runtime-boot/'
  data={'runtime.tar':out.getvalue(),'runtime-files.sha256':blobs['SHA256SUMS'],'runtime.sha256':(sha(out.getvalue())+'  runtime.tar\n').encode(),'install.sh':b'#!/system/bin/sh\ntarget=$parent/v1\ntest ! -e "$parent/v1.new"\n','service.sh':b'exec /system/bin/sh /data/vendor/sq2-codec2/v1/start.sh "$@"\n'}
  data['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(data.items())).encode()
  entries={root+n:(0o100644,b) for n,b in data.items()}
  with self.assertRaisesRegex(ValueError,'--codec-launcher'):u.nested_codec_runtime(entries,None)
  launcher=bytearray(64);launcher[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',launcher,18,183);launcher+=b'/data/vendor/sq2-codec2/v2u/\0'
  after,audit=u.nested_codec_runtime(entries,bytes(launcher))
  struct.pack_into('<H',launcher,18,62)
  with self.assertRaisesRegex(ValueError,'launcher must be rebuilt'):u.nested_codec_runtime(entries,bytes(launcher))
  self.assertTrue(audit['legacy_version_untouched']);self.assertIn(b'/v2u',after[root+'install.sh'][1]);self.assertNotIn(b'/v1',after[root+'service.sh'][1])
  with tarfile.open(fileobj=io.BytesIO(after[root+'runtime.tar'][1])) as t:
   self.assertNotIn(b'screen_only',t.extractfile('start.sh').read());self.assertEqual(t.extractfile('service').read(),blobs['service'])
   self.assertEqual(t.extractfile('SHA256SUMS').read(),after[root+'runtime-files.sha256'][1])
  self.assertEqual(u.nested_codec_runtime(after,None)[0],after)

 def test_both_model_launchers_close_marker_and_keep_other_boot_policy(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);raw=bytearray(512);struct.pack_into('<I',raw,60,64);raw[64:68]=b'PE\0\0';struct.pack_into('<H',raw,68,0xaa64)
   strings=[r'\EFI\SQ2Default\android-ip4.dtb',r'\EFI\SQ2Default\qebspilaa64.efi',r'\EFI\SurfacePmAuto\Image',r'\EFI\SQ2Default\tcblaunch.exe',r'console=tty0 suspend.screen_only=1 suspend.screen_only_autosuspend=1 nohibernate panic=15 sq2.el2=1 initrd=\EFI\SurfacePmAuto\initramfs.cpio.gz']
   for text in strings:raw+=(text+(' '*512 if text.startswith('console=') else '')).encode('utf-16le')+b'\0\0'
   files={'Image':b'kernel','native.efi':bytes(raw),'spi-hid.ko':b'module','system-suspend.sha256':b'a'*64+b'\n'}
   for n,b in files.items():(d/n).write_bytes(b)
   (d/'power-bundle.json').write_text(json.dumps({'policy':'android-persistent-owner','sha256':{n:p.digest(b) for n,b in files.items()}}))
   for model in ('sq1','sq2'):
    result=p.launcher(d,model,policy='unrestricted');self.assertEqual(len(result),len(raw))
    for token in ['suspend.screen_only=0','suspend.screen_only_autosuspend=0','nohibernate','sq2.el2=1','panic=0']:self.assertIn(token.encode('utf-16le'),result)
    self.assertNotIn('suspend.screen_only=1'.encode('utf-16le'),result);self.assertNotIn('suspend.screen_only_autosuspend=1'.encode('utf-16le'),result)
    legacy=p.launcher(d,model,policy='android');self.assertIn('suspend.screen_only=1'.encode('utf-16le'),legacy)
    layered=p.launcher(d,model,policy='unrestricted',base_initrd=r'\EFI\Existing\initramfs.cpio.gz',initrd_addons=(r'\EFI\NewEnv\unrestricted.cpio.gz',))
    self.assertEqual(layered.count('initrd='.encode('utf-16le')),2)
    self.assertIn(r'initrd=\EFI\Existing\initramfs.cpio.gz'.encode('utf-16le'),layered)
    with self.assertRaises(ValueError):p.launcher(d,model,policy='unrestricted',base_initrd=r'\EFI\..\bad')
    with self.assertRaises(ValueError):p.launcher(d,model,policy='android',base_initrd=r'\EFI\Existing\initramfs.cpio.gz')

 def test_real_display_and_exclusive_hardware_integrity_guards_preserved(self):
  self.assertIn('FLAG_KEEP_SCREEN_ON',(R/'probe/video/player-app/MainActivity.java').read_text())
  for f in ['probe/graphics/quest2-composition.cpp','probe/graphics/quest2-readpixels.c','probe/graphics/quest2-hwui/Sq2Quest2Hwui.java','probe/graphics/quest2-visible/Sq2Quest2Visible.java']:
   text=(R/f).read_text();self.assertNotIn('/parameters/screen_only',text);self.assertIn('LD_PRELOAD',text)
  self.assertIn('nohibernate',(R/'probe/lte/lte-unit-seed').read_text());self.assertIn('sha256sum -c',(R/'probe/lte/lte-framework-boot').read_text())
if __name__=='__main__':unittest.main()
