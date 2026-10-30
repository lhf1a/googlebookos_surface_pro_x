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
d = load('sq2-display-boot-payload')
v = load('sq2-video-boot-payload')
u = load('sq2-usb-boot-payload')
deploy = load('deploy-experimental-usb')
BASE = Path(os.environ.get('SQ2_GPU_BASELINE', R/'out/source-boot'/g.NORMAL))
RESCUE = Path(os.environ.get('SQ2_GPU_RESCUE', R/'out/source-boot'/g.RESCUE))


def menu(profile=d.DEFAULT):
    return d.default_menu(u.default_menu(v.default_menu((R/'configs/grub-source.cfg').read_text())), profile).encode()


class DefaultGpu(unittest.TestCase):
    def test_unknown_baseline_and_payloads_rejected(self):
        with self.assertRaises(ValueError):
            g.build(b'unknown SQ1 or modified DTB')
        with self.assertRaises(ValueError):
            g.verify(b'normal', b'rescue')

    def test_menu_both_display_profiles_keep_rescue(self):
        for profile in d.PROFILES:
            m = menu(profile)
            g.validate_menu(m)
            d.validate_menu(m, profile)
            u.validate_menu(m)
            self.assertNotIn(b'SQ1/SQ2', m)
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
        boot = (R/'scripts/build-source-boot.py').read_text()
        self.assertLess(boot.index('gpu.build('), boot.index("output.open('xb')"))
        self.assertIn('gpu_profile=gpu_profile', boot)
        self.assertIn('(gpu.NORMAL,gpu_normal),(gpu.RESCUE,gpu_rescue)', boot)
        disk = (R/'scripts/build-source-disk.py').read_text()
        self.assertLess(disk.index('gpu.verify('), disk.index('b.build()'))
        self.assertLess(disk.index('gpu.policy('), disk.index('b.build()'))
        cli = (R/'scripts/deploy-experimental-usb.py').read_text()
        self.assertLess(cli.index('verify_gpu_profile(m, layout'), cli.index('disks = [d for d in inventory()'))

    @unittest.skipUnless(BASE.exists() and RESCUE.exists(), 'requires local pinned DTB inputs')
    def test_exact_boot_tested_bytes_and_preserved_baseline(self):
        raw = BASE.read_bytes()
        result = g.build(raw)
        self.assertEqual(g.sha(result), g.NORMAL_SHA)
        self.assertEqual(g.build(raw), result)
        self.assertEqual(BASE.read_bytes(), raw)
        policy = g.verify(result, RESCUE.read_bytes())
        self.assertEqual((policy['maximum_hz'], policy['opp_level']), (700000000, 432))
        self.assertEqual(policy['rescue_maximum_hz'], 514000000)
        for normal, rescue in ((raw, RESCUE.read_bytes()), (result, result)):
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
