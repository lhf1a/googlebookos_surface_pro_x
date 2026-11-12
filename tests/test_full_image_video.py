"""Host tests for full-image Venus source hooks and Mesa OMX transformation."""
import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import full_image_video as v  # noqa: E402

RUNTIME = ROOT / 'private/build-inputs/full-image-runtime/files'


def record(mode):
    return ['070701', [0, mode, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0], '']


def elf64_aarch64(payload=b''):
    head = bytearray(64)
    head[:6] = b'\x7fELF\x02\x01'
    head[18:20] = (183).to_bytes(2, 'little')
    return bytes(head) + payload


class SoftwareRendererPatch(unittest.TestCase):
    def test_rejects_unknown_service_and_site(self):
        with self.assertRaisesRegex(ValueError, 'AArch64 ELF64'):
            v.software_renderer_service(b'not an elf')
        with self.assertRaisesRegex(ValueError, 'index anchor mismatch'):
            v.software_renderer_service(elf64_aarch64(b'\0' * 0x50000))

    @unittest.skipUnless((RUNTIME / v.OMX_SERVICE).is_file(), 'private runtime not staged')
    def test_patch_changes_only_the_audited_site(self):
        service = (RUNTIME / v.OMX_SERVICE).read_bytes()
        patched = v.software_renderer_service(service)
        self.assertEqual(len(patched), len(service))
        diff = [i for i, (a, b) in enumerate(zip(service, patched)) if a != b]
        self.assertTrue(diff)
        self.assertGreaterEqual(min(diff), v.OMX_SWRENDER_OFFSET)
        self.assertLess(max(diff), v.OMX_SWRENDER_OFFSET + len(v.OMX_SWRENDER_AFTER))
        # The bl to hidl_vec() keeps its exact address and encoding.
        self.assertEqual(patched[v.OMX_SWRENDER_OFFSET + 12:v.OMX_SWRENDER_OFFSET + 16],
                         service[v.OMX_SWRENDER_OFFSET + 12:v.OMX_SWRENDER_OFFSET + 16])
        with self.assertRaisesRegex(ValueError, 'Unexpected OMX RequiresSwRenderer patch site'):
            v.software_renderer_service(patched)


class MesaOmx(unittest.TestCase):
    @unittest.skipUnless((RUNTIME / v.OMX_SERVICE).is_file(), 'private runtime not staged')
    def test_transform_updates_payload_and_manifest(self):
        bodies = {name: (RUNTIME / name).read_bytes()
                  for name in (v.OMX_SERVICE, v.OMX_SUMS, v.OMX_GRALLOC_UTILS_SOURCE)}
        records = {}
        report = v.mesa_omx(records, bodies, record)
        self.assertEqual(report['omx_output'], 'bytebuffer-software-renderer')
        self.assertIn(v.OMX_GRALLOC_UTILS, records)
        sums = bodies[v.OMX_SUMS].decode()
        self.assertIn(hashlib.sha256(bodies[v.OMX_SERVICE]).hexdigest() + '  quest2-omx-service', sums)
        self.assertIn(hashlib.sha256(bodies[v.OMX_GRALLOC_UTILS]).hexdigest() +
                      '  lib64/libgrallocutils.so', sums)

    def test_requires_valid_gralloc_utility_library(self):
        bodies = {v.OMX_SERVICE: elf64_aarch64(), v.OMX_SUMS: b'',
                  v.OMX_GRALLOC_UTILS_SOURCE: elf64_aarch64(b'missing_symbol')}
        with self.assertRaisesRegex(ValueError, 'GetBufferLayout'):
            v.mesa_omx({}, bodies, record)


class VenusSourceHooks(unittest.TestCase):
    def test_restores_publish_and_capability_hooks_idempotently(self):
        with tempfile.TemporaryDirectory() as td:
            d = pathlib.Path(td)
            (d / 'venus-sq2-core.h').write_text(
                'static int f(void) {\n if (ret) goto fail;\n return 0;\nfail:\n return ret;\n}\n')
            (d / 'venus-sq2-system-pm.h').write_text(
                'static struct venus_core *sq2_pm_status_core;\n'
                'static const char *sq2_pm_stage = "probe";\n')
            (d / 'venus-sq2-avc-export.h').write_text('/* caps */\n')
            first = v.ensure_venus_source_hooks(d)
            self.assertEqual(first['updated_headers'],
                             ['venus-sq2-core.h', 'venus-sq2-system-pm.h'])
            self.assertIn('venus_sq2_pm_publish(core);', (d / 'venus-sq2-core.h').read_text())
            self.assertIn('#include "venus-sq2-avc-export.h"',
                          (d / 'venus-sq2-system-pm.h').read_text())
            second = v.ensure_venus_source_hooks(d)
            self.assertEqual(second['updated_headers'], [])

    def test_validate_venus_core_module(self):
        with self.assertRaisesRegex(ValueError, 'lacks SQ2 capability exports'):
            v.validate_venus_core_module(elf64_aarch64(b'incomplete'))
        v.validate_venus_core_module(elf64_aarch64(b''.join(v.VENUS_CORE_REQUIRED_STRINGS)))


if __name__ == '__main__':
    unittest.main()
