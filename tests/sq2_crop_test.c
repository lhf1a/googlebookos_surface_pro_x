#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <linux/videodev2.h>
typedef uint32_t u32;
struct venus_core {bool sq2;};
struct venus_inst {struct venus_core *core; bool reconfig; u32 width,height; struct v4l2_rect crop;};
#define IS_SQ2_HFI(c) ((c)->sq2)
#include "venus-sq2-crop.h"
int main(void)
{
 struct venus_core c={true};
 struct venus_inst i={&c,true,320,256,{0,0,320,240}};
 unsigned type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
 assert(venus_sq2_keep_crop(&i,type,320,256));
 i.crop=(struct v4l2_rect){8,4,312,240};
 assert(venus_sq2_keep_crop(&i,type,320,256));
 assert(!venus_sq2_keep_crop(&i,type,640,256));
 assert(!venus_sq2_keep_crop(&i,type,320,512));
 assert(!venus_sq2_keep_crop(&i,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,320,256));
 c.sq2=false;assert(!venus_sq2_keep_crop(&i,type,320,256));c.sq2=true;
 i.reconfig=false;assert(!venus_sq2_keep_crop(&i,type,320,256));i.reconfig=true;
 const struct v4l2_rect invalid[]={
  {-1,0,320,240},{0,-1,320,240},{0,0,0,240},{0,0,320,0},
  {320,0,1,1},{0,256,1,1},{1,0,320,240},{0,17,320,240},
  {1,1,UINT32_MAX,UINT32_MAX}
 };
 for(unsigned n=0;n<sizeof(invalid)/sizeof(invalid[0]);n++){
  i.crop=invalid[n];assert(!venus_sq2_keep_crop(&i,type,320,256));
 }
 puts("SQ2_CROP_PRESERVATION_PASS");
}
