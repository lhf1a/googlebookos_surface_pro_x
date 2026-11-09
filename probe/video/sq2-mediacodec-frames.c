// Diagnostic real bitstream transport. Dumps output for independent verification.
#include <media/NdkMediaCodec.h>
#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <android/data_space.h>
#ifdef SQ2_P010_AHB_READBACK
#include "../graphics/quest2-mapper5-video-buffer.h"
#endif
// Platform libbinder_ndk entry point: standalone clients need callback threads.
extern void ABinderProcess_startThreadPool(void);
#ifdef SQ2_IMAGE_HDR_METADATA
extern int sq2_dump_image_hdr_metadata(AHardwareBuffer *buffer);
#endif
static const char *frame_directory="/data/local/tmp";
typedef uint32_t u32;
typedef uint8_t u8;
#ifdef SQ2_HEVC_VECTOR
#include "sq2-hevc-vector.h"
#define SQ2_MIME "video/hevc"
#define SQ2_COMPONENT "OMX.qcom.video.decoder.hevc"
#define SQ2_COLOR (SQ2_VEC_BYTES_PER_SAMPLE==2?54:21)
#else
#include "sq2-h264-vector.h"
#define SQ2_MIME "video/avc"
#define SQ2_COMPONENT "OMX.qcom.video.decoder.avc"
#define SQ2_COLOR 19
#endif
#include "sq2-window-playback.h"
struct window_callbacks {
 pthread_mutex_t mutex;
 struct sq2_window_playback playback;
};
static int64_t monotonic_ns(void) {
 struct timespec time;
 if(clock_gettime(CLOCK_MONOTONIC,&time))return -1;
 return (int64_t)time.tv_sec*1000000000+time.tv_nsec;
}
static void frame_rendered(AMediaCodec *codec,void *opaque,int64_t pts,int64_t callback_ns) {
 (void)codec;struct window_callbacks *callbacks=opaque;
 int64_t arrival_ns=monotonic_ns();
 pthread_mutex_lock(&callbacks->mutex);
 sq2_window_callback(&callbacks->playback,pts,callback_ns,arrival_ns);
 pthread_mutex_unlock(&callbacks->mutex);
}
static int pace_window(int64_t target) {
 // Limit render-ahead to two 30-fps frames, without waiting on consumer callbacks.
 int64_t release=target-66666000;
 struct timespec time={.tv_sec=release/1000000000,.tv_nsec=release%1000000000};
 int result;
 do {result=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&time,NULL);}while(result==EINTR);
 return result;
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static size_t startcode(size_t at, size_t *prefix) {
 for(size_t i=at;i+3<sizeof(sq2_vec_stream);i++) {
  if(sq2_vec_stream[i]||sq2_vec_stream[i+1])continue;
  if(sq2_vec_stream[i+2]==1){*prefix=3;return i;}
  if(sq2_vec_stream[i+2]==0&&sq2_vec_stream[i+3]==1){*prefix=4;return i;}
 }
 return sizeof(sq2_vec_stream);
}
static int add_csd(AMediaFormat *f) {
#ifdef SQ2_HEVC_VECTOR
 /* Android HEVC csd-0 contains Annex-B VPS/SPS/PPS, not AVC csd-0/csd-1. */
 size_t end=sq2_vec_au[1],prefix=0,at=startcode(0,&prefix),written=0;
 unsigned mask=0;
 uint8_t *csd=malloc(end);if(!csd)return 0;
 while(at<end && prefix<end-at && end-at-prefix>=2) {
  size_t nextprefix=0,next=startcode(at+prefix+2,&nextprefix);
  if(next>end)next=end;
  unsigned type=(sq2_vec_stream[at+prefix]>>1)&63;
  if(type>=32 && type<=34) {
   memcpy(csd+written,sq2_vec_stream+at,next-at);written+=next-at;
   mask|=1U<<(type-32);
  }
  at=next;prefix=nextprefix;
 }
 if(mask==7) {
  AMediaFormat_setBuffer(f,"csd-0",csd,written);
  printf("CSD HEVC VPS/SPS/PPS bytes=%zu\n",written);
 }
 free(csd);return mask==7;
#else
 size_t prefix=0,at=startcode(0,&prefix);unsigned mask=0;
 while(at+prefix<sizeof(sq2_vec_stream)) {
  unsigned type=sq2_vec_stream[at+prefix]&31;
  size_t nextprefix=0,next=startcode(at+prefix+1,&nextprefix);
  if((type==7&&!(mask&1))||(type==8&&!(mask&2))){
   AMediaFormat_setBuffer(f,type==7?"csd-0":"csd-1",sq2_vec_stream+at,next-at);
   printf("CSD type=%u bytes=%zu\n",type,next-at);mask|=type==7?1:2;
  }
  if(mask==3)break;
  at=next;prefix=nextprefix;
 }
 return mask==3;
#endif
}
static size_t copy_au_part(uint8_t *dst,unsigned index,int configuration) {
 size_t begin=sq2_vec_au[index],end=sq2_vec_au[index+1],prefix=0;
 size_t at=startcode(begin,&prefix),written=0;
 if(at!=begin)return 0;
 while(at<end&&at+prefix<end){
  size_t nextprefix=0,next=startcode(at+prefix+1,&nextprefix);
  if(next>end)next=end;
#ifdef SQ2_HEVC_VECTOR
  if(end-at-prefix<2)return 0;
  unsigned type=(sq2_vec_stream[at+prefix]>>1)&63;
  int parameter=type>=32 && type<=34;
#else
  unsigned type=sq2_vec_stream[at+prefix]&31;
  int parameter=type==7||type==8;
#endif
  if(parameter==configuration){memcpy(dst+written,sq2_vec_stream+at,next-at);written+=next-at;}
  at=next;prefix=nextprefix;
 }
 return written;
}
static int save_rgb10_image(AImage *im,unsigned index){
 AHardwareBuffer *buffer=NULL;int32_t space=0;int64_t timestamp=0;AImageCropRect crop;
 if(AImage_getHardwareBuffer(im,&buffer)||!buffer||AImage_getDataSpace(im,&space)||
    AImage_getTimestamp(im,&timestamp)||AImage_getCropRect(im,&crop))return 0;
#ifdef SQ2_IMAGE_HDR_METADATA
 if(!sq2_dump_image_hdr_metadata(buffer))return 0;
#endif
 AHardwareBuffer_Desc desc={0};AHardwareBuffer_describe(buffer,&desc);
 printf("IMAGE_RGB10 index=%u format=%u width=%u height=%u stride=%u dataspace=0x%x timestamp_ns=%lld crop=%d,%d,%d,%d\n",
   index,desc.format,desc.width,desc.height,desc.stride,space,(long long)timestamp,
   crop.left,crop.top,crop.right,crop.bottom);
 if(desc.format!=AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM || desc.width<SQ2_VEC_W ||
    desc.height<SQ2_VEC_H || desc.stride<desc.width || crop.left || crop.top ||
    crop.right!=SQ2_VEC_W || crop.bottom!=SQ2_VEC_H)return 0;
 void *pixels=NULL;int rc=AHardwareBuffer_lock(buffer,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&pixels);
 printf("IMAGE_RGB10_LOCK=%d\n",rc);if(rc||!pixels)return 0;
 char path[256];int n=snprintf(path,sizeof(path),"%s/sq2-rgb10-frame-%u.bin",frame_directory,index);
 FILE *out=n>0&&(size_t)n<sizeof(path)?fopen(path,"wbx"):NULL;int ok=out!=NULL;
 if(out){
  for(unsigned y=0;y<SQ2_VEC_H;y++)
   if(fwrite((const uint8_t *)pixels+(size_t)y*desc.stride*4,4,SQ2_VEC_W,out)!=SQ2_VEC_W){ok=0;break;}
  if(fclose(out))ok=0;
 }
 if(AHardwareBuffer_unlock(buffer,NULL))ok=0;
 return ok;
}
static int copy_image_plane(uint8_t *dst,size_t capacity,const uint8_t *data,int len,
    int row,int pixel,unsigned w,unsigned h,unsigned sample) {
 if(!dst||!data||len<0||row<=0||pixel<=0||!w||!h||(sample!=1&&sample!=2)||
    (unsigned)pixel<sample||(uint64_t)w*h>capacity/sample)return 0;
 const uint64_t line=(uint64_t)(w-1)*pixel+sample;
 if(line>(unsigned)row||(uint64_t)(h-1)*row+line>(unsigned)len)return 0;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
  memcpy(dst+((size_t)y*w+x)*sample,data+(size_t)y*row+(size_t)x*pixel,sample);
 return 1;
}
static int save_image(AImage *im,unsigned index){
 int32_t width=0,height=0,planes=0;int64_t timestamp=0;AImageCropRect crop;
 int32_t image_format=0;
 if(AImage_getFormat(im,&image_format))return 0;
 if(image_format==AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM)return save_rgb10_image(im,index);
 if(AImage_getWidth(im,&width)||AImage_getHeight(im,&height)||AImage_getNumberOfPlanes(im,&planes)||
    AImage_getCropRect(im,&crop)||AImage_getTimestamp(im,&timestamp))return 0;
 printf("IMAGE index=%u width=%d height=%d planes=%d timestamp_ns=%lld crop=%d,%d,%d,%d\n",
        index,width,height,planes,(long long)timestamp,crop.left,crop.top,crop.right,crop.bottom);
 if(index>=SQ2_VEC_FRAMES)return 0;
#ifdef SQ2_VEC_DYNAMIC
 const unsigned expected_width=sq2_vec_width[index],expected_height=sq2_vec_height[index];
#else
 const unsigned expected_width=SQ2_VEC_W,expected_height=SQ2_VEC_H;
#endif
 if(!expected_width||!expected_height||expected_width>4096||expected_height>4096||
    (expected_width&1)||(expected_height&1)||width!=(int32_t)expected_width||
    height!=(int32_t)expected_height||planes!=3||crop.left||crop.top||
    crop.right!=width||crop.bottom!=height)return 0;
 AHardwareBuffer *buffer=NULL;AHardwareBuffer_Desc desc={0};int32_t space=0;
 if(AImage_getHardwareBuffer(im,&buffer)||!buffer||AImage_getDataSpace(im,&space))return 0;
 AHardwareBuffer_describe(buffer,&desc);
 const unsigned sample=desc.format==AHARDWAREBUFFER_FORMAT_YCbCr_P010||desc.format==0x7fa30c0aU?2:
     desc.format==AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420||desc.format==0x7fa30c04U?1:0;
 printf("IMAGE_LAYOUT index=%u format=%d ahb_format=0x%x sample_bytes=%u dataspace=0x%x\n",
        index,image_format,desc.format,sample,space);
 if(!sample)return 0;
#ifdef SQ2_IMAGE_HDR_METADATA
 if(!sq2_dump_image_hdr_metadata(buffer))return 0;
#endif
 size_t packed_size=(size_t)width*height*3/2*sample,at=0;
 uint8_t *packed=malloc(packed_size);if(!packed)return 0;
#ifdef SQ2_P010_AHB_READBACK
 if(sample==2) {
  struct quest2_video_buffer layout={0};void *address=NULL;
  if(quest2_video_buffer_import(buffer,&layout)){free(packed);return 0;}
  int result=AHardwareBuffer_lock(buffer,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&address);
  printf("IMAGE_AHB_LOCK index=%u result=%d ystride=%u cstride=%u uv=%u capacity=%llu\n",
     index,result,layout.ystride,layout.cstride,layout.uv,(unsigned long long)layout.capacity);
  int ok=!result&&address&&layout.capacity<=INT32_MAX&&layout.uv+2ULL<layout.capacity;
  if(ok) {
   const uint8_t *base=address;
   const size_t ybytes=(size_t)width*height*2,cbytes=ybytes/4;
   ok=copy_image_plane(packed,ybytes,base,layout.uv,layout.ystride,2,width,height,2)&&
      copy_image_plane(packed+ybytes,cbytes,base+layout.uv,layout.capacity-layout.uv,
                       layout.cstride,4,width/2,height/2,2)&&
      copy_image_plane(packed+ybytes+cbytes,cbytes,base+layout.uv+2,layout.capacity-layout.uv-2,
                       layout.cstride,4,width/2,height/2,2);
   if(ok)at=packed_size;
  }
  if(!result&&AHardwareBuffer_unlock(buffer,NULL))ok=0;
  quest2_video_buffer_release(&layout);
  if(!ok){free(packed);return 0;}
 } else
#endif
 for(int plane=0;plane<3;plane++){
  uint8_t *data=NULL;int len=0;int32_t row=0,pixel=0;
  if(AImage_getPlaneData(im,plane,&data,&len)||AImage_getPlaneRowStride(im,plane,&row)||AImage_getPlanePixelStride(im,plane,&pixel)){free(packed);return 0;}
  int w=plane?width/2:width,h=plane?height/2:height;
  if(!copy_image_plane(packed+at,packed_size-at,data,len,row,pixel,w,h,sample)){free(packed);return 0;}
  at+=(size_t)w*h*sample;
 }
 char path[128];snprintf(path,sizeof(path),"%s/sq2-ndk-frame-%u.bin",frame_directory,index);
 FILE *out=fopen(path,"wbx");if(!out){free(packed);return 0;}
 int ok=at==packed_size&&fwrite(packed,1,packed_size,out)==packed_size;
 if(fclose(out))ok=0;
 free(packed);return ok;
}
int sq2_mediacodec_frames_run(int argc,char **argv,ANativeWindow *external) {
 setvbuf(stdout,NULL,_IONBF,0);
 FILE *maps=fopen("/proc/self/maps","r");
 if(maps){char line[1024];while(fgets(line,sizeof(line),maps))
   if(strstr(line,"/libstagefright.so")||strstr(line,"/libsq2-p010-renderer.so"))printf("RENDERER_MAP %s",line);
  fclose(maps);}
 if(getenv("SQ2_FRAME_DIR"))frame_directory=getenv("SQ2_FRAME_DIR");
 if(argc>3 || (argc>2 && strcmp(argv[2],"--surface-csd") && strcmp(argv[2],"--surface-full-au") && strcmp(argv[2],"--csd") && strcmp(argv[2],"--csd-stripped"))) {
  fprintf(stderr,"usage: %s [codec [--surface-full-au|--surface-csd|--csd|--csd-stripped]]\n",argv[0]);return 64;
 }
#ifdef SQ2_SPLIT_INBAND_CSD
 if(argc>2&&strcmp(argv[2],"--surface-full-au")){fputs("In-band CSD diagnostic requires full-AU input\n",stderr);return 64;}
#endif
 if(external) {
  if(SQ2_VEC_FRAMES!=SQ2_WINDOW_FRAMES){fputs("External window requires 30 frames\n",stderr);return 64;}
 }
 unsigned input_interval_us=0;
 const char *interval=getenv("SQ2_INPUT_INTERVAL_US");
 if(interval){
  char *end=NULL;errno=0;unsigned long value=strtoul(interval,&end,10);
  if(errno||!interval[0]||*end||!value||value>100000||
     value*SQ2_VEC_FRAMES>10000000UL)return 64;
  input_interval_us=(unsigned)value;
  printf("INPUT_PACING interval_us=%u throughput_test=0\n",input_interval_us);
 }
 ABinderProcess_startThreadPool();
 const char *name=argc>1?argv[1]:SQ2_COMPONENT;
 printf("CREATE_BEGIN name=%s\n",name);
 fsync(1);
 AMediaCodec *c=AMediaCodec_createCodecByName(name);
 printf("CREATE_END nonnull=%d\n",c!=NULL);
 if(!c)return 2;
 int surface=external||(argc>2&&(!strcmp(argv[2],"--surface-csd")||!strcmp(argv[2],"--surface-full-au")));
 AImageReader *reader=NULL;ANativeWindow *window=external;
 if(surface&&!external){
  int r;
#ifdef SQ2_HEVC_VECTOR
  if(SQ2_VEC_BYTES_PER_SAMPLE==2) {
#ifdef SQ2_P010_SURFACE
   r=AImageReader_newWithDataSpace(SQ2_VEC_W,SQ2_VEC_H,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
      8,AHARDWAREBUFFER_FORMAT_YCbCr_P010,ADATASPACE_UNKNOWN,&reader);
#else
   AHardwareBuffer_Desc wanted={.width=SQ2_VEC_W,.height=SQ2_VEC_H,.layers=1,
      .format=AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM,
      .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
   AHardwareBuffer *allocation=NULL;
   r=AHardwareBuffer_allocate(&wanted,&allocation);printf("RGB10_ALLOCATE=%d\n",r);
   if(r){AMediaCodec_delete(c);return 7;}
   void *pixels=NULL;r=AHardwareBuffer_lock(allocation,AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY,-1,NULL,&pixels);
   printf("RGB10_WRITE_LOCK=%d\n",r);
   if(!r)r=AHardwareBuffer_unlock(allocation,NULL);
   AHardwareBuffer_release(allocation);
   if(r){AMediaCodec_delete(c);return 7;}
   r=AImageReader_newWithDataSpace(SQ2_VEC_W,SQ2_VEC_H,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
      8,AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM,ADATASPACE_UNKNOWN,&reader);
#endif
  }else
#endif
   r=AImageReader_newWithUsage(SQ2_VEC_W,SQ2_VEC_H,AIMAGE_FORMAT_YUV_420_888,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,8,&reader);
  printf("IMAGE_READER=%d\n",r);
  if(r||AImageReader_getWindow(reader,&window)){if(reader)AImageReader_delete(reader);AMediaCodec_delete(c);return 7;}
 }
 AMediaFormat *f=AMediaFormat_new();
 AMediaFormat_setString(f,AMEDIAFORMAT_KEY_MIME,SQ2_MIME);
 AMediaFormat_setInt32(f,AMEDIAFORMAT_KEY_WIDTH,SQ2_VEC_W);
 AMediaFormat_setInt32(f,AMEDIAFORMAT_KEY_HEIGHT,SQ2_VEC_H);
 // This pixel-verification sink must receive every queued frame.
 if(surface)AMediaFormat_setInt32(f,"allow-frame-drop",0);
 if(!surface
#ifdef SQ2_HEVC_VECTOR
    || SQ2_VEC_BYTES_PER_SAMPLE==2
#endif
   )AMediaFormat_setInt32(f,"color-format",SQ2_COLOR);
 int strip_csd=argc>2&&(!strcmp(argv[2],"--surface-csd")||!strcmp(argv[2],"--csd-stripped"));
 if(argc>2&&(strip_csd||!strcmp(argv[2],"--csd"))) {
  if(!add_csd(f)){AMediaFormat_delete(f);AMediaCodec_delete(c);return 6;}
 }
 puts("CONFIGURE_BEGIN");
 fsync(1);
 int rc=AMediaCodec_configure(c,f,window,NULL,0);AMediaFormat_delete(f);
 printf("CONFIGURE=%d\n",rc);
 if(rc){AMediaCodec_delete(c);if(reader)AImageReader_delete(reader);return 3;}
 if(surface){
  AMediaFormat *input=AMediaCodec_getInputFormat(c);int32_t allow=1;
  int present=input&&AMediaFormat_getInt32(input,"allow-frame-drop",&allow);
  printf("SURFACE_FRAME_DROP enabled=%d present=%d\n",allow,present);
  if(input)AMediaFormat_delete(input);
  if(!present||allow){AMediaCodec_delete(c);if(reader)AImageReader_delete(reader);return 3;}
 }
 rc=AMediaCodec_start(c);printf("START=%d\n",rc);
 fsync(1);
 if(rc){AMediaCodec_delete(c);if(reader)AImageReader_delete(reader);return 4;}
 struct window_callbacks callbacks={.mutex=PTHREAD_MUTEX_INITIALIZER};
 if(external){
  uint64_t pts[SQ2_VEC_FRAMES];
  for(unsigned i=0;i<SQ2_VEC_FRAMES;i++){
#ifdef SQ2_VEC_HAS_PTS
   pts[i]=sq2_vec_pts[i];
#else
   pts[i]=(uint64_t)i*33333;
#endif
  }
  if(!sq2_window_init(&callbacks.playback,pts,SQ2_VEC_FRAMES)||
     AMediaCodec_setOnFrameRenderedCallback(c,frame_rendered,&callbacks)!=AMEDIA_OK){
   AMediaCodec_stop(c);AMediaCodec_delete(c);pthread_mutex_destroy(&callbacks.mutex);return 4;
  }
  puts("WINDOW_PLAYBACK_READY frames=30 timed_release=1 consumer_wait=0 pixels_verified=0");
 }
#ifdef SQ2_SPLIT_INBAND_CSD
 unsigned configured=UINT32_MAX,config_packets=0;
#endif
 unsigned sent=0,frames=0,rendered=0;int eos=0,failed=0;double input_epoch=now(),deadline=input_epoch+12;
 while(now()<deadline&&(!eos||(reader&&frames<rendered))&&!failed){
  if(sent<=SQ2_VEC_FRAMES&&(!input_interval_us||now()>=input_epoch+sent*(input_interval_us/1e6))){
   ssize_t i=AMediaCodec_dequeueInputBuffer(c,1000);
   if(i>=0){
    size_t capacity=0;uint8_t *buf=AMediaCodec_getInputBuffer(c,i,&capacity);
    size_t bytes=sent<SQ2_VEC_FRAMES?sq2_vec_au[sent+1]-sq2_vec_au[sent]:0;
    if(!buf||capacity<bytes){printf("BAD_INPUT capacity=%zu\n",capacity);failed=1;break;}
    unsigned queue_flags=bytes?0:AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
#ifdef SQ2_SPLIT_INBAND_CSD
    int configuration=0;
    if(bytes && configured!=sent) {
     size_t headers=copy_au_part(buf,sent,1);
     if(headers){bytes=headers;configuration=1;queue_flags=AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG;}
    }
    if(bytes&&!configuration){bytes=copy_au_part(buf,sent,0);if(!bytes){failed=1;break;}}
#else
    if(bytes){
     if(strip_csd){bytes=copy_au_part(buf,sent,0);if(!bytes){failed=1;break;}}
     else memcpy(buf,sq2_vec_stream+sq2_vec_au[sent],bytes);
    }
#endif
    uint64_t pts=sent*33333;
#ifdef SQ2_VEC_HAS_PTS
    if(sent<SQ2_VEC_FRAMES)pts=sq2_vec_pts[sent];
#endif
    rc=AMediaCodec_queueInputBuffer(c,i,0,bytes,pts,queue_flags);
    printf("QUEUE frame=%u bytes=%zu rc=%d\n",sent,bytes,rc);fsync(1);if(rc){failed=1;break;}
#ifdef SQ2_SPLIT_INBAND_CSD
    if(configuration) {
     configured=sent;++config_packets;
     printf("INBAND_CSD frame=%u bytes=%zu pts=%llu packets=%u flags=%u\n",sent,bytes,(unsigned long long)pts,config_packets,queue_flags);
    } else ++sent;
#else
    ++sent;
#endif
   }else if(i!=AMEDIACODEC_INFO_TRY_AGAIN_LATER){printf("INPUT_ERROR=%zd\n",i);failed=1;}
  }
  // Consume each verification image before rendering the next (not a throughput test).
  AMediaCodecBufferInfo info={0};
  ssize_t o=reader&&frames<rendered?AMEDIACODEC_INFO_TRY_AGAIN_LATER:
      AMediaCodec_dequeueOutputBuffer(c,&info,1000);
  if(o==AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED){
   f=AMediaCodec_getOutputFormat(c);printf("FORMAT %s\n",AMediaFormat_toString(f));
   if(external) {
    int32_t color=0;
#ifdef SQ2_NORMAL_WINDOW
    int32_t software=0;
    int has_software=AMediaFormat_getInt32(f,"using-sw-renderer",&software);
    int has_color=AMediaFormat_getInt32(f,"color-format",&color);
    printf("WINDOW_RENDERER using_sw_renderer=%d present=%d color=0x%x expected=software_nv12\n",
           software,has_software,color);
    if(!has_color||color!=21||!has_software||software!=1)failed=1;
#else
    int32_t expected_color=0x7fa30c04;
#ifdef SQ2_HEVC_VECTOR
    if(SQ2_VEC_BYTES_PER_SAMPLE==2)expected_color=0x7fa30c0a;
#endif
    if(!AMediaFormat_getInt32(f,"color-format",&color)||color!=expected_color){
     printf("WINDOW_UNSUPPORTED_FORMAT color=0x%x expected=0x%x\n",color,expected_color);failed=1;
    }
#endif
   }
   int32_t standard=0,transfer=0,range=0;
   int hs=AMediaFormat_getInt32(f,"color-standard",&standard);
   int ht=AMediaFormat_getInt32(f,"color-transfer",&transfer);
   int hr=AMediaFormat_getInt32(f,"color-range",&range);
   printf("COLOR standard=%d present=%d transfer=%d present=%d range=%d present=%d\n",standard,hs,transfer,ht,range,hr);
   void *hdr=NULL;size_t hdrsize=0;
   if(AMediaFormat_getBuffer(f,"hdr-static-info",&hdr,&hdrsize)){
    printf("HDR_STATIC bytes=%zu hex=",hdrsize);
    if(hdrsize<=128)for(size_t j=0;j<hdrsize;j++)printf("%02x",((unsigned char*)hdr)[j]);
    else printf("diagnostic_limit_exceeded");
    puts("");
   } else puts("HDR_STATIC absent");
   AMediaFormat_delete(f);
  }
  else if(o>=0){
   size_t capacity=0;uint8_t *buf=surface?NULL:AMediaCodec_getOutputBuffer(c,o,&capacity);
   printf("OUTPUT pts=%lld size=%d offset=%d flags=%u capacity=%zu\n",(long long)info.presentationTimeUs,info.size,info.offset,info.flags,capacity);
   if(info.size>0&&!surface){
    if(!buf||info.offset<0||(size_t)info.offset>capacity||(size_t)info.size>capacity-(size_t)info.offset){failed=1;}
    else {char path[128];snprintf(path,sizeof(path),"%s/sq2-ndk-frame-%u.bin",frame_directory,frames);FILE *out=fopen(path,"wb");if(!out)failed=1;else {if(fwrite(buf+info.offset,1,info.size,out)!=(size_t)info.size)failed=1;fclose(out);}frames++;}
   }
   eos=!!(info.flags&AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
   int render=surface&&info.size>0;
   if(render&&external){
    int64_t target=0;
    pthread_mutex_lock(&callbacks.mutex);
    int valid=sq2_window_target(&callbacks.playback,frames,info.presentationTimeUs,monotonic_ns(),&target);
    pthread_mutex_unlock(&callbacks.mutex);
    int paced=valid&&!pace_window(target);
    int released=paced?AMediaCodec_releaseOutputBufferAtTime(c,o,target):AMEDIA_ERROR_INVALID_PARAMETER;
    if(!paced)AMediaCodec_releaseOutputBuffer(c,o,false);
    if(released!=AMEDIA_OK)failed=1;else rendered++;
    printf("WINDOW_RELEASE index=%u pts_us=%lld target_ns=%lld result=%d\n",
           frames,(long long)info.presentationTimeUs,(long long)target,released);
    frames++;
   }else{
    if(render)rendered++;
    if(AMediaCodec_releaseOutputBuffer(c,o,render)!=AMEDIA_OK)failed=1;
   }
  }else if(o!=AMEDIACODEC_INFO_TRY_AGAIN_LATER&&o!=AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED&&o!=AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED){printf("OUTPUT_ERROR=%zd\n",o);failed=1;}
  if(reader){
   for(unsigned n=0;n<8;n++){
    AImage *im=NULL;int r=AImageReader_acquireNextImage(reader,&im);
    if(r==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE)break;
    if(r||!im){printf("IMAGE_ERROR=%d\n",r);failed=1;break;}
    if(frames>=SQ2_VEC_FRAMES||!save_image(im,frames))failed=1;else frames++;
    AImage_delete(im);
   }
   if(frames<rendered)usleep(1000);
  }
 }
 if(external){
  for(;;){
   pthread_mutex_lock(&callbacks.mutex);
   unsigned count=callbacks.playback.callbacks;int invalid=callbacks.playback.invalid_callback;
   pthread_mutex_unlock(&callbacks.mutex);
   if(count==SQ2_WINDOW_FRAMES||invalid||failed||now()>=deadline)break;
   usleep(1000);
  }
  if(AMediaCodec_setOnFrameRenderedCallback(c,NULL,NULL)!=AMEDIA_OK)failed=1;
  pthread_mutex_lock(&callbacks.mutex);
  for(unsigned i=0;i<SQ2_WINDOW_FRAMES;i++)
   printf("WINDOW_CALLBACK index=%u pts_us=%lld target_ns=%lld callback_ns=%lld callback_arrival_ns=%lld\n",i,
      (long long)callbacks.playback.pts[i],(long long)callbacks.playback.target[i],
      (long long)callbacks.playback.callback_time[i],(long long)callbacks.playback.callback_arrival[i]);
  printf("WINDOW_COUNTS sent=%u outputs=%u timed_releases=%u callbacks=%u invalid_callback=%d eos=%d pixels_verified=0\n",
    sent,frames,rendered,callbacks.playback.callbacks,callbacks.playback.invalid_callback,eos);
  if(sent!=SQ2_VEC_FRAMES+1||rendered!=SQ2_VEC_FRAMES||
     callbacks.playback.callbacks!=SQ2_VEC_FRAMES||callbacks.playback.invalid_callback)failed=1;
  pthread_mutex_unlock(&callbacks.mutex);
 }
 int stop=AMediaCodec_stop(c),deleted=AMediaCodec_delete(c);
 if(external&&(stop||deleted))failed=1;
 pthread_mutex_destroy(&callbacks.mutex);
#ifdef SQ2_SPLIT_INBAND_CSD
 printf("CONFIGURATION_PACKETS sent=%u\n",config_packets);
#endif
 printf("TRANSPORT sent=%u frames=%u eos=%d failed=%d stop=%d delete=%d PIXELS_UNVERIFIED\n",
        sent,frames,eos,failed,stop,deleted);
 if(reader)AImageReader_delete(reader);
 return !failed&&frames==SQ2_VEC_FRAMES&&eos?0:5;
}
#ifndef SQ2_MEDIACODEC_JNI
int main(int argc,char **argv) {return sq2_mediacodec_frames_run(argc,argv,NULL);}
#endif
