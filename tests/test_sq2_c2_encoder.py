"""Tests for SQ2 C2 hardware video encoder kernel hooks, DT overlay, C2 source, and XML."""
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def load_script(filename):
    path = ROOT / 'scripts' / filename
    spec = importlib.util.spec_from_file_location(filename.replace('-', '_').replace('.py', ''), path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Sq2C2EncoderTests(unittest.TestCase):
    def test_patch_sq2_venus_enc_on_fixture_and_idempotent(self):
        patch_mod = load_script('patch-sq2-venus-enc.py')
        fixture = (
            '#include "helpers.h"\n'
            '#include "venc.h"\n'
            '#include "pm_helpers.h"\n'
            '\n'
            'static void venc_release_session(struct venus_inst *inst)\n'
            '{\n'
            '\tint ret;\n'
            '\n'
            '\tvenc_pm_get(inst);\n'
            '\n'
            '\tmutex_lock(&inst->lock);\n'
            '\n'
            '\tret = hfi_session_deinit(inst);\n'
            '\tif (ret || inst->session_error)\n'
            '\t\thfi_session_abort(inst);\n'
            '\n'
            '\tmutex_unlock(&inst->lock);\n'
            '\n'
            '\tvenus_pm_load_scale(inst);\n'
            '\tINIT_LIST_HEAD(&inst->registeredbufs);\n'
            '\tvenus_pm_release_core(inst);\n'
            '\n'
            '\tvenc_pm_put(inst, false);\n'
            '}\n'
            '\n'
            'static const struct vb2_ops venc_vb2_ops = {\n'
            '\t.queue_setup = venc_queue_setup,\n'
            '\t.buf_init = venc_buf_init,\n'
            '\t.buf_cleanup = venc_buf_cleanup,\n'
            '\t.buf_prepare = venus_helper_vb2_buf_prepare,\n'
            '\t.start_streaming = venc_start_streaming,\n'
            '\t.stop_streaming = venus_helper_vb2_stop_streaming,\n'
            '\t.buf_queue = venc_vb2_buf_queue,\n'
            '};\n'
            '\n'
            'static int m2m_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)\n'
            '{\n'
            '\tsrc_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;\n'
            '\tsrc_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;\n'
            '\tsrc_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;\n'
            '\tsrc_vq->ops = &venc_vb2_ops;\n'
            '\tsrc_vq->mem_ops = &vb2_dma_contig_memops;\n'
            '\tdst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;\n'
            '\tdst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;\n'
            '\tdst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;\n'
            '\tdst_vq->ops = &venc_vb2_ops;\n'
            '\tdst_vq->mem_ops = &vb2_dma_contig_memops;\n'
            '\treturn 0;\n'
            '}\n'
            '\n'
            'static int venc_close(struct file *file)\n'
            '{\n'
            '\tstruct venus_inst *inst = to_inst(file);\n'
            '\n'
            '\tvenc_pm_get(inst);\n'
            '\n'
            '\tv4l2_m2m_ctx_release(inst->m2m_ctx);\n'
            '\tv4l2_m2m_release(inst->m2m_dev);\n'
            '\tvenc_ctrl_deinit(inst);\n'
            '\thfi_session_destroy(inst);\n'
            '\tmutex_destroy(&inst->ctx_q_lock);\n'
            '\tmutex_destroy(&inst->lock);\n'
            '\tv4l2_fh_del(&inst->fh, file);\n'
            '\tv4l2_fh_exit(&inst->fh);\n'
            '\n'
            '\tvenc_pm_put(inst, false);\n'
            '\n'
            '\tkfree(inst);\n'
            '\treturn 0;\n'
            '}\n'
            '\n'
            'MODULE_LICENSE("GPL v2");\n'
        )
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            (d / 'venc.c').write_text(fixture)
            changed1 = patch_mod.apply(d)
            self.assertEqual(changed1, ['venc.c'])
            out = (d / 'venc.c').read_text()
            self.assertIn('#include "venus-sq2-vb2.h"', out)
            self.assertIn('#include "venus-sq2-close-guard.h"', out)
            self.assertEqual(out.count('sq2_vb2_memops'), 2)
            self.assertIn('venus_sq2_quarantine_unresolved', out)
            self.assertIn('venus_helper_sq2_retire_buffers(inst);', out)
            self.assertIn('.stop_streaming = venc_stop_streaming,', out)
            self.assertIn('MODULE_IMPORT_NS("DMA_BUF");', out)
            # Idempotent re-application must be a no-op
            changed2 = patch_mod.apply(d)
            self.assertEqual(changed2, [])

    def test_hfi_overlay_and_prepare_sources_wired(self):
        overlay = (ROOT / 'scripts/build-sq2-hfi-overlay.py').read_text()
        self.assertIn('video-decoder { compatible = "venus-decoder"; };', overlay)
        self.assertIn('video-encoder { compatible = "venus-encoder"; };', overlay)
        prep = (ROOT / 'scripts/prepare-full-image-sources.py').read_text()
        self.assertIn("invoke('patch-sq2-venus-enc','apply',venus)", prep)

    def test_c2_encoder_sources_invariants(self):
        codecs_h = (ROOT / 'probe/video/sq2-c2-enc-codecs.h').read_text()
        self.assertIn('sq2OpenVenusEncoderNode', codecs_h)
        self.assertIn('VIDIOC_QUERYCAP', codecs_h)
        self.assertNotIn('/dev/video15', codecs_h)

        session_h = (ROOT / 'probe/video/sq2-v4l2-enc-session.h').read_text()
        self.assertIn('VIDIOC_S_SELECTION', session_h)
        self.assertIn('fillNv12FromRgba', session_h)
        self.assertIn('vld4q_u8', session_h)

        intf_h = (ROOT / 'probe/video/sq2-c2-enc-interface.h').read_text()
        self.assertIn('C2MemoryUsage::CPU_READ', intf_h)
        self.assertIn('V4L2_MPEG_VIDEO_BITRATE_MODE_CBR', intf_h)
        self.assertNotIn('HFI_RATE_CONTROL_VBR', intf_h)

        enc_h = (ROOT / 'probe/video/sq2-c2-v4l2-encoder.h').read_text()
        self.assertIn('C2StreamInitDataInfo::output::AllocUnique', enc_h)
        self.assertIn('findAnnexBCsdEnd', enc_h)

    def test_xml_wiring_preserves_omx_decoders(self):
        xml_mod = load_script('prepare-sq2-c2-enc-xml.py')
        sample = b'<?xml version="1.0" encoding="utf-8"?><MediaCodecs><Include href="media_codecs_quest2_omx.xml" /></MediaCodecs>'
        out1 = xml_mod.configure_enc_codecs(sample)
        root = ET.fromstring(out1)
        hrefs = [n.get('href') for n in root.findall('./Include')]
        self.assertEqual(hrefs, ['media_codecs_quest2_omx.xml', 'media_codecs_sq2_c2_enc.xml'])
        # Idempotent
        out2 = xml_mod.configure_enc_codecs(out1)
        root2 = ET.fromstring(out2)
        self.assertEqual([n.get('href') for n in root2.findall('./Include')],
                         ['media_codecs_quest2_omx.xml', 'media_codecs_sq2_c2_enc.xml'])


if __name__ == '__main__':
    unittest.main()
