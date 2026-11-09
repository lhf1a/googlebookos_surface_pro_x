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

/* Called with the state lock held by the sequence callback. A blocking OMX
 * flush must let that callback run without issuing a second shared completion. */
static int sq2_omx_flush_output(struct venus_inst *inst)
{
 if (inst->sq2_omx_flush_flags) {
  if (!(inst->sq2_omx_flush_flags & 2))
   inst->sq2_omx_flush_output_pending = true;
  return 0;
 }
 return hfi_session_flush(inst, HFI_FLUSH_OUTPUT, false);
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
 /* Queue ioctls stay serialized while the IRQ callback takes the state lock. */
 mutex_lock(&inst->ctx_q_lock);
 mutex_lock(&inst->lock);
 if (inst->state != INST_START || inst->session_error ||
     test_bit(0, &inst->core->sys_error)) {
  ret = -EIO;
  goto out;
 }
 type = flags == 3 ? HFI_FLUSH_ALL :
        flags == 1 ? HFI_FLUSH_INPUT : HFI_FLUSH_OUTPUT;
 inst->sq2_omx_flush_flags = flags;
 inst->sq2_omx_flush_output_pending = false;
 mutex_unlock(&inst->lock);
 ret = hfi_session_flush(inst, type, true);
 mutex_lock(&inst->lock);
 if (!ret && inst->sq2_omx_flush_output_pending) {
  /* An input-only flush cannot acknowledge a DRC output flush. */
  inst->sq2_omx_flush_flags |= 2;
  mutex_unlock(&inst->lock);
  ret = hfi_session_flush(inst, HFI_FLUSH_OUTPUT, true);
  mutex_lock(&inst->lock);
 }
 inst->sq2_omx_flush_flags = 0;
 inst->sq2_omx_flush_output_pending = false;
 if (ret) goto out;
 if (inst->session_error || test_bit(0, &inst->core->sys_error)) {
  ret = -EIO;
  goto out;
 }
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
 mutex_unlock(&inst->ctx_q_lock);
 return ret;
}
#endif
