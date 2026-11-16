import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
import full_image_reuse as reuse


class ReuseTests(unittest.TestCase):
    def test_prepared_runtime_hash_change_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'runtime.gz';path.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'runtime or source changed'):
                reuse.prepared_runtime({'path':str(path),'sha256':'0'*64,'source_sha256':{}})

    def test_touch_source_change_invalidates_only_wizard(self):
        from unittest.mock import patch
        import full_image
        sources={'probe/touch-calibration/MainActivity.java':'new','probe/storage/resize-userdata-partition.c':'same'}
        prefix='etc/sq2-qti-system/root/'
        names=['lib64/libgrallocutils.so','lib64/sq2-qti-display/libgrallocutils.so','lib64/libsq2_grallocutils_vendor.so',
               'lib64/sq2-qti-display/libsdmcore.so','lib64/sq2-qti-display/libsq2_sdm_backlight.so']
        bodies={prefix+n:b'graphics' for n in names}
        bodies.update({'etc/sq2-touch-calibration/wizard.apk':b'old-apk','bin/resize-userdata-partition':b'resize'})
        receipt={'path':'/fixture','sha256':'same','source_sha256':dict(sources)}
        with patch.object(reuse,'helper_sources',return_value=sources),patch.object(reuse,'digest',return_value='same'),patch.object(full_image,'records_from_gzip',return_value=({},bodies)):
            graphics,helpers=reuse.prepared_runtime(receipt)
            self.assertEqual(helpers['etc/sq2-touch-calibration/wizard.apk'],b'old-apk')
            receipt['source_sha256']['probe/touch-calibration/MainActivity.java']='old'
            changed_graphics,helpers=reuse.prepared_runtime(receipt)
            self.assertEqual(changed_graphics,graphics)
            self.assertEqual(helpers,{'bin/resize-userdata-partition':b'resize'})
            receipt['source_sha256']['probe/storage/resize-userdata-partition.c']='old'
            with self.assertRaisesRegex(ValueError,'runtime or source changed'):reuse.prepared_runtime(receipt)

    def test_reuse_growth_helper_does_not_compile(self):
        from unittest.mock import patch
        import full_image
        records, bodies = {}, {}
        with patch.object(full_image.subprocess,'run',side_effect=AssertionError('unexpected compiler')):
            full_image.install_userdata_growth(records,bodies,compiled_resize=b'explicit completed helper')
        self.assertEqual(bodies['bin/resize-userdata-partition'],b'explicit completed helper')

    def test_inventory_changes_detected_before_loading_binaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            roots = {k:root/k for k in ('kernel','camera','sensors','mesa','glue')}
            names = {'kernel':['Image','kernel.config','Module.symvers','System.map','build-audit.json',
                               'modules-stage/lib/modules/test/sample.ko'],
                     'camera':['CAMERA.json'], 'sensors':['sensors-bundle.json'],
                     'mesa':[*reuse.MESA, 'vulkan.turnip.so'], 'glue':list(reuse.GLUE.values())}
            for role, files in names.items():
                for n in files:
                    p=roots[role]/n; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(b'fixture')
            manifest=root/'reuse.json'
            manifest.write_text(json.dumps({'schema':1,'roots':{k:str(v) for k,v in roots.items()},
                                            'sha256':reuse.inventory(roots)}))
            (roots['kernel']/'Image').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'hash/inventory mismatch'): reuse.load(manifest)

    def test_missing_or_linked_input_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); target=root/'file'; target.write_bytes(b'x')
            other=root/'camera'; other.mkdir(); (other/'CAMERA.json').symlink_to(target)
            with self.assertRaisesRegex(ValueError,'regular reuse input'): reuse.inventory({'camera':other})


if __name__ == '__main__': unittest.main()
