"""Host-only tests for the full-Mesa universal-image option.

The first classes use the real private runtime/bundle when it is staged under
ignored private/build-inputs/ (skip otherwise). The later classes exercise the
transformation helpers with synthetic fixtures. Nothing here builds a kernel or
disk image, touches a device, or claims rendering/hardware validation.
"""
import importlib.util
import json
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import full_image as fi  # noqa: E402  (scripts/ on sys.path)
import full_image_video as fiv  # noqa: E402

PRIVATE = ROOT / 'private/build-inputs'
RUNTIME = PRIVATE / 'full-image-runtime'
BUNDLE = PRIVATE / 'kernel-6.18/EFI/SQ2Nat'
SOURCE_ARCHIVE = PRIVATE / 'kernel-6.18/source/source.tar.gz'
PATCH_DIR = ROOT / 'patches/drm-618-a6xx-upstream'
PATCH_CONFIG = ROOT / 'configs/drm-618-a6xx-upstream-20261111.json'

OPTIONS = ('console=tty0 earlycon=efifb loglevel=6 acpi=off efi=novamap clk_ignore_unused '
           'pd_ignore_unused suspend.screen_only=0 suspend.screen_only_autosuspend=0 nohibernate '
           'panic=0 sq2.native=ssd sq2.storage=nvme sq2.el2=1 sq2.audio=1 sq2.video.c2=experimental '
           'sq2.video.autostart=1 sq2.vulkan=qcom sq2.display=2.7-60 sq2.usb=experimental '
           'nvme_core.default_ps_max_latency_us=0 lsm=selinux enforcing=0 printk.devkmsg=on '
           'initrd=\\EFI\\SQ2Nat\\initramfs.cpio.gz')
PROPS = ('ro.hardware.egl=freedreno\nro.hardware.vulkan=freedreno\n'
         'ro.vendor.hwcomposer.mode=client\nro.vendor.hwc.drop_drm_master=0\n')
STAGE_LINES = (b'qualcomm-full-start > /logs/qualcomm-full.txt 2>&1 || fail qualcomm-full-graphics\n'
               b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n')
INIT = (b'#!/bin/busybox sh\n'
        b'if [ -d /etc/googlebook-security ]; then\n'
        b'    apply-security-overlay > /logs/security-overlay.txt 2>&1 || fail security-overlay\n'
        b'fi\n' + STAGE_LINES + b'quest2-hidl-stage > /logs/hidl-stage.txt 2>&1 || fail hidl-stage\n')


def _synthetic_aarch64_elf(extra=b''):
    head = bytearray(64)
    head[:6] = b'\x7fELF\x02\x01'
    head[18:20] = (183).to_bytes(2, 'little')
    return bytes(head) + extra


def _synthetic_omx_service():
    buf = bytearray(_synthetic_aarch64_elf())
    end = fiv.OMX_SWRENDER_OFFSET + len(fiv.OMX_SWRENDER_BEFORE)
    buf.extend(b'\0' * (end - len(buf)))
    buf[fiv.OMX_INDEX_CHECK_OFFSET:fiv.OMX_INDEX_CHECK_OFFSET + len(fiv.OMX_INDEX_CHECK_BYTES)] = (
        fiv.OMX_INDEX_CHECK_BYTES)
    buf[fiv.OMX_SWRENDER_OFFSET:end] = fiv.OMX_SWRENDER_BEFORE
    return bytes(buf)


def fixture_runtime():
    records, bodies = {}, {}
    def put(name, body, mode=0o100644):
        records[name] = fi.new_record(mode)
        bodies[name] = body
    put('init', INIT, 0o100755)
    put('etc/googlebook-security/build.prop', PROPS.encode())
    for name in ('etc/googlebook-security/lib64/egl/libEGL_freedreno.so',
                 'etc/googlebook-security/lib64/hw/vulkan.freedreno.so',
                 'etc/googlebook-security/lib64/hw/mapper.minigbm.so'):
        put(name, b'fixture')
    omx_service = _synthetic_omx_service()
    put(fiv.OMX_SERVICE, omx_service, 0o100755)
    put(fiv.OMX_SUMS, (fiv.sha(omx_service) + '  quest2-omx-service\n').encode())
    for directory in fi.CLOSED_GRAPHICS_DIRS:
        put(directory, b'', 0o040755)
        put(directory + '/payload.so', b'closed-fixture')
    put(fiv.OMX_GRALLOC_UTILS_SOURCE, _synthetic_aarch64_elf(fiv.GRALLOC_LAYOUT_SYMBOL))
    for name in fi.CLOSED_GRAPHICS_HELPERS:
        put(name, b'#!/bin/sh\n', 0o100755)
    return records, bodies


class MesaRuntimeFixture(unittest.TestCase):
    def test_transform_removes_closed_stack_and_keeps_mesa(self):
        records, bodies = fixture_runtime()
        report = fi.mesa_runtime(records, bodies)
        self.assertEqual(report['graphics'], 'mesa')
        self.assertEqual(report['removed_count'], 7 + len(fi.CLOSED_GRAPHICS_HELPERS))
        self.assertEqual(report['video']['omx_output'], 'bytebuffer-software-renderer')
        self.assertIn(fiv.OMX_GRALLOC_UTILS, bodies)
        self.assertIn(fiv.OMX_GRALLOC_UTILS, records)
        self.assertFalse(report['hardware_validated'])
        for directory in fi.CLOSED_GRAPHICS_DIRS:
            self.assertNotIn(directory, bodies)
            self.assertNotIn(directory, records)
            self.assertFalse(any(n.startswith(directory + '/') for n in bodies))
        for name in fi.CLOSED_GRAPHICS_HELPERS:
            self.assertNotIn(name, bodies)
        self.assertEqual(bodies['etc/sq2-graphics-mode'], b'full-mesa\n')
        self.assertIn('etc/sq2-graphics-mode', records)
        for line in fi.MESA_STAGE_LINES:
            self.assertNotIn(line, bodies['init'])
        self.assertIn(fi.MESA_STATUS_HOOK, bodies['init'])
        self.assertIn(b'quest2-hidl-stage', bodies['init'])
        self.assertIn('etc/googlebook-security/lib64/egl/libEGL_freedreno.so', bodies)
        with self.assertRaisesRegex(ValueError, 'closed-graphics stage'):
            fi.mesa_runtime(records, bodies)

    def test_transform_rejects_unknown_or_incomplete_inputs(self):
        for mutate, message in (
                (lambda b, r: b.pop('etc/googlebook-security/build.prop'), 'Mesa overlay missing'),
                (lambda b, r: b.__setitem__('etc/googlebook-security/build.prop',
                                            b'ro.hardware.egl=freedreno\n'), 'property missing'),
                (lambda b, r: b.pop('etc/googlebook-security/lib64/hw/vulkan.freedreno.so'),
                 'Mesa Vulkan payload missing'),
                (lambda b, r: b.pop('etc/sq2-qti-system'), 'Closed graphics payload missing'),
                (lambda b, r: b.pop('bin/qualcomm-full-start'), 'Closed graphics helper missing'),
                (lambda b, r: b.__setitem__('init', INIT.replace(STAGE_LINES, b'')),
                 'closed-graphics stage')):
            records, bodies = fixture_runtime()
            mutate(bodies, records)
            with self.assertRaisesRegex(ValueError, message):
                fi.mesa_runtime(records, bodies)


class MesaBootSelectionFixture(unittest.TestCase):
    def _bundle(self, root, options=OPTIONS, launcher=None):
        bundle = root / 'EFI/SQ2Nat'
        bundle.mkdir(parents=True)
        (bundle / 'kernel-options.txt').write_text(options + '\n')
        if launcher is None:
            launcher = (b'PE-fixture-prefix\0' + options.strip().encode('utf-16le') + b'\0\0' +
                        b'PE-fixture-suffix\0')
        (bundle / 'el2launch.efi').write_bytes(launcher)
        return bundle

    def test_equal_length_token_rewrite(self):
        with tempfile.TemporaryDirectory() as td:
            bundle = self._bundle(pathlib.Path(td))
            before = (bundle / 'el2launch.efi').read_bytes()
            self.assertEqual(fi.rewrite_mesa_boot_selection(bundle), 'sq2.vulkan=mesa')
            after = (bundle / 'el2launch.efi').read_bytes()
            self.assertEqual(len(before), len(after))
            self.assertIn('sq2.vulkan=mesa', (bundle / 'kernel-options.txt').read_text())
            self.assertEqual(after.count(b'sq2.vulkan=mesa'.decode().encode('utf-16le')), 1)
            self.assertNotIn(b'sq2.vulkan=qcom'.decode().encode('utf-16le'), after)

    def test_rejects_missing_duplicate_or_mismatched_launcher(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            with self.assertRaisesRegex(ValueError, 'Unexpected baseline graphics option'):
                fi.rewrite_mesa_boot_selection(self._bundle(root / 'a', options=OPTIONS.replace(
                    'sq2.vulkan=qcom', 'sq2.vulkan=mesa')))
            with self.assertRaisesRegex(ValueError, 'Unexpected baseline graphics option'):
                fi.rewrite_mesa_boot_selection(self._bundle(root / 'b', options=OPTIONS +
                                                            ' sq2.vulkan=mesa'))
            stale = (b'PE\0' + OPTIONS.replace('sq2.vulkan=qcom', 'sq2.vulkan=qualcomm')
                     .strip().encode('utf-16le') + b'\0\0')
            with self.assertRaisesRegex(ValueError, 'EFI graphics constant|does not match'):
                fi.rewrite_mesa_boot_selection(self._bundle(root / 'c', launcher=stale))


class KernelPatchApplication(unittest.TestCase):
    @unittest.skipUnless(SOURCE_ARCHIVE.is_file() and PATCH_DIR.is_dir(),
                         'private pinned source archive or patch series not staged')
    def test_series_applies_to_pinned_source_and_matches_config(self):
        config = json.loads(PATCH_CONFIG.read_text(encoding='utf-8'))
        expected = {}
        for entry in config['patches']:
            for name, hashes in entry['files'].items():
                expected[name.removeprefix('kernel/')] = hashes['after']
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            kernel = root / 'kernel'
            names = {name.removeprefix('kernel/') for entry in config['patches']
                     for name in entry['files']}
            with tarfile.open(SOURCE_ARCHIVE) as archive:
                members = {m.name: m for m in archive.getmembers() if m.isfile()}
                for name in names:
                    body = archive.extractfile(members['kernel/' + name]).read()
                    path = kernel / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(body)

            def run(command, cwd=None):
                result = subprocess.run(list(map(str, command)), cwd=cwd,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                return result
            report = fi.apply_kernel_patches(PATCH_DIR, kernel, root, run)
            self.assertEqual(report['patch_count'], len(config['patches']))
            self.assertEqual(report['hardware_validated'], False)
            self.assertEqual(report['external_msm_synced'], False)
            for name, want in expected.items():
                got = __import__('hashlib').sha256((kernel / name).read_bytes()).hexdigest()
                self.assertEqual(got, want, name)
            manifest = json.loads((root / 'KERNEL-PATCHES.json').read_text())
            self.assertEqual(len(manifest['patches']), len(config['patches']))

    @unittest.skipUnless(SOURCE_ARCHIVE.is_file() and PATCH_DIR.is_dir(),
                         'private pinned source archive or patch series not staged')
    def test_series_syncs_external_edp618_msm_tree(self):
        config = json.loads(PATCH_CONFIG.read_text(encoding='utf-8'))
        expected = {}
        for entry in config['patches']:
            for name, hashes in entry['files'].items():
                expected[name.removeprefix('kernel/')] = hashes['after']
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            kernel = root / 'kernel'
            external = root / 'external'
            edp = external / 'edp-618'
            names = {name.removeprefix('kernel/') for entry in config['patches']
                     for name in entry['files']}
            with tarfile.open(SOURCE_ARCHIVE) as archive:
                members = {m.name: m for m in archive.getmembers() if m.isfile()}
                for name in names:
                    for prefix, tree in (('kernel/', kernel), ('external/edp-618/', edp)):
                        body = archive.extractfile(members[prefix + name]).read()
                        path = tree / name
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(body)

            def run(command, cwd=None):
                result = subprocess.run(list(map(str, command)), cwd=cwd,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                return result
            report = fi.apply_kernel_patches(PATCH_DIR, kernel, root, run, external_source=external)
            self.assertTrue(report['external_msm_synced'])
            for name, want in expected.items():
                got_kern = __import__('hashlib').sha256((kernel / name).read_bytes()).hexdigest()
                got_ext = __import__('hashlib').sha256((edp / name).read_bytes()).hexdigest()
                self.assertEqual(got_kern, want, name)
                self.assertEqual(got_ext, want, 'external/edp-618/' + name)
            manifest = json.loads((root / 'KERNEL-PATCHES.json').read_text())
            self.assertTrue(all('external_files' in p for p in manifest['patches']))

    @unittest.skipUnless(SOURCE_ARCHIVE.is_file() and PATCH_DIR.is_dir()
                         and fi.DEFAULT_A6XX_MMIO_FIX_PATCHES.is_dir(),
                         'private pinned source archive or patch series not staged')
    def test_series_plus_gmu_mmio_fix_applies_to_kernel_and_edp618(self):
        config = json.loads(PATCH_CONFIG.read_text(encoding='utf-8'))
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            kernel = root / 'kernel'
            external = root / 'external'
            edp = external / 'edp-618'
            names = {name.removeprefix('kernel/') for entry in config['patches']
                     for name in entry['files']}
            with tarfile.open(SOURCE_ARCHIVE) as archive:
                members = {m.name: m for m in archive.getmembers() if m.isfile()}
                for name in names:
                    for prefix, tree in (('kernel/', kernel), ('external/edp-618/', edp)):
                        body = archive.extractfile(members[prefix + name]).read()
                        path = tree / name
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(body)

            def run(command, cwd=None):
                result = subprocess.run(list(map(str, command)), cwd=cwd,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                return result
            report = fi.apply_kernel_patches(
                PATCH_DIR, kernel, root, run, external_source=external,
                extra_patch_dirs=(fi.DEFAULT_A6XX_MMIO_FIX_PATCHES,))
            self.assertEqual(report['patch_count'], len(config['patches']) + 1)
            self.assertEqual(report['extra_directories'], [str(fi.DEFAULT_A6XX_MMIO_FIX_PATCHES)])
            gpu_c_kern = (kernel / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.c').read_text()
            gpu_c_ext = (edp / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.c').read_text()
            self.assertEqual(gpu_c_kern, gpu_c_ext)
            self.assertIn('if (gmu->mmio && (reg->offset << 2) >= gmu->mmio_offset)', gpu_c_kern)
            self.assertIn('gmu_write(gmu, reg->offset, state ? reg->value : 0);', gpu_c_kern)

    def test_mixed_patch_and_unsynced_override_guard(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            kernel = root / 'kernel'
            external = root / 'external'
            edp = external / 'edp-618'
            (kernel / 'include/uapi/drm').mkdir(parents=True)
            (kernel / 'drivers/gpu/drm/msm/adreno').mkdir(parents=True)
            (edp / 'drivers/gpu/drm/msm/adreno').mkdir(parents=True)
            (kernel / 'include/uapi/drm/msm_drm.h').write_text('int header = 1;\n')
            for tree in (kernel, edp):
                (tree / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.c').write_text('int gpu = 1;\n')
            patches = root / 'patches'
            patches.mkdir()
            (patches / '0001-mixed.patch').write_text(
                'From: test\nSubject: mixed\n\n'
                'diff --git a/include/uapi/drm/msm_drm.h b/include/uapi/drm/msm_drm.h\n'
                '--- a/include/uapi/drm/msm_drm.h\n'
                '+++ b/include/uapi/drm/msm_drm.h\n'
                '@@ -1 +1 @@\n'
                '-int header = 1;\n'
                '+int header = 2;\n'
                'diff --git a/drivers/gpu/drm/msm/adreno/a6xx_gpu.c b/drivers/gpu/drm/msm/adreno/a6xx_gpu.c\n'
                '--- a/drivers/gpu/drm/msm/adreno/a6xx_gpu.c\n'
                '+++ b/drivers/gpu/drm/msm/adreno/a6xx_gpu.c\n'
                '@@ -1 +1 @@\n'
                '-int gpu = 1;\n'
                '+int gpu = 2;\n'
            )

            def run(command, cwd=None):
                result = subprocess.run(list(map(str, command)), cwd=cwd,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                return result
            out = root / 'out'
            out.mkdir()
            report = fi.apply_kernel_patches(patches, kernel, out, run, external_source=external)
            self.assertEqual((kernel / 'include/uapi/drm/msm_drm.h').read_text(), 'int header = 2;\n')
            self.assertEqual((kernel / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.c').read_text(), 'int gpu = 2;\n')
            self.assertEqual((edp / 'drivers/gpu/drm/msm/adreno/a6xx_gpu.c').read_text(), 'int gpu = 2;\n')
            self.assertEqual(set(report['patches'][0]['external_files']),
                             {'drivers/gpu/drm/msm/adreno/a6xx_gpu.c'})

            bad_patches = root / 'bad-patches'
            bad_patches.mkdir()
            (bad_patches / '0001-venus.patch').write_text(
                'diff --git a/drivers/media/platform/qcom/venus/core.c b/drivers/media/platform/qcom/venus/core.c\n'
                '--- a/drivers/media/platform/qcom/venus/core.c\n'
                '+++ b/drivers/media/platform/qcom/venus/core.c\n'
                '@@ -1 +1 @@\n-old\n+new\n'
            )
            with self.assertRaisesRegex(ValueError, 'unsynced external module override'):
                fi.apply_kernel_patches(bad_patches, kernel, out, run, external_source=external)


@unittest.skipUnless(RUNTIME.is_dir(), 'private full-image runtime not staged')
class RealRuntimeMesaTransform(unittest.TestCase):
    def test_real_runtime_switches_to_mesa_chain(self):
        records = json.loads((RUNTIME / 'records.json').read_text())
        bodies = {name: b'' if rec[1][1] & 0o170000 == 0o040000 else (RUNTIME / 'files' / name).read_bytes()
                  for name, rec in records.items()}
        report = fi.mesa_runtime(records, bodies)
        self.assertGreater(report['removed_count'], 100)
        self.assertGreater(report['retained_mesa_members'], 50)
        self.assertIn('etc/sq2-hidl', bodies)
        self.assertIn('etc/sq2-hidl/lib64/libgrallocutils.so', bodies)
        self.assertEqual(report['video']['omx_output'], 'bytebuffer-software-renderer')
        self.assertIn('bin/quest2-hidl-stage', bodies)
        self.assertNotIn('bin/qti-system-display-stage', bodies)
        self.assertNotIn('etc/sq2-qti-system', bodies)
        self.assertEqual(bodies['etc/sq2-graphics-mode'], b'full-mesa\n')
        for line in fi.MESA_STAGE_LINES:
            self.assertNotIn(line, bodies['init'])
        self.assertIn(fi.MESA_STATUS_HOOK, bodies['init'])
        for prop in fi.MESA_REQUIRED_PROPS:
            self.assertIn(prop.encode(), bodies['etc/googlebook-security/build.prop'])


@unittest.skipUnless(BUNDLE.is_dir(), 'private EFI bundle not staged')
class RealBundleMesaSelection(unittest.TestCase):
    def test_real_launcher_keeps_valid_pe_and_new_command(self):
        with tempfile.TemporaryDirectory() as td:
            bundle = pathlib.Path(td) / 'EFI/SQ2Nat'
            bundle.mkdir(parents=True)
            for name in ('kernel-options.txt', 'el2launch.efi', 'start.efi'):
                shutil.copyfile(BUNDLE / name, bundle / name)
            before = (bundle / 'el2launch.efi').read_bytes()
            fi.rewrite_mesa_boot_selection(bundle)
            after = (bundle / 'el2launch.efi').read_bytes()
            self.assertEqual(len(before), len(after))
            tool = fi.module('mesa_efi_check', 'prepare-sq2-618-debug-entry.py')
            tool.unsigned_arm64_pe(after)
            commands = [s for _, _, s in tool.utf16_strings(after) if s.startswith('console=')]
            self.assertEqual(commands, [(bundle / 'kernel-options.txt').read_text().strip()])
            token = tool.graphics_option(commands[0])
            self.assertEqual(token, 'sq2.vulkan=mesa')


class PrebuiltKernelWithPatches(unittest.TestCase):
    def test_patches_with_prebuilt_kernel_are_rejected_loudly(self):
        args = __import__('argparse').Namespace(
            kernel_mode='prebuilt', kernel_patches=ROOT / 'patches/drm-618-a6xx-upstream')
        with self.assertRaisesRegex(ValueError, 'requires --kernel-mode source'):
            fi.build(args, None)


class FullImageCli(unittest.TestCase):
    def test_help_and_choices_expose_full_mesa_option(self):
        help_text = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/build.py'), 'full-image', '--help'],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(help_text.returncode, 0, help_text.stderr)
        self.assertIn('--graphics', help_text.stdout)
        self.assertIn('mesa', help_text.stdout)
        self.assertIn('--kernel-patches', help_text.stdout)
        self.assertIn('--esp-only', help_text.stdout)
        bad = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/build.py'), 'full-image',
             '--output', str(ROOT / 'out/should-not-exist'), '--graphics', 'unknown'],
            capture_output=True, text=True, timeout=60)
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn('invalid choice', bad.stderr)
        self.assertFalse((ROOT / 'out/should-not-exist').exists())


if __name__ == '__main__':
    unittest.main()
