/* SPDX-License-Identifier: GPL-2.0-only */
/* Bounded read-only copies of the actual firmware decoder tables. */
static int sq2_avc_caps_get(char *buf, const struct kernel_param *kp)
{
 struct venus_core *core = READ_ONCE(sq2_pm_status_core);
 const struct hfi_plat_caps *c;
 unsigned int i;
 int n = 0;
 u32 codec = kp->arg ? *(const u32 *)kp->arg : HFI_VIDEO_CODEC_H264;
 if (!core) return -ENODATA;
 mutex_lock(&core->pm_lock);
 mutex_lock(&core->lock);
 if (core->state != CORE_INIT || test_bit(0, &core->sys_error)) { n = -EIO; goto done; }
 c = venus_caps_by_codec(core, codec, VIDC_SESSION_TYPE_DEC);
 if (!c || !c->num_pl || !c->num_caps) { n = -ENODATA; goto done; }
 #define EMIT(...) do { int k = snprintf(buf+n, PAGE_SIZE-n, __VA_ARGS__); \
  if (k < 0 || k >= PAGE_SIZE-n) { n = -EOVERFLOW; goto done; } n += k; } while (0)
 EMIT("version=1\nsessions=%u\n", core->max_sessions_supported);
 for (i=0; i<c->num_caps; i++)
  EMIT("cap=%u,%u,%u,%u\n", c->caps[i].capability_type,c->caps[i].min,c->caps[i].max,c->caps[i].step_size);
 for (i=0; i<c->num_pl; i++) EMIT("pl=%u,%u\n",c->pl[i].profile,c->pl[i].level);
 for (i=0; i<c->num_fmts; i++) EMIT("fmt=%u,%u\n",c->fmts[i].buftype,c->fmts[i].fmt);
 EMIT("end=1\n");
 #undef EMIT
done:
 mutex_unlock(&core->lock);
 mutex_unlock(&core->pm_lock);
 return n;
}
static const struct kernel_param_ops sq2_avc_caps_ops = {.get=sq2_avc_caps_get};
module_param_cb(sq2_avc_caps, &sq2_avc_caps_ops, NULL, 0444);

/* Keep the original AVC ABI while exposing each codec independently. */
#define SQ2_CAPS_PARAM(name, codec) \
 static const u32 name##_codec = codec; \
 module_param_cb(name, &sq2_avc_caps_ops, (void *)&name##_codec, 0444)
SQ2_CAPS_PARAM(sq2_hevc_caps, HFI_VIDEO_CODEC_HEVC);
SQ2_CAPS_PARAM(sq2_vp8_caps, HFI_VIDEO_CODEC_VP8);
SQ2_CAPS_PARAM(sq2_vp9_caps, HFI_VIDEO_CODEC_VP9);
SQ2_CAPS_PARAM(sq2_mpeg2_caps, HFI_VIDEO_CODEC_MPEG2);
SQ2_CAPS_PARAM(sq2_mpeg1_caps, HFI_VIDEO_CODEC_MPEG1);
#undef SQ2_CAPS_PARAM
