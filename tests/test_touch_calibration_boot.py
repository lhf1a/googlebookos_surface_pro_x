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
            put('sys/class/drm/card7-eDP-1/edid','x'*128)
            put('sys/class/drm/card7-eDP-1/status','connected\n')
            put('sys/class/hidraw/hidraw19/device/uevent','HID_ID=001C:0000045E:00000996\n')
            put('sys/class/hidraw/hidraw19/device/report_descriptor','test-hid')
            put('logs/power-input-boot.txt','SQ2_COVER_AND_POWER_INPUT_READY\n')
            put('etc/iptsd.conf','[Contacts]\nAspectMax = 2.0\n')
            put('etc/sq2-touch-calibration/wizard.apk','apk')
            put('run/iptsd-hidraw','/dev/hidraw19\n')
            (w/'android/data/local/tmp').mkdir(parents=True)
            files=w/'android/data/user_de/10/org.surface.touchcalibration.unified/files'
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
            device=hashlib.sha256(b'test-board\n'+b'x'*128+b'HID_ID=001C:0000045E:00000996\n'+b'test-hid').hexdigest()
            store=w/'android/data/vendor/sq2-touch-calibration';saved=store/(device+'.profile')
            profile=f'Version=1\nDevice={device}\nCalibrationX=0:0 .02:.03 .12:.12 1:1\nCalibrationY=\nCalibrationXTop=\nCalibrationTopStart=0.1\nCalibrationTopEnd=0.3\n'
            processes=[]
            def wait_for(fn):
                end=time.monotonic()+8
                while time.monotonic()<end:
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

    def test_legacy_pre_hid_id_calibration_migrates_without_reprompt(self):
        """Upgrading from a pre-HID_ID build must migrate saved profiles and never re-launch the wizard."""
        with tempfile.TemporaryDirectory() as td:
            w=pathlib.Path(td)
            def put(path,body,mode=0o644):
                p=w/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body);p.chmod(mode);return p
            put('sys/class/dmi/id/product_uuid','test-board\n')
            put('sys/class/drm/card7-eDP-1/edid','x'*128)
            put('sys/class/drm/card7-eDP-1/status','connected\n')
            put('sys/class/hidraw/hidraw19/device/uevent','HID_ID=001C:0000045E:00000C21\n')
            put('sys/class/hidraw/hidraw19/device/report_descriptor','test-hid')
            put('logs/power-input-boot.txt','SQ2_COVER_AND_POWER_INPUT_READY\n')
            put('etc/iptsd.conf','[Contacts]\nAspectMax = 2.0\n')
            put('etc/sq2-touch-calibration/wizard.apk','apk')
            put('run/iptsd-hidraw','/dev/hidraw19\n')
            (w/'android/data/local/tmp').mkdir(parents=True)
            files=w/'android/data/user_de/10/org.surface.touchcalibration.unified/files'
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
            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/noise-tool.cpp'),'-o',str(w/'bin/touch-noise-profile')],check=True)
            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/tap-tool.cpp'),'-o',str(w/'bin/touch-tap-profile')],check=True)
            script=(R/'probe/test2/touch-calibration-boot').read_text()
            script=script.replace('android() { timeout "${ANDROID_COMMAND_TIMEOUT:-15}" nsenter -t 1 -m -r -- /system/bin/"$@"; }',f'android() {{ "{w}/bin/android" "$@"; }}')
            for prefix in ('/proc/1/root','/sys','/run','/logs','/etc'):
                script=script.replace(prefix,str(w/('android' if prefix=='/proc/1/root' else prefix[1:])))
            script=script.replace('/bin/touch-',str(w/'bin/touch-')).replace('sleep 2','sleep 0.02').replace('+ 180','+ 2')
            runner=put('runner',script,0o755)

            device=hashlib.sha256(b'test-board\n'+b'x'*128+b'HID_ID=001C:0000045E:00000C21\n'+b'test-hid').hexdigest()
            legacy=hashlib.sha256(b'test-board\n'+b'x'*128+b'test-hid').hexdigest()
            self.assertNotEqual(device, legacy)

            store=w/'android/data/vendor/sq2-touch-calibration'
            store.mkdir(parents=True,exist_ok=True)
            legacy_profile=f'Version=1\nDevice={legacy}\nCalibrationX=0:0 .02:.03 .12:.12 1:1\nCalibrationY=\nCalibrationXTop=\nCalibrationTopStart=0.1\nCalibrationTopEnd=0.3\n'
            legacy_noise=f'SPXNOISE1 {legacy} 1118 3105 32 48 1 10 0.25\n'
            legacy_tap=f'SPXTAP1 {legacy} 1118 3105 32 48 2\n'
            (store/(legacy+'.profile')).write_text(legacy_profile)
            (store/(legacy+'.noise')).write_text(legacy_noise)
            (store/(legacy+'.tap')).write_text(legacy_tap)
            (store/(legacy+'.prompted')).touch()

            processes=[]
            def wait_for(fn):
                end=time.monotonic()+8
                while time.monotonic()<end:
                    if fn():return
                    time.sleep(.025)
                self.fail('Timed out; '+(w/'session.log').read_text())
            def start():
                log=(w/'session.log').open('w');p=subprocess.Popen(['sh',str(runner)],stdout=log,stderr=log);processes.append((p,log));wait_for(lambda:(files/'context').exists());return p
            def stop(p):
                p.terminate();p.wait(timeout=8)
                (files/'context').unlink(missing_ok=True)

            try:
                # 1. Boot after OTA: migrates profile + noise + tap, applies them, never launches wizard.
                p=start()
                self.assertFalse((w/'launched').exists())
                self.assertTrue((store/(device+'.profile')).is_file())
                self.assertTrue((store/(device+'.noise')).is_file())
                self.assertTrue((store/(device+'.tap')).is_file())
                self.assertTrue((store/(device+'.prompted')).exists())
                self.assertTrue((store/(device+'.migrated-v2')).exists())
                self.assertIn(f'Device={device}\n', (store/(device+'.profile')).read_text())
                self.assertIn('.02:.03', (w/'applied').read_text())
                self.assertIn(f'SPXNOISE1 {device} ', (w/'run/iptsd-noise/active').read_text())
                self.assertIn(f'SPXTAP1 {device} ', (w/'run/iptsd-noise/tap-active').read_text())
                # Original legacy files remain intact for rollback.
                self.assertEqual((store/(legacy+'.profile')).read_text(), legacy_profile)
                self.assertEqual((store/(legacy+'.noise')).read_text(), legacy_noise)
                self.assertEqual((store/(legacy+'.tap')).read_text(), legacy_tap)
                stop(p)

                # 1b. Partial new-key state (e.g. only .noise existed from an earlier unfixed OTA,
                # plus stale v1 .migrated marker): must preserve existing new .noise AND still migrate
                # missing .profile, .tap, and .prompted without launching the wizard.
                for f in list(store.iterdir()):
                    f.unlink()
                (store/(legacy+'.profile')).write_text(legacy_profile)
                (store/(legacy+'.noise')).write_text(legacy_noise)
                (store/(legacy+'.tap')).write_text(legacy_tap)
                (store/(legacy+'.prompted')).touch()
                custom_new_noise = f'SPXNOISE1 {device} 1118 3105 32 48 1 20 0.5\n'
                (store/(device+'.noise')).write_text(custom_new_noise)
                (store/(device+'.migrated')).touch()
                p=start()
                self.assertFalse((w/'launched').exists())
                self.assertEqual((store/(device+'.noise')).read_text(), custom_new_noise)
                self.assertTrue((store/(device+'.profile')).is_file())
                self.assertTrue((store/(device+'.tap')).is_file())
                self.assertTrue((store/(device+'.prompted')).exists())
                self.assertTrue((store/(device+'.migrated-v2')).exists())
                self.assertIn('.02:.03', (w/'applied').read_text())
                stop(p)

                # 2. If user clears new-key calibration later (keeping .prompted & .migrated), legacy must not resurrect.
                (store/(device+'.profile')).unlink()
                (store/(device+'.noise')).unlink()
                (store/(device+'.tap')).unlink()
                (w/'run/iptsd-noise/active').unlink(missing_ok=True)
                (w/'run/iptsd-noise/tap-active').unlink(missing_ok=True)
                p=start()
                self.assertFalse((w/'launched').exists())
                self.assertFalse((store/(device+'.profile')).exists())
                self.assertFalse((store/(device+'.noise')).exists())
                self.assertFalse((store/(device+'.tap')).exists())
                stop(p)

                # 3. Legacy skip-only (.prompted without .profile) also suppresses wizard on OTA upgrade.
                for f in list(store.iterdir()):
                    f.unlink()
                (store/(legacy+'.prompted')).touch()
                p=start()
                self.assertFalse((w/'launched').exists())
                self.assertTrue((store/(device+'.prompted')).exists())
                stop(p)

                # 4. Corrupt legacy profile does not crash touch-calibration-boot under set -eu.
                for f in list(store.iterdir()):
                    f.unlink()
                (store/(legacy+'.profile')).write_text('CORRUPT_PROFILE_DATA\n')
                p=start()
                self.assertTrue((w/'launched').exists())
                self.assertFalse((store/(device+'.profile')).exists())
                stop(p)

                # 5. Legacy skip-with-disabled-noise (.noise.disabled without .noise) migrates cleanly.
                for f in list(store.iterdir()):
                    f.unlink()
                (w/'launched').unlink(missing_ok=True)
                (store/(legacy+'.noise.disabled')).touch()
                p=start()
                self.assertFalse((w/'launched').exists())
                self.assertTrue((store/(device+'.noise.disabled')).exists())
                self.assertTrue((store/(device+'.prompted')).exists())
                stop(p)
            finally:
                for p,log in processes:
                    if p.poll() is None:p.terminate();p.wait(timeout=8)
                    log.close()

    def test_skip_disables_ghost_patch_and_suppresses_future_prompts(self):
        """Confirming healthy-screen skip disables ghost-touch patch, reloads iptsd, and stops re-prompting."""
        with tempfile.TemporaryDirectory() as td:
            w=pathlib.Path(td)
            def put(path,body,mode=0o644):
                p=w/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body);p.chmod(mode);return p
            put('sys/class/dmi/id/product_uuid','test-board\n')
            put('sys/class/drm/card7-eDP-1/edid','x'*128)
            put('sys/class/drm/card7-eDP-1/status','connected\n')
            put('sys/class/hidraw/hidraw19/device/uevent','HID_ID=001C:0000045E:00000C21\n')
            put('sys/class/hidraw/hidraw19/device/report_descriptor','test-hid')
            put('logs/power-input-boot.txt','SQ2_COVER_AND_POWER_INPUT_READY\n')
            put('etc/iptsd.conf','[Contacts]\nAspectMax = 2.0\n')
            put('etc/sq2-touch-calibration/wizard.apk','apk')
            put('run/iptsd-hidraw','/dev/hidraw19\n')
            (w/'android/data/local/tmp').mkdir(parents=True)
            files=w/'android/data/user_de/10/org.surface.touchcalibration.unified/files'
            files.mkdir(parents=True)
            put('bin/android',f'''#!/usr/bin/python3
import sys,pathlib
a=sys.argv[1:]
if a[:1]==['getprop']:print('1')
elif a[:2]==['cmd','activity']:print('10')
elif a[:2]==['am','start']:pathlib.Path({str(w/'launched_args')!r}).write_text(' '.join(a)+'\\n')
''',0o755)
            put('bin/touch-framework-ready',f'#!/bin/sh\ncat "{w}/run/iptsd-calibrated.conf" > "{w}/applied"\n',0o755)
            put('bin/touch-services','#!/bin/sh\nexit 0\n',0o755)
            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/profile-tool.cpp'),'-o',str(w/'bin/touch-calibration-profile')],check=True)
            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/noise-tool.cpp'),'-o',str(w/'bin/touch-noise-profile')],check=True)
            script=(R/'probe/test2/touch-calibration-boot').read_text()
            script=script.replace('android() { timeout "${ANDROID_COMMAND_TIMEOUT:-15}" nsenter -t 1 -m -r -- /system/bin/"$@"; }',f'android() {{ "{w}/bin/android" "$@"; }}')
            for prefix in ('/proc/1/root','/sys','/run','/logs','/etc'):
                script=script.replace(prefix,str(w/('android' if prefix=='/proc/1/root' else prefix[1:])))
            script=script.replace('/bin/touch-',str(w/'bin/touch-')).replace('sleep 2','sleep 0.02').replace('+ 180','+ 2')
            # Simulate iptsd reload ACK when noise/version exists.
            script=script.replace('noise_reload() {', 'noise_reload() {\n    noise_cmd reload "$1"\n    printf \'%s applied 100 0\\n\' "$1" > "$noise/status"; return 0\n')
            runner=put('runner',script,0o755)

            device=hashlib.sha256(b'test-board\n'+b'x'*128+b'HID_ID=001C:0000045E:00000C21\n'+b'test-hid').hexdigest()
            store=w/'android/data/vendor/sq2-touch-calibration'
            noise=w/'run/iptsd-noise'
            processes=[]
            def wait_for(fn):
                end=time.monotonic()+8
                while time.monotonic()<end:
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
                count+=1;nonce=f'req-{count}';temp=files/'request.tmp';temp.write_text(command+'\n'+nonce+'\n'+body);temp.rename(files/'request')
                wait_for(lambda:(files/'reply').exists() and (files/'reply').read_text().startswith(nonce+'\n'))
                return (files/'reply').read_text().splitlines()[1]
            try:
                # 1. First boot auto-launches wizard with first_boot extra.
                p=start()
                self.assertTrue((w/'launched_args').exists())
                self.assertIn('--ez first_boot true', (w/'launched_args').read_text())
                (noise/'version').write_text('7\n')

                # 2. User confirms healthy-screen skip: disables ghost patch, reloads iptsd, marks prompted.
                self.assertEqual(request('skip'), 'OK')
                self.assertTrue((store/(device+'.noise.disabled')).exists())
                self.assertTrue((store/(device+'.prompted')).exists())
                self.assertFalse((noise/'active').exists())
                self.assertEqual((noise/'command').read_text().strip(), f'reload {device} req-1')
                stop(p)

                # 3. Subsequent reboot does NOT pop up calibration again.
                (w/'launched_args').unlink()
                p=start()
                self.assertFalse((w/'launched_args').exists())
                self.assertTrue((store/(device+'.noise.disabled')).exists())
                stop(p)

                # 4. If a saved .noise file existed before skip, skip disables it without deleting it,
                #    clears active in RAM, and reboot keeps it disabled until re-enabled in Advanced Settings.
                sample_noise=f'SPXNOISE1 {device} 1118 3105 32 48 1 10 0.25\n'
                (store/(device+'.noise')).write_text(sample_noise)
                (store/(device+'.noise.disabled')).unlink()
                p=start()
                self.assertTrue((noise/'active').is_file())
                self.assertEqual(request('skip'), 'OK')
                self.assertTrue((store/(device+'.noise')).is_file())
                self.assertTrue((store/(device+'.noise.disabled')).exists())
                self.assertFalse((noise/'active').exists())
                stop(p)

                p=start()
                self.assertFalse((w/'launched_args').exists())
                self.assertFalse((noise/'active').exists())

                # 5. 'complete' marks prompted without disabling an active ghost-touch patch.
                (store/(device+'.noise.disabled')).unlink()
                (store/(device+'.prompted')).unlink()
                stop(p)
                p=start()
                self.assertTrue((noise/'active').is_file())
                self.assertEqual(request('complete'), 'OK')
                self.assertTrue((store/(device+'.prompted')).exists())
                self.assertFalse((store/(device+'.noise.disabled')).exists())
                self.assertTrue((noise/'active').is_file())
                stop(p)
            finally:
                for p,log in processes:
                    if p.poll() is None:p.terminate();p.wait(timeout=8)
                    log.close()

    def test_noise_learn_publishes_learning_immediately_and_wakes_fast_with_event_wait(self):
        """Regression: noise-learn must publish 'learning' before reply OK and propagate 'ready' promptly under spx-event-wait."""
        with tempfile.TemporaryDirectory() as td:
            w=pathlib.Path(td)
            def put(path,body,mode=0o644):
                p=w/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(body);p.chmod(mode);return p
            put('sys/class/dmi/id/product_uuid','test-board\n')
            put('sys/class/drm/card7-eDP-1/edid','x'*128)
            put('sys/class/drm/card7-eDP-1/status','connected\n')
            put('sys/class/hidraw/hidraw19/device/uevent','HID_ID=001C:0000045E:00000996\n')
            put('sys/class/hidraw/hidraw19/device/report_descriptor','test-hid')
            put('logs/power-input-boot.txt','SQ2_COVER_AND_POWER_INPUT_READY\n')
            put('etc/iptsd.conf','[Contacts]\nAspectMax = 2.0\n')
            put('etc/sq2-touch-calibration/wizard.apk','apk')
            put('run/iptsd-hidraw','/dev/hidraw19\n')
            put('run/iptsd-noise/version','7\n')
            (w/'android/data/local/tmp').mkdir(parents=True)
            files=w/'android/data/user_de/10/org.surface.touchcalibration.unified/files'
            files.mkdir(parents=True)
            noise=w/'run/iptsd-noise'
            put('bin/android','''#!/usr/bin/python3
import sys
a=sys.argv[1:]
if a[:1]==['getprop']:print('1')
elif a[:2]==['cmd','activity']:print('10')
''',0o755)
            put('bin/touch-framework-ready','#!/bin/sh\nexit 0\n',0o755)
            put('bin/touch-services','#!/bin/sh\nexit 0\n',0o755)
            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/profile-tool.cpp'),'-o',str(w/'bin/touch-calibration-profile')],check=True)
            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/noise-tool.cpp'),'-o',str(w/'bin/touch-noise-profile')],check=True)
            subprocess.run(['gcc','-std=c11','-O2',str(R/'probe/test2/spx-event-wait.c'),'-o',str(w/'bin/spx-event-wait')],check=True)

            script=(R/'probe/test2/touch-calibration-boot').read_text()
            script=script.replace('export PATH=/bin:/sbin', f'export PATH="{w}/bin:/usr/bin:/bin:/sbin"')
            script=script.replace('android() { timeout "${ANDROID_COMMAND_TIMEOUT:-15}" nsenter -t 1 -m -r -- /system/bin/"$@"; }',f'android() {{ "{w}/bin/android" "$@"; }}')
            for prefix in ('/proc/1/root','/sys','/run','/logs','/etc'):
                script=script.replace(prefix,str(w/('android' if prefix=='/proc/1/root' else prefix[1:])))
            script=script.replace('/bin/touch-',str(w/'bin/touch-'))
            runner=put('runner',script,0o755)

            # Stub iptsd noise worker: responds to learn/reload commands and finishes learn after 0.6s.
            stop_stub = {'flag': False}
            import threading
            def iptsd_stub():
                last_cmd = ''
                learn_nonce = None
                learn_finish_at = 0.0
                while not stop_stub['flag']:
                    cmd_file = noise / 'command'
                    if cmd_file.is_file():
                        txt = cmd_file.read_text().strip()
                        if txt and txt != last_cmd:
                            last_cmd = txt
                            parts = txt.split()
                            if len(parts) == 3:
                                op, _dev, n = parts
                                if op == 'reload':
                                    learn_nonce = None
                                    tmp = noise / 'status.new'
                                    tmp.write_text(f'{n} applied 0 0\n')
                                    tmp.rename(noise / 'status')
                                elif op == 'learn':
                                    learn_nonce = n
                                    learn_finish_at = time.monotonic() + 0.6
                                    tmp = noise / 'status.new'
                                    tmp.write_text(f'{n} learning 10 0\n')
                                    tmp.rename(noise / 'status')
                    if learn_nonce and time.monotonic() >= learn_finish_at:
                        n = learn_nonce
                        learn_nonce = None
                        tmp = noise / 'status.new'
                        tmp.write_text(f'{n} ready 4505 0\nabnormal_frames=12\n')
                        tmp.rename(noise / 'status')
                    time.sleep(0.05)

            import signal, threading
            stub_t = threading.Thread(target=iptsd_stub, daemon=True)
            stub_t.start()
            log = (w / 'session.log').open('w')
            p = subprocess.Popen(['sh', str(runner)], stdout=log, stderr=log, start_new_session=True)
            try:
                end = time.monotonic() + 8
                while time.monotonic() < end and not (files / 'context').exists():
                    time.sleep(0.05)
                self.assertTrue((files / 'context').exists(), (w / 'session.log').read_text())

                # Send noise-learn request; MUST reply OK AND already have published '<nonce> learning' immediately!
                nonce = 'learn-test-1'
                t0 = time.monotonic()
                tmp = files / 'request.tmp'
                tmp.write_text(f'noise-learn\n{nonce}\n')
                tmp.rename(files / 'request')
                end = time.monotonic() + 5
                while time.monotonic() < end:
                    if (files / 'reply').exists() and (files / 'reply').read_text().startswith(nonce + '\n'):
                        break
                    time.sleep(0.02)
                self.assertTrue((files / 'reply').exists() and (files / 'reply').read_text().startswith(nonce + '\nOK'))
                # Immediately upon reply OK, noise-status MUST already reflect '<nonce> learning' (no 20s stall!)
                ns_text = (files / 'noise-status').read_text()
                self.assertTrue(ns_text.startswith(f'{nonce} learning '), f'Expected immediate learning status, got: {ns_text!r}')
                self.assertLess(time.monotonic() - t0, 3.0)

                # Once stub transitions to 'ready' (~0.6s later), noise-status MUST propagate within <2.5s (not 20s!)
                t_ready_wait = time.monotonic()
                while time.monotonic() - t_ready_wait < 3.0:
                    ns_text = (files / 'noise-status').read_text()
                    if ns_text.startswith(f'{nonce} ready 4505 0'):
                        break
                    time.sleep(0.05)
                self.assertTrue(ns_text.startswith(f'{nonce} ready 4505 0'), f'Timed out waiting for ready status; last: {ns_text!r}')
                self.assertLess(time.monotonic() - t_ready_wait, 2.5)
            finally:
                stop_stub['flag'] = True
                if p.poll() is None:
                    os.killpg(p.pid, signal.SIGTERM)
                    p.wait(timeout=8)
                stub_t.join(timeout=2)
                log.close()

if __name__=='__main__':unittest.main()
