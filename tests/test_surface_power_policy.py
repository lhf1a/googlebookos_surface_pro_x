import gzip,hashlib,importlib.util,json,os,struct,subprocess,tempfile,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
def load(n):
 s=importlib.util.spec_from_file_location(n,R/'scripts'/n);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
p=load('surface-power-payload.py');c=load('pack-sq2-vulkan.py')
def archive(e):return gzip.compress(b''.join(c.entry(n,b,m) for n,(m,b) in e.items())+c.entry('TRAILER!!!',b'',0),mtime=0)
class Power(unittest.TestCase):
 def test_payload_isolated_credentials_and_wrong_abi(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);mod=b'\x7fELF\x00vermagic=fixture\x00resume_count\x00'
   files={'Image':b'kernel','native.efi':b'efi','spi-hid.ko':mod,'system-suspend.sha256':b'a'*64+b'\n'}
   for n,b in files.items():(d/n).write_bytes(b)
   (d/'power-bundle.json').write_text(json.dumps({'policy':'android-persistent-owner','sha256':{n:p.digest(b) for n,b in files.items()}}))
   e={'init':(0o100755,b'#!/bin/sh\nlab-keep-awake > /logs/lab-keep-awake.txt 2>&1 &\n'),
      'lib/modules/fixture/spi-hid.ko':(0o100644,mod),
      'lib/modules/fixture/surface_aggregator.ko':(0o100644,b'unchanged SSAM'),
      'bin/unrelated':(0o100755,b'untouched')}
   identity=archive({'etc/shadow':(0o100600,b'fixture'),'bin/dropbear':(0o100755,b'fixture')})
   result,a=p.integrate(archive(e)+identity,R,d,c);after=c.entries(gzip.decompress(result))
   self.assertTrue(result.endswith(identity));self.assertEqual(after['bin/unrelated'],e['bin/unrelated'])
   self.assertEqual(after['lib/modules/fixture/surface_aggregator.ko'],e['lib/modules/fixture/surface_aggregator.ko'])
   self.assertIn(b'framework-suspend-start',after['init'][1]);self.assertNotIn('etc/surface-pm/desktop-services.jar',after)
   e['lib/modules/fixture/spi-hid.ko']=(0o100644,mod.replace(b'fixture',b'other'))
   with self.assertRaisesRegex(ValueError,'vermagic'):p.integrate(archive(e)+identity,R,d,c)
   e['lib/modules/fixture/spi-hid.ko']=(0o100644,mod)
   e['lib/modules/fixture/surface_aggregator.ko']=(0o100644,b'PM_GRACE_DIAGNOSTIC')
   with self.assertRaisesRegex(ValueError,'SSAM'):p.integrate(archive(e)+identity,R,d,c)
 def test_launcher_both_models_and_signature_rejection(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);raw=bytearray(512);struct.pack_into('<I',raw,60,64);raw[64:68]=b'PE\0\0';struct.pack_into('<H',raw,68,0xaa64)
   strings=[r'\EFI\SQ2Default\android-ip4.dtb',r'\EFI\SQ2Default\qebspilaa64.efi',r'\EFI\SurfacePmAuto\Image',r'\EFI\SQ2Default\tcblaunch.exe',r'console=tty0 suspend.screen_only=1 suspend.screen_only_autosuspend=1 nohibernate initrd=\EFI\SurfacePmAuto\initramfs.cpio.gz']
   for text in strings:raw+=text.encode('utf-16le')+b'\0\0'
   files={'Image':b'kernel','native.efi':bytes(raw),'spi-hid.ko':b'module','system-suspend.sha256':b'a'*64+b'\n'}
   for n,b in files.items():(d/n).write_bytes(b)
   def manifest():(d/'power-bundle.json').write_text(json.dumps({'policy':'android-persistent-owner','sha256':{n:p.digest((d/n).read_bytes()) for n in files}}))
   manifest()
   for model in ('sq1','sq2'):
    b=p.launcher(d,model);self.assertEqual(len(b),len(raw));self.assertIn(('\\EFI\\'+model.upper()+'Default\\Image').encode('utf-16le'),b)
    self.assertIn('suspend.screen_only_autosuspend=1'.encode('utf-16le'),b)
   struct.pack_into('<II',raw,64+24+112+8*4,10,20);(d/'native.efi').write_bytes(raw);manifest()
   with self.assertRaisesRegex(ValueError,'signed'):p.launcher(d,'sq2')
 def test_restore_defaults_uses_current_user_and_keeps_credentials(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);log=d/'calls'
   for name in ('am','cmd','settings'):
    f=d/name;f.write_text('#!/bin/sh\necho "'+name+' $*" >> "$CALLS"\n'+('echo 12\n' if name=='am' else 'exit 0\n'));f.chmod(0o755)
   subprocess.run(['sh',str(R/'scripts/restore-surface-power-defaults.sh')],env={**os.environ,'PATH':str(d)+':'+os.environ['PATH'],'CALLS':str(log)},check=True,stdout=subprocess.PIPE)
   calls=log.read_text();self.assertIn('settings --user 12 delete system screen_off_timeout',calls)
   self.assertIn('cmd lock_settings set-disabled --user 12 false',calls)
   self.assertNotIn('clear',calls);self.assertNotIn('set-pin',calls);self.assertNotIn('set-password',calls)
 def test_entry_policies_retain_fallback(self):
     # Current boot is preserved as a checked 6.18 bundle, not rebuilt through old overlays.
     for name in ['build.py', 'build-surface-pro-x.py', 'build-source-boot.py', 'build-source-disk.py', 'build-sq2-default.py']:
         text = (R/"scripts"/name).read_text()
         self.assertIn("build-lts.py", text)
         self.assertNotIn("6.3.8", text)
if __name__=='__main__':unittest.main()
