/* Exercise the actual publication code with host representations of Venus ABI. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <linux/videodev2.h>
typedef uint32_t u32;
#define MAX_CODEC_NUM 32
#define MAX_CAP_ENTRIES 32
#define MAX_FMT_ENTRIES 32
#define MAX_ALLOC_MODE_ENTRIES 16
#define HFI_MAX_PROFILE_COUNT 16
#define MAX_SESSIONS 16
#define HFI_BUFFER_OUTPUT 2
#define HFI_BUFFER_OUTPUT2 3
#define HFI_BUFFER_MODE_DYNAMIC 0x1000003
#define HFI_ERR_NONE 0
#define HFI_ERR_SYS_INSUFFICIENT_RESOURCES 1
struct hfi_capability {u32 capability_type,min,max,step_size;};
struct hfi_profile_level {u32 profile,level;};
struct raw_formats {u32 buftype,fmt;};
struct hfi_plat_caps {
 u32 codec,domain;bool cap_bufs_mode_dynamic;unsigned num_caps;
 struct hfi_capability caps[32];unsigned num_pl;
 struct hfi_profile_level pl[16];unsigned num_fmts;
 struct raw_formats fmts[32];bool valid;
};
struct venus_core {struct hfi_plat_caps caps[32];u32 codecs_count,dec_codecs,enc_codecs,max_sessions_supported;};
#include "venus-sq2-capabilities.h"
#define HFI_VIDEO_CODEC_H264 2
#define HFI_VIDEO_CODEC_MPEG1 8
#define HFI_VIDEO_CODEC_MPEG2 16
#define HFI_VIDEO_CODEC_VP8 0x1000
#define HFI_VIDEO_CODEC_HEVC 0x2000
#define HFI_VIDEO_CODEC_VP9 0x4000
#define VIDC_SESSION_TYPE_DEC 2
#define HFI_CAPABILITY_FRAME_WIDTH 1
#define HFI_CAPABILITY_FRAME_HEIGHT 2
static const struct hfi_plat_caps *venus_caps_by_codec(struct venus_core *c,u32 codec,u32 domain){
 for(u32 i=0;i<c->codecs_count;i++)if(c->caps[i].codec==codec&&c->caps[i].domain==domain)return &c->caps[i];
 return NULL;
}
#include "venus-sq2-frame-sizes.h"
int main(int argc,char **argv){
 assert(argc==2);u32 packet[4096];FILE *f=fopen(argv[1],"rb");assert(f);
 size_t bytes=fread(packet,1,sizeof(packet),f);assert(feof(f));fclose(f);
 struct venus_core *core=calloc(1,sizeof(*core)),*before=malloc(sizeof(*core));assert(core&&before);
 assert(!venus_sq2_parse_capabilities(core,packet+4,bytes-16,packet[3]));
 assert(core->codecs_count==10&&core->dec_codecs==0x701a);
 unsigned widths=0;
 for(u32 i=0;i<core->codecs_count;i++)if(core->caps[i].domain==2){
  struct hfi_plat_caps *c=&core->caps[i];
  for(u32 j=0;j<c->num_caps;j++)if(c->caps[j].capability_type==1){
   u32 expected=c->codec==0x10?1920:c->codec==0x1000?4096:8192;
   assert(c->caps[j].max==expected);widths++;
  }
 }
 assert(widths==5);memcpy(before,core,sizeof(*core));
 const u32 formats[]={V4L2_PIX_FMT_MPEG2,V4L2_PIX_FMT_H264,V4L2_PIX_FMT_VP8,V4L2_PIX_FMT_VP9,V4L2_PIX_FMT_HEVC};
 const u32 maxima[]={1920,8192,4096,8192,8192};
 for(unsigned i=0;i<5;i++){
  struct v4l2_frmsizeenum f={.pixel_format=formats[i]};
  assert(!venus_sq2_enum_sizes(core,&f));
  assert(f.stepwise.max_width==maxima[i]&&f.stepwise.max_height==maxima[i]);
  assert(f.stepwise.min_width==96&&f.stepwise.step_width==1);
  f.index=1;assert(venus_sq2_enum_sizes(core,&f)==-EINVAL);
 }
 struct v4l2_frmsizeenum absent={.pixel_format=V4L2_PIX_FMT_MPEG1};
 assert(venus_sq2_enum_sizes(core,&absent)==-EINVAL);
 assert(!memcmp(before,core,sizeof(*core)));
 puts("PER_FORMAT_ENUM_NO_STATE_MUTATION_MISSING_LIMITS_REFUSED");
 u32 bad[]={0x1009,2,0,0x100e,2,2,0x1007,33};
 assert(venus_sq2_parse_capabilities(core,bad,sizeof(bad),3));
 assert(!memcmp(before,core,sizeof(*core)));
 /* Two individually bounded capability arrays overflow cumulative capacity. */
 u32 overflow[6+2*70]={0x1009,2,0,0x100e,2,2};
 for(unsigned chunk=0;chunk<2;chunk++){
  unsigned at=6+chunk*70;overflow[at++]=0x1007;overflow[at++]=17;
  for(unsigned j=0;j<17;j++){overflow[at++]=j+1;overflow[at++]=1;overflow[at++]=10;overflow[at++]=1;}
 }
 assert(venus_sq2_parse_capabilities(core,overflow,sizeof(overflow),4));
 assert(!memcmp(before,core,sizeof(*core)));
 for(u32 cut=1;cut<(bytes-16)/4;cut++){
  assert(venus_sq2_parse_capabilities(core,packet+4,cut*4,packet[3]));
  assert(!memcmp(before,core,sizeof(*core)));
 }
 free(before);free(core);puts("REAL_CODEC_LIMITS_TRANSACTION_ROLLBACK_PASS");return 0;
}
