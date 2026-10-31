"""Run the real boot/session shell against an isolated filesystem and Android stub."""
import hashlib,os,pathlib,subprocess,tempfile,time,unittest
R=pathlib.Path(__file__).resolve().parents[1]

class Boot(unittest.TestCase):
    def test_first_run_preview_save_reboot_timeout_skip_and_wrong_device(self):
        with tempfile.TemporaryDirectory() as td:
            w=pathlib.Path(td)
            def put(path,body,mode=0o644):
                p=w/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body);p.chmod(mode);return p
            put('sys/class/dmi/id/product_uuid','test-board\n')
            put('sys/class/drm/card0-eDP-1/edid','x'*128)
            put('sys/class/hidraw/hidraw0/device/uevent','HID_ID=001C:0000045E:00000C21\n')
            put('sys/class/hidraw/hidraw0/device/report_descriptor','test-hid')
            put('logs/power-input-boot.txt','SQ2_COVER_AND_POWER_INPUT_READY\n')
            put('etc/iptsd.conf','[Contacts]\nAspectMax = 2.0\n')
            put('etc/sq2-touch-calibration/wizard.apk','apk')
            (w/'run').mkdir();(w/'android/data/local/tmp').mkdir(parents=True)
            files=w/'android/data/user_de/10/org.surface.touchcalibration/files'
            files.mkdir(parents=True)
            put('bin/android',f'''#!/usr/bin/python3
import sys,pathlib
a=sys.argv[1:]
if a[:1]==['getprop']:print('1')
elif a[:2]==['cmd','activity']:print('10')
elif a[:2]==['am','start']:pathlib.Path({str(w/'launched')!r}).touch()
''',0o755)
            put('bin/touch-framework-ready',f'#!/bin/sh\ncat "{w}/run/iptsd-calibrated.conf" > "{w}/applied"\n',0o755)
            put('bin/touch-services','#!/bin/sh\nexit 0\n',0o755)
            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/profile-tool.cpp'),'-o',str(w/'bin/touch-calibration-profile')],check=True)
            script=(R/'probe/test2/touch-calibration-boot').read_text()
            script=script.replace('android() { timeout "${ANDROID_COMMAND_TIMEOUT:-15}" nsenter -t 1 -m -r -- /system/bin/"$@"; }',f'android() {{ "{w}/bin/android" "$@"; }}')
            for prefix in ('/proc/1/root','/sys','/run','/logs','/etc'):
                script=script.replace(prefix,str(w/('android' if prefix=='/proc/1/root' else prefix[1:])))
            script=script.replace('/bin/touch-',str(w/'bin/touch-')).replace('sleep 2','sleep 0.02').replace('+ 180','+ 2')
            runner=put('runner',script,0o755)
            device=hashlib.sha256(b'test-board\n'+b'x'*128+b'test-hid').hexdigest()
            store=w/'android/data/vendor/sq2-touch-calibration';saved=store/(device+'.profile')
            profile=f'Version=1\nDevice={device}\nCalibrationX=0:0 .02:.03 .12:.12 1:1\nCalibrationY=\nCalibrationXTop=\nCalibrationTopStart=0.1\nCalibrationTopEnd=0.3\n'
            processes=[]
            def wait_for(fn):
                end=time.time()+8
                while time.time()<end:
                    if fn():return
                    time.sleep(.025)
                self.fail('Timed out; '+(w/'session.log').read_text())
            def start():
                log=(w/'session.log').open('w');p=subprocess.Popen(['sh',str(runner)],stdout=log,stderr=log);processes.append((p,log));wait_for(lambda:(files/'context').exists());return p
            def stop(p):
                p.terminate();p.wait(timeout=8)
                (files/'context').unlink(missing_ok=True)
            count=0
            def request(command,body=''):
                nonlocal count
                count+=1;nonce=f'request-{count}';temp=files/'request.tmp';temp.write_text(command+'\n'+nonce+'\n'+body);temp.rename(files/'request')
                wait_for(lambda:(files/'reply').exists() and (files/'reply').read_text().startswith(nonce+'\n'))
                return (files/'reply').read_text().splitlines()[1]
            try:
                p=start();self.assertTrue((w/'launched').exists());self.assertFalse(saved.exists())
                self.assertEqual(request('preview',profile),'OK');self.assertIn('.02:.03',(w/'applied').read_text())
                self.assertEqual(request('save'),'OK');self.assertEqual(saved.read_text(),profile)
                stop(p);(w/'launched').unlink();p=start();self.assertFalse((w/'launched').exists());self.assertIn('.02:.03',(w/'applied').read_text())
                bad=profile.replace(device,'b'*64);self.assertEqual(request('preview',bad),'ERROR');self.assertEqual(saved.read_text(),profile)
                identity=profile.replace('0:0 .02:.03 .12:.12 1:1','')
                self.assertEqual(request('preview',identity),'OK');wait_for(lambda:'.02:.03' in (w/'applied').read_text())
                self.assertEqual(saved.read_text(),profile);stop(p)
                # Optional thumb limit is temporary until saved, bound to the same device.
                p=start();optimized=profile+'ContactSizeMax=2.4\n'
                self.assertIn('SizeMax = 2.0',(w/'applied').read_text())
                self.assertEqual(request('preview',optimized),'OK');self.assertIn('SizeMax = 2.4',(w/'applied').read_text())
                self.assertEqual(request('cancel'),'OK');self.assertIn('SizeMax = 2.0',(w/'applied').read_text());self.assertEqual(saved.read_text(),profile)
                self.assertEqual(request('preview',optimized),'OK')
                wait_for(lambda:'SizeMax = 2.0' in (w/'applied').read_text())
                self.assertEqual(saved.read_text(),profile)
                self.assertEqual(request('preview',optimized.replace('2.4','2.5')),'ERROR')
                self.assertEqual(request('preview',optimized),'OK');self.assertEqual(request('save'),'OK');stop(p)
                p=start();self.assertEqual(saved.read_text(),optimized);self.assertIn('SizeMax = 2.4',(w/'applied').read_text());stop(p)
                saved.unlink();(store/(device+'.prompted')).unlink();p=start();self.assertTrue((w/'launched').exists())
                self.assertEqual(request('skip'),'OK');stop(p);(w/'launched').unlink();p=start();self.assertFalse((w/'launched').exists());stop(p)
            finally:
                for p,log in processes:
                    if p.poll() is None:p.terminate();p.wait(timeout=8)
                    log.close()

if __name__=='__main__':unittest.main()
