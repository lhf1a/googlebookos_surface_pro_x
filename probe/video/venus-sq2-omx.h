/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_OMX_H
#define VENUS_SQ2_OMX_H
/* The standard resolution bit stays first. Additional words carry actual HFI
 * sequence fields and their presence mask; old V4L2 clients ignore them. */
static void sq2_omx_sequence(struct v4l2_event *event,
                            const struct hfi_event_data *data)
{
 u32 payload[] = { V4L2_EVENT_SRC_CH_RESOLUTION, 0x51324d31,
                  data->sq2_sequence_fields, data->height, data->width,
                  data->bit_depth, data->pic_struct, data->colour_space };
 memcpy(event->u.data, payload, sizeof(payload));
}

/* Private downstream FLUSH. Completion means an acknowledged HFI flush,
 * then release of any still-software-owned buffers, never a guessed event. */
static int sq2_omx_flush(struct venus_inst *inst, u32 flags)
{
 struct vb2_v4l2_buffer *buffer;
 u32 type;
 int ret;
 if (!flags || (flags & ~3U)) return -EINVAL;
 if (!IS_SQ2_HFI(inst->core)) return -EOPNOTSUPP;
 mutex_lock(&inst->lock);
 if (inst->state != INST_START || inst->session_error ||
     test_bit(0, &inst->core->sys_error)) {
  ret = -EIO;
  goto out;
 }
 type = flags == 3 ? HFI_FLUSH_ALL :
        flags == 1 ? HFI_FLUSH_INPUT : HFI_FLUSH_OUTPUT;
 ret = hfi_session_flush(inst, type, true);
 if (ret) goto out;
 /* The real firmware has returned ownership. Only buffers still on the
  * software queue remain here; completed buffers were removed by callbacks. */
 if (flags & 1)
  while ((buffer = v4l2_m2m_src_buf_remove(inst->m2m_ctx))) {
   vb2_set_plane_payload(&buffer->vb2_buf, 0, 0);
   buffer->flags = 0;
   v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_DONE);
  }
 if (flags & 2) {
  /* A completed EOS latch otherwise hides real buffers returned by FLUSH. */
  vb2_clear_last_buffer_dequeued(v4l2_m2m_get_dst_vq(inst->m2m_ctx));
  while ((buffer = v4l2_m2m_dst_buf_remove(inst->m2m_ctx))) {
   vb2_set_plane_payload(&buffer->vb2_buf, 0, 0);
   buffer->flags = 0;
   v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_DONE);
  }
 }
out:
 mutex_unlock(&inst->lock);
 return ret;
}
#endif
