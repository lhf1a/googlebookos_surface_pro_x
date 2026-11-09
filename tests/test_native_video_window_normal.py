"""Exercise the normal runner's real environment import without Android services."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class NormalVideoWindow(unittest.TestCase):
    def test_zygote_paths_are_literal_allowlisted_and_required(self):
        source=(ROOT/"probe/video/native-window/run-native-video-window-normal.sh").read_text()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);proc=root/"proc/123";proc.mkdir(parents=True)
            probe=root/"probe";probe.mkdir()
            for name in ("sq2-native-video-window.jar","libsq2-native-video-window.so"):
                (probe/name).touch()
            binary=root/"bin";binary.mkdir()
            pidof=binary/"pidof";pidof.write_text("#!/bin/sh\nprintf '123\\n'\n");pidof.chmod(0o700)
            app=binary/"app_process"
            app.write_text("#!/usr/bin/python3\nimport json,os,sys\n"
                "print(json.dumps({'args':sys.argv[1:],'boot':os.environ.get('BOOTCLASSPATH'),"
                "'root':os.environ.get('ANDROID_ROOT'),'secret':os.environ.get('ZYGOTE_PRIVATE'),"
                "'dex':os.environ.get('DEX2OATBOOTCLASSPATH'),'classpath':os.environ.get('CLASSPATH')}))\n")
            app.chmod(0o700)
            runner=root/"run.sh"
            runner.write_text(source.replace('"/proc/$zygote/environ"',str(proc/"environ"))
                .replace("/system/bin/app_process",str(app)))
            marker=root/"should-not-exist"
            literal='/system/a.jar:$(touch '+str(marker)+')'
            (proc/"environ").write_bytes(("BOOTCLASSPATH="+literal+"\0ANDROID_ROOT=/system\0"
                "DEX2OATBOOTCLASSPATH=/apex/dex.jar\0ZYGOTE_PRIVATE=do-not-import\0").encode())
            env={**os.environ,"PATH":str(binary)+":"+os.environ['PATH'],"BOOTCLASSPATH":"stale"}
            env.pop('ZYGOTE_PRIVATE',None)
            result=subprocess.run(["sh",str(runner),str(probe),"2880","1920","0"],env=env,
                text=True,capture_output=True,check=True)
            data=json.loads(result.stdout)
            self.assertEqual(data['boot'],literal);self.assertFalse(marker.exists())
            self.assertEqual(data['root'],'/system');self.assertEqual(data['dex'],'/apex/dex.jar')
            self.assertIsNone(data['secret'])
            self.assertEqual(data['classpath'],str(probe/'sq2-native-video-window.jar'))
            self.assertEqual(data['args'],['/system/bin','Sq2NativeVideoWindow',
                str(probe/'libsq2-native-video-window.so'),'2880','1920','0','baseline'])
            for scene in ('scale-up-left','scale-down-right','occlusion','fullscreen'):
                selected=subprocess.run(["sh",str(runner),str(probe),"2880","1920","0",scene],
                    env=env,text=True,capture_output=True,check=True)
                self.assertEqual(json.loads(selected.stdout)['args'][-1],scene)
            masked=subprocess.run(["sh",str(runner),str(probe),"2880","1920","0","fullscreen-mask","68"],
                env=env,text=True,capture_output=True,check=True)
            self.assertEqual(json.loads(masked.stdout)['args'][-2:],['fullscreen-mask','68'])
            for tail in (['fullscreen-mask'],['fullscreen-mask','bad'],['fullscreen','68']):
                invalid_mask=subprocess.run(["sh",str(runner),str(probe),"2880","1920","0"]+tail,
                    env=env,text=True,capture_output=True)
                self.assertEqual(invalid_mask.returncode,2);self.assertEqual(invalid_mask.stdout,'')
            rejected=subprocess.run(["sh",str(runner),str(probe),"2880","1920","0","invalid"],
                env=env,text=True,capture_output=True)
            self.assertEqual(rejected.returncode,2);self.assertEqual(rejected.stdout,'')
            (proc/"environ").write_bytes(b"ANDROID_ROOT=/system\0")
            failed=subprocess.run(["sh",str(runner),str(probe),"2880","1920","0"],env=env,
                text=True,capture_output=True)
            self.assertEqual(failed.returncode,2);self.assertEqual(failed.stdout,'')
            self.assertIn('Zygote BOOTCLASSPATH unavailable',failed.stderr)

if __name__=='__main__':unittest.main()
