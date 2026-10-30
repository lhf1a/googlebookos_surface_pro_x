/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_FRAME_SIZES_H
#define VENUS_SQ2_FRAME_SIZES_H
/* ENUM_FRAMESIZES describes the requested compressed format, not the active
 * session's codec. Do not mutate inst->hfi_codec to answer a read-only query. */
static int venus_sq2_enum_sizes(struct venus_core *core,struct v4l2_frmsizeenum *f)
{
 u32 codec,i;const struct hfi_plat_caps *c;
 const struct hfi_capability *w=NULL,*h=NULL;
 switch(f->pixel_format){
 case V4L2_PIX_FMT_H264:codec=HFI_VIDEO_CODEC_H264;break;
 case V4L2_PIX_FMT_HEVC:codec=HFI_VIDEO_CODEC_HEVC;break;
 case V4L2_PIX_FMT_VP8:codec=HFI_VIDEO_CODEC_VP8;break;
 case V4L2_PIX_FMT_VP9:codec=HFI_VIDEO_CODEC_VP9;break;
 case V4L2_PIX_FMT_MPEG1:codec=HFI_VIDEO_CODEC_MPEG1;break;
 case V4L2_PIX_FMT_MPEG2:codec=HFI_VIDEO_CODEC_MPEG2;break;
 default:return -EINVAL;
 }
 if(f->index)return -EINVAL;
 c=venus_caps_by_codec(core,codec,VIDC_SESSION_TYPE_DEC);if(!c)return -EINVAL;
 for(i=0;i<c->num_caps;i++){
  if(c->caps[i].capability_type==HFI_CAPABILITY_FRAME_WIDTH)w=&c->caps[i];
  if(c->caps[i].capability_type==HFI_CAPABILITY_FRAME_HEIGHT)h=&c->caps[i];
 }
 if(!w||!h||!w->step_size||!h->step_size||w->min>w->max||h->min>h->max)return -EINVAL;
 f->type=V4L2_FRMSIZE_TYPE_STEPWISE;
 f->stepwise.min_width=w->min;f->stepwise.max_width=w->max;f->stepwise.step_width=w->step_size;
 f->stepwise.min_height=h->min;f->stepwise.max_height=h->max;f->stepwise.step_height=h->step_size;
 return 0;
}
#endif
