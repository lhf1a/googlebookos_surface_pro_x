#!/usr/bin/env python3
"""Wire SQ2 VB2 DMA safety and HFI close-guard lifecycle hooks into venus venc.c."""
import argparse
from pathlib import Path


def apply(directory):
    """Patch external/venus/venc.c idempotently for SQ2 hardware encoding."""
    directory = Path(directory)
    venc = directory / 'venc.c'
    text = venc.read_text()
    if 'sq2_vb2_memops' in text and 'MODULE_IMPORT_NS("DMA_BUF")' in text:
        return []

    def replace(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError('Unknown venc.c anchor: ' + repr(old[:80]))
        text = text.replace(old, new, 1)

    replace(
        '#include "pm_helpers.h"',
        '#include "pm_helpers.h"\n'
        '#include "venus-sq2-vb2.h"\n'
        '#include "venus-sq2-callback.h"\n'
        '#include "venus-sq2-close-guard.h"',
    )

    replace(
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
        '}',
        'static void venc_release_session(struct venus_inst *inst)\n'
        '{\n'
        '\tstruct venus_core *core = inst->core;\n'
        '\tint ret, power, abort = 0;\n'
        '\n'
        '\tpower = IS_SQ2_HFI(core) && test_bit(0, &core->sys_error) ?\n'
        '\t\t-EIO : venc_pm_get(inst);\n'
        '\tif (power < 0)\n'
        '\t\tvenus_sq2_quarantine_unresolved(inst, power, "enc-release-power");\n'
        '\n'
        '\tmutex_lock(&inst->lock);\n'
        '\tif (power < 0 || (IS_SQ2_HFI(core) && test_bit(0, &core->sys_error))) {\n'
        '\t\tinst->session_error = true;\n'
        '\t\tgoto retire_only;\n'
        '\t}\n'
        '\n'
        '\tret = hfi_session_deinit(inst);\n'
        '\tif (IS_SQ2_HFI(core)) {\n'
        '\t\tif (venus_sq2_quarantine_unresolved(inst, ret ? ret : -EIO, "enc-release-end"))\n'
        '\t\t\tgoto retire_only;\n'
        '\t\tif (ret)\n'
        '\t\t\tabort = 1;\n'
        '\t} else if (ret || inst->session_error) {\n'
        '\t\tabort = 1;\n'
        '\t}\n'
        '\tif (abort && !IS_SQ2_HFI(core))\n'
        '\t\thfi_session_abort(inst);\n'
        '\n'
        '\tmutex_unlock(&inst->lock);\n'
        '\n'
        '\tvenus_pm_load_scale(inst);\n'
        '\tINIT_LIST_HEAD(&inst->registeredbufs);\n'
        '\tvenus_pm_release_core(inst);\n'
        '\n'
        '\tvenc_pm_put(inst, false);\n'
        '\treturn;\n'
        '\n'
        'retire_only:\n'
        '\tmutex_unlock(&inst->lock);\n'
        '\tsq2_callback_close_begin(inst);\n'
        '\tmutex_lock(&inst->lock);\n'
        '\tvenus_helper_sq2_retire_buffers(inst);\n'
        '\tINIT_LIST_HEAD(&inst->registeredbufs);\n'
        '\tmutex_unlock(&inst->lock);\n'
        '\tif (power >= 0)\n'
        '\t\tvenc_pm_put(inst, false);\n'
        '}',
    )

    replace(
        'static const struct vb2_ops venc_vb2_ops = {\n'
        '\t.queue_setup = venc_queue_setup,\n'
        '\t.buf_init = venc_buf_init,\n'
        '\t.buf_cleanup = venc_buf_cleanup,\n'
        '\t.buf_prepare = venus_helper_vb2_buf_prepare,\n'
        '\t.start_streaming = venc_start_streaming,\n'
        '\t.stop_streaming = venus_helper_vb2_stop_streaming,\n'
        '\t.buf_queue = venc_vb2_buf_queue,\n'
        '};',
        'static void venc_stop_streaming(struct vb2_queue *q)\n'
        '{\n'
        '\tstruct venus_inst *inst = vb2_get_drv_priv(q);\n'
        '\tstruct venus_core *core = inst->core;\n'
        '\tint ret;\n'
        '\n'
        '\tif (IS_SQ2_HFI(core)) {\n'
        '\t\tret = test_bit(0, &core->sys_error) ? -EIO : venc_pm_get_put(inst);\n'
        '\t\tif (ret < 0 || test_bit(0, &core->sys_error)) {\n'
        '\t\t\tvenus_sq2_quarantine_unresolved(inst, ret < 0 ? ret : -EIO,\n'
        '\t\t\t\t\t\t\t"enc-stop-streaming-power");\n'
        '\t\t\tsq2_callback_close_begin(inst);\n'
        '\t\t\tmutex_lock(&inst->lock);\n'
        '\t\t\tinst->session_error = true;\n'
        '\t\t\tvenus_helper_sq2_retire_buffers(inst);\n'
        '\t\t\tvenus_helper_buffers_done(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,\n'
        '\t\t\t\t\t\t  VB2_BUF_STATE_ERROR);\n'
        '\t\t\tvenus_helper_buffers_done(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,\n'
        '\t\t\t\t\t\t  VB2_BUF_STATE_ERROR);\n'
        '\t\t\tif (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)\n'
        '\t\t\t\tinst->streamon_out = 0;\n'
        '\t\t\telse\n'
        '\t\t\t\tinst->streamon_cap = 0;\n'
        '\t\t\tmutex_unlock(&inst->lock);\n'
        '\t\t\treturn;\n'
        '\t\t}\n'
        '\t}\n'
        '\tvenus_helper_vb2_stop_streaming(q);\n'
        '}\n'
        '\n'
        'static const struct vb2_ops venc_vb2_ops = {\n'
        '\t.queue_setup = venc_queue_setup,\n'
        '\t.buf_init = venc_buf_init,\n'
        '\t.buf_cleanup = venc_buf_cleanup,\n'
        '\t.buf_prepare = venus_helper_vb2_buf_prepare,\n'
        '\t.start_streaming = venc_start_streaming,\n'
        '\t.stop_streaming = venc_stop_streaming,\n'
        '\t.buf_queue = venc_vb2_buf_queue,\n'
        '};',
    )

    replace(
        '\tsrc_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;\n'
        '\tsrc_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;\n'
        '\tsrc_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;\n'
        '\tsrc_vq->ops = &venc_vb2_ops;\n'
        '\tsrc_vq->mem_ops = &vb2_dma_contig_memops;',
        '\tsrc_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;\n'
        '\tsrc_vq->io_modes = IS_SQ2_HFI(inst->core) ? (VB2_MMAP | VB2_DMABUF) : (VB2_MMAP | VB2_USERPTR | VB2_DMABUF);\n'
        '\tsrc_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;\n'
        '\tsrc_vq->ops = &venc_vb2_ops;\n'
        '\tsrc_vq->mem_ops = IS_SQ2_HFI(inst->core) ? &sq2_vb2_memops : &vb2_dma_contig_memops;',
    )

    replace(
        '\tdst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;\n'
        '\tdst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;\n'
        '\tdst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;\n'
        '\tdst_vq->ops = &venc_vb2_ops;\n'
        '\tdst_vq->mem_ops = &vb2_dma_contig_memops;',
        '\tdst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;\n'
        '\tdst_vq->io_modes = IS_SQ2_HFI(inst->core) ? (VB2_MMAP | VB2_DMABUF) : (VB2_MMAP | VB2_USERPTR | VB2_DMABUF);\n'
        '\tdst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;\n'
        '\tdst_vq->ops = &venc_vb2_ops;\n'
        '\tdst_vq->mem_ops = IS_SQ2_HFI(inst->core) ? &sq2_vb2_memops : &vb2_dma_contig_memops;',
    )

    replace(
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
        '}',
        'static int venc_close(struct file *file)\n'
        '{\n'
        '\tstruct venus_inst *inst = to_inst(file);\n'
        '\tint ret, end = 0;\n'
        '\n'
        '\tsq2_callback_close_begin(inst);\n'
        '\tret = IS_SQ2_HFI(inst->core) && test_bit(0, &inst->core->sys_error) ?\n'
        '\t\t-EIO : venc_pm_get(inst);\n'
        '\tif (ret < 0)\n'
        '\t\tvenus_sq2_quarantine_unresolved(inst, ret, "enc-close-power");\n'
        '\n'
        '\tv4l2_m2m_ctx_release(inst->m2m_ctx);\n'
        '\tv4l2_m2m_release(inst->m2m_dev);\n'
        '\tif (IS_SQ2_HFI(inst->core)) {\n'
        '\t\tif (ret >= 0 && !test_bit(0, &inst->core->sys_error))\n'
        '\t\t\tend = hfi_session_deinit(inst);\n'
        '\t\telse\n'
        '\t\t\tend = ret < 0 ? ret : -EIO;\n'
        '\t\tif (venus_sq2_quarantine_unresolved(inst, end ? end : -EIO, "enc-close-end") && !end)\n'
        '\t\t\tend = -EIO;\n'
        '\t}\n'
        '\tvenc_ctrl_deinit(inst);\n'
        '\thfi_session_destroy(inst);\n'
        '\tvenus_helper_sq2_retire_buffers(inst);\n'
        '\tmutex_destroy(&inst->ctx_q_lock);\n'
        '\tmutex_destroy(&inst->lock);\n'
        '\tv4l2_fh_del(&inst->fh, file);\n'
        '\tv4l2_fh_exit(&inst->fh);\n'
        '\n'
        '\tif (ret >= 0)\n'
        '\t\tvenc_pm_put(inst, false);\n'
        '\n'
        '\tkfree(inst);\n'
        '\treturn ret < 0 ? ret : end;\n'
        '}',
    )

    replace(
        'MODULE_LICENSE("GPL v2");',
        'MODULE_LICENSE("GPL v2");\nMODULE_IMPORT_NS("DMA_BUF");',
    )

    venc.write_text(text)
    return ['venc.c']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    apply(args.directory)
