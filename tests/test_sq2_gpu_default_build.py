"""Default SQ2 GPU profile, independent rescue and pre-disk deployment gates."""
import copy
import importlib.util
import os
from pathlib import Path
import unittest
from unittest import mock

R = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), R/'scripts'/(name+'.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


g = load('sq2-gpu-boot-payload')
v = load('sq2-video-boot-payload')
u = load('sq2-usb-boot-payload')
deploy = load('deploy-experimental-usb')
BASE = Path(os.environ.get('SQ2_GPU_BASELINE', R/'out/source-boot'/g.NORMAL))
RESCUE = Path(os.environ.get('SQ2_GPU_RESCUE', R/'out/source-boot'/g.RESCUE))


def menu():
    return u.default_menu(v.default_menu((R/'configs/grub-source.cfg').read_text())).encode()


class DefaultGpu(unittest.TestCase):
    def test_unknown_baseline_and_payloads_rejected(self):
        with self.assertRaises(ValueError):
            g.build(b'unknown SQ1 or modified DTB')
        with self.assertRaises(ValueError):
            g.verify(b'normal', b'rescue')

    def test_generic_usb_video_menu_keeps_rescue(self):
        m = menu()
        g.validate_menu(m)
        u.validate_menu(m)
        for bad in (m.replace(g.RESCUE.encode(), g.NORMAL.encode()),
                    m.replace(g.NORMAL.encode(), g.RESCUE.encode()),
                    m.replace(b'nohibernate', b''),
                    m+b'linux /unparsed\n'):
            with self.assertRaises(ValueError):
                g.validate_menu(bad)

    def test_legacy_deployment_is_unchanged(self):
        with mock.patch.object(deploy.subprocess, 'check_output', side_effect=AssertionError('no reads')):
            deploy.verify_gpu_profile({}, {}, Path('legacy.img'))

    def test_gates_precede_image_build_and_device_selection(self):
        # Current boot is preserved as a checked 6.18 bundle, not rebuilt through old overlays.
        for name in ['build.py', 'build-surface-pro-x.py', 'build-source-boot.py', 'build-source-disk.py', 'build-sq2-default.py']:
            text = (R/"scripts"/name).read_text()
            self.assertIn("build-lts.py", text)
            self.assertNotIn("6.3.8", text)

    @unittest.skipUnless(BASE.exists() and RESCUE.exists(), 'requires local pinned DTB inputs')
    def test_exact_boot_tested_bytes_and_preserved_baseline(self):
        raw = BASE.read_bytes()
        result = g.build(raw)
        self.assertEqual(g.sha(result), g.NORMAL_SHA)
        self.assertEqual(g.build(raw), result)
        self.assertEqual(BASE.read_bytes(), raw)
        policy = g.verify(result, RESCUE.read_bytes())
        self.assertEqual((policy['maximum_hz'], policy['opp_level']), (661000000, 416))
        self.assertEqual(policy['rescue_maximum_hz'], 514000000)
        experimental = g.build_700(raw)
        self.assertEqual(g.sha(experimental), g.EXPERIMENTAL_SHA)
        self.assertEqual(g.verify(experimental, RESCUE.read_bytes())['maximum_hz'], 700000000)
        for normal, rescue in ((result, result), (experimental, experimental),
                               (result, experimental)):
            with self.assertRaises(ValueError):
                g.verify(normal, rescue)

    @unittest.skipUnless(BASE.exists() and RESCUE.exists(), 'requires local pinned DTB inputs')
    def test_actual_packed_payload_and_manifest_tampering(self):
        normal, rescue, m = g.build(BASE.read_bytes()), RESCUE.read_bytes(), menu()
        policy = g.policy(m, normal, rescue)
        manifest = dict(gpu_profile=policy,
                        boot_payload_sha256={g.NORMAL:g.sha(normal), g.RESCUE:g.sha(rescue)})
        layout = {'partitions':[{'name':'SQ2PROBE','offset':1048576}]}
        payload = {'grub.cfg':m, g.NORMAL:normal, g.RESCUE:rescue}
        def packed(cmd):
            self.assertEqual(cmd[:2], ['mtype', '-i'])
            return payload[Path(cmd[-1]).name]
        with mock.patch.object(deploy.subprocess, 'check_output', side_effect=packed):
            deploy.verify_gpu_profile(manifest, layout, Path('fixture.img'))
            for field in ('maximum_hz', 'opp_level', 'normal_sha256', 'menu_sha256'):
                bad = copy.deepcopy(manifest)
                bad['gpu_profile'][field] = 0
                with self.assertRaises(ValueError):
                    deploy.verify_gpu_profile(bad, layout, Path('fixture.img'))
            bad = copy.deepcopy(manifest)
            bad['boot_payload_sha256'][g.NORMAL] = '0'*64
            with self.assertRaises(ValueError):
                deploy.verify_gpu_profile(bad, layout, Path('fixture.img'))
            payload[g.NORMAL] = normal[:-1]+bytes([normal[-1]^1])
            with self.assertRaises(ValueError):
                deploy.verify_gpu_profile(manifest, layout, Path('fixture.img'))


if __name__ == '__main__':
    unittest.main()
