// Read-only enumeration of the resident decoder. No stream or format changes.
#include <linux/videodev2.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
int main(void){
 int fd=open("/dev/video0",O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd<0){perror("open");return 1;}
 struct v4l2_capability cap={0};
 if(ioctl(fd,VIDIOC_QUERYCAP,&cap)||strcmp((char*)cap.driver,"qcom-venus")){close(fd);return 2;}
 for(unsigned n=0;n<64;n++){
 struct v4l2_fmtdesc format={0};format.index=n;format.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
 if(ioctl(fd,VIDIOC_ENUM_FMT,&format)){if(errno==EINVAL)break;perror("ENUM_FMT");close(fd);return 3;}
 printf("FORMAT index=%u fourcc=%08x description=%s flags=%x\n",n,format.pixelformat,format.description,format.flags);
 struct v4l2_frmsizeenum f={0};f.pixel_format=format.pixelformat;
 if(ioctl(fd,VIDIOC_ENUM_FRAMESIZES,&f)){printf("SIZE_QUERY errno=%d\n",errno);continue;}
 printf("fourcc=%08x type=%u min=%ux%u max=%ux%u step=%ux%u\n",f.pixel_format,f.type,f.stepwise.min_width,f.stepwise.min_height,f.stepwise.max_width,f.stepwise.max_height,f.stepwise.step_width,f.stepwise.step_height);
 }
 const unsigned controls[]={V4L2_CID_MPEG_VIDEO_H264_PROFILE,V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
  V4L2_CID_MPEG_VIDEO_VP8_PROFILE,V4L2_CID_MPEG_VIDEO_VP9_PROFILE,V4L2_CID_MPEG_VIDEO_MPEG2_PROFILE};
 for(unsigned n=0;n<sizeof(controls)/sizeof(controls[0]);n++){
 unsigned id=controls[n];
  struct v4l2_queryctrl q={0};q.id=id;
  if(ioctl(fd,VIDIOC_QUERYCTRL,&q)){printf("PROFILE_QUERY errno=%d\n",errno);continue;}
 printf("PROFILE id=%08x min=%d max=%d default=%d\n",id,q.minimum,q.maximum,q.default_value);
  for(int i=q.minimum;i<=q.maximum;i++){struct v4l2_querymenu m={0};m.id=id;m.index=i;if(!ioctl(fd,VIDIOC_QUERYMENU,&m))printf("PROFILE value=%d name=%s\n",i,m.name);}
 }
 close(fd);return 0;
}
