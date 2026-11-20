"""Sensor integration exercises the real full-image archive encoder."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import full_image as image
import full_image_rescue as rescue
import full_image_sensors as sensors

class SensorAssembly(unittest.TestCase):
    def test_archive_hooks_payload_and_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            private=Path(directory);runtime=private/'full-image-runtime'
            bodies={'init': b'#!/bin/sh\n'+rescue.LEGACY_RESCUE_BLOCK+sensors.STAGE+image.ANCHOR,
                    'etc/shadow':b'identity fixture','bin/dropbear':b'ssh fixture',
                    'bin/start-ssh':b'start fixture','etc/unrelated':b'preserve'}
            for name,data in bodies.items():
                p=runtime/'files'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
            (runtime/'records.json').write_text(json.dumps({n:image.new_record(0o100644) for n in bodies}))
            files={n:('new '+n).encode() for n in sensors.FILES}
            files['config/config_list.txt']=b'model.json\n';files['config/model.json']=b'{}'
            loader=image.module
            def load(name,filename):
                if filename=='build-touch-calibration.py':
                    return SimpleNamespace(build=lambda root:({'etc/sq2-touch-calibration/wizard.apk':(0o100644,b'wizard')},{}))
                if filename=='prepare-sq2-primary-audio.py':
                    return SimpleNamespace(install=lambda bodies:None)
                return loader(name,filename)
            output=private/'runtime.gz'
            with patch.object(image,'module',side_effect=load):
                image.assemble_runtime(private,output,graphics=None,sensors=files)
            records,result=image.records_from_gzip(output)
            for n in ('etc/shadow','bin/dropbear','etc/unrelated'):self.assertEqual(result[n],bodies[n])
            for name,target in sensors.payload_paths(files).items():self.assertEqual(result[target],files[name])
            self.assertLess(result['init'].index(sensors.BOOT_HOOK),result['init'].index(sensors.BOOT))
            self.assertLess(result['init'].index(sensors.STAGE),result['init'].index(sensors.STAGE_HOOK))
            self.assertIn(b'config.override_forced_orient=true',result['bin/spx-sensors-stage'])
            self.assertNotIn(b'settings put',result['bin/spx-sensors-stage'])
            before=dict(result);sensors.install(records,result,files,image.new_record);self.assertEqual(before,result)
            self.assertFalse(any('subhal-test' in name or 'ssc-debug' in name for name in result))
    def test_fresh_kernel_build_replaces_all_three_modules(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);files={n:b'old' for n in sensors.FILES};commands=[]
            def run(command):
                commands.append(list(map(str,command)));dest=Path(command[-1]);dest.mkdir()
                for n in ('spx-tcs3430','spx-slpi-pas','fastrpc'):(dest/(n+'.ko')).write_bytes(b'new '+n.encode())
            sensors.rebuild_modules(files,Path('/source'),Path('/build'),output,'test-cross-',run)
            for name in ('spx-tcs3430','spx-slpi-pas','fastrpc'):self.assertEqual(files['modules/'+name+'.ko'],b'new '+name.encode())
            self.assertTrue(all('CROSS_COMPILE=test-cross-' in c for c in commands))
    def test_manifest_rejects_non_model_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            names=set(sensors.FILES)|{'config/config_list.txt','config/../../unit.json'}
            (root/'sensors-bundle.json').write_text(json.dumps({'schema':1,'sha256':dict.fromkeys(names,'unused')}))
            with self.assertRaisesRegex(ValueError,'Unexpected'):sensors.load(root)

if __name__=='__main__':unittest.main()
