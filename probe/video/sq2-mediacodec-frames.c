// Diagnostic real bitstream transport. Dumps output for independent verification.
#include <media/NdkMediaCodec.h>
#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
// Platform libbinder_ndk entry point: standalone clients need callback threads.
extern void ABinderProcess_startThreadPool(void);
typedef uint32_t u32;
typedef uint8_t u8;
#include "sq2-h264-vector.h"
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
 size_t prefix=0,at=startcode(0,&prefix);unsigned mask=0;
 while(at+prefix<sizeof(sq2_vec_stream)) {
  unsigned type=sq2_vec_stream[at+prefix]&31;
  size_t nextprefix=0,next=startcode(at+prefix+1,&nextprefix);
  if(type==7||type==8){
   AMediaFormat_setBuffer(f,type==7?"csd-0":"csd-1",sq2_vec_stream+at,next-at);
   printf("CSD type=%u bytes=%zu\n",type,next-at);mask|=type==7?1:2;
  }
  at=next;prefix=nextprefix;
 }
 return mask==3;
}
static size_t copy_au_without_csd(uint8_t *dst,unsigned index) {
 size_t begin=sq2_vec_au[index],end=sq2_vec_au[index+1],prefix=0;
 size_t at=startcode(begin,&prefix),written=0;
 if(at!=begin)return 0;
 while(at<end&&at+prefix<end){
  size_t nextprefix=0,next=startcode(at+prefix+1,&nextprefix);
  if(next>end)next=end;
  unsigned type=sq2_vec_stream[at+prefix]&31;
  if(type!=7&&type!=8){memcpy(dst+written,sq2_vec_stream+at,next-at);written+=next-at;}
  at=next;prefix=nextprefix;
 }
 return written;
}
static int save_image(AImage *im,unsigned index){
 int32_t width=0,height=0,planes=0;int64_t timestamp=0;AImageCropRect crop;
 if(AImage_getWidth(im,&width)||AImage_getHeight(im,&height)||AImage_getNumberOfPlanes(im,&planes)||
    AImage_getCropRect(im,&crop)||AImage_getTimestamp(im,&timestamp))return 0;
 printf("IMAGE index=%u width=%d height=%d planes=%d timestamp_ns=%lld crop=%d,%d,%d,%d\n",
        index,width,height,planes,(long long)timestamp,crop.left,crop.top,crop.right,crop.bottom);
 if(width!=SQ2_VEC_W||height!=SQ2_VEC_H||planes!=3||crop.left||crop.top||crop.right!=width||crop.bottom!=height)return 0;
 uint8_t packed[SQ2_VEC_W*SQ2_VEC_H*3/2];size_t at=0;
 for(int plane=0;plane<3;plane++){
  uint8_t *data=NULL;int len=0;int32_t row=0,pixel=0;
  if(AImage_getPlaneData(im,plane,&data,&len)||AImage_getPlaneRowStride(im,plane,&row)||AImage_getPlanePixelStride(im,plane,&pixel))return 0;
  int w=plane?width/2:width,h=plane?height/2:height;
  if(!data||len<0||row<=0||pixel<=0||(uint64_t)(h-1)*row+(uint64_t)(w-1)*pixel>=(uint64_t)len)return 0;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)packed[at++]=data[(size_t)y*row+(size_t)x*pixel];
 }
 char path[128];snprintf(path,sizeof(path),"/data/local/tmp/sq2-ndk-frame-%u.bin",index);
 FILE *out=fopen(path,"wb");if(!out)return 0;
 int ok=fwrite(packed,1,sizeof(packed),out)==sizeof(packed);if(fclose(out))ok=0;
 return ok;
}
int main(int argc, char **argv) {
 setvbuf(stdout,NULL,_IONBF,0);
 ABinderProcess_startThreadPool();
 const char *name=argc>1?argv[1]:"c2.sq2.avc.experimental";
 printf("CREATE_BEGIN name=%s\n",name);
 fsync(1);
 AMediaCodec *c=AMediaCodec_createCodecByName(name);
 printf("CREATE_END nonnull=%d\n",c!=NULL);
 if(!c)return 2;
 int surface=argc>2&&!strcmp(argv[2],"--surface-csd");
 AImageReader *reader=NULL;ANativeWindow *window=NULL;
 if(surface){
  int r=AImageReader_newWithUsage(SQ2_VEC_W,SQ2_VEC_H,AIMAGE_FORMAT_YUV_420_888,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,8,&reader);
  printf("IMAGE_READER=%d\n",r);
  if(r||AImageReader_getWindow(reader,&window)){if(reader)AImageReader_delete(reader);AMediaCodec_delete(c);return 7;}
 }
 AMediaFormat *f=AMediaFormat_new();
 AMediaFormat_setString(f,AMEDIAFORMAT_KEY_MIME,"video/avc");
 AMediaFormat_setInt32(f,AMEDIAFORMAT_KEY_WIDTH,SQ2_VEC_W);
 AMediaFormat_setInt32(f,AMEDIAFORMAT_KEY_HEIGHT,SQ2_VEC_H);
 if(!surface)AMediaFormat_setInt32(f,"color-format",19);
 int strip_csd=surface||(argc>2&&!strcmp(argv[2],"--csd-stripped"));
 if(argc>2&&(strip_csd||!strcmp(argv[2],"--csd"))) {
  if(!add_csd(f)){AMediaFormat_delete(f);AMediaCodec_delete(c);return 6;}
 }
 puts("CONFIGURE_BEGIN");
 fsync(1);
 int rc=AMediaCodec_configure(c,f,window,NULL,0);AMediaFormat_delete(f);
 printf("CONFIGURE=%d\n",rc);
 if(rc){AMediaCodec_delete(c);if(reader)AImageReader_delete(reader);return 3;}
 rc=AMediaCodec_start(c);printf("START=%d\n",rc);
 fsync(1);
 if(rc){AMediaCodec_delete(c);if(reader)AImageReader_delete(reader);return 4;}
 unsigned sent=0,frames=0,rendered=0;int eos=0,failed=0;double deadline=now()+12;
 while(now()<deadline&&(!eos||(surface&&frames<rendered))&&!failed){
  if(sent<=SQ2_VEC_FRAMES){
   ssize_t i=AMediaCodec_dequeueInputBuffer(c,1000);
   if(i>=0){
    size_t capacity=0;uint8_t *buf=AMediaCodec_getInputBuffer(c,i,&capacity);
    size_t bytes=sent<SQ2_VEC_FRAMES?sq2_vec_au[sent+1]-sq2_vec_au[sent]:0;
    if(!buf||capacity<bytes){printf("BAD_INPUT capacity=%zu\n",capacity);failed=1;break;}
    if(bytes){
     if(strip_csd){bytes=copy_au_without_csd(buf,sent);if(!bytes){failed=1;break;}}
     else memcpy(buf,sq2_vec_stream+sq2_vec_au[sent],bytes);
    }
    uint64_t pts=sent*33333;
#ifdef SQ2_VEC_HAS_PTS
    if(sent<SQ2_VEC_FRAMES)pts=sq2_vec_pts[sent];
#endif
    rc=AMediaCodec_queueInputBuffer(c,i,0,bytes,pts,bytes?0:AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    printf("QUEUE frame=%u bytes=%zu rc=%d\n",sent,bytes,rc);fsync(1);if(rc){failed=1;break;}sent++;
   }else if(i!=AMEDIACODEC_INFO_TRY_AGAIN_LATER){printf("INPUT_ERROR=%zd\n",i);failed=1;}
  }
  AMediaCodecBufferInfo info={0};ssize_t o=AMediaCodec_dequeueOutputBuffer(c,&info,1000);
  if(o==AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED){f=AMediaCodec_getOutputFormat(c);printf("FORMAT %s\n",AMediaFormat_toString(f));AMediaFormat_delete(f);}
  else if(o>=0){
   size_t capacity=0;uint8_t *buf=surface?NULL:AMediaCodec_getOutputBuffer(c,o,&capacity);
   printf("OUTPUT pts=%lld size=%d offset=%d flags=%u capacity=%zu\n",(long long)info.presentationTimeUs,info.size,info.offset,info.flags,capacity);
   if(info.size>0&&!surface){
    if(!buf||info.offset<0||(size_t)info.offset>capacity||(size_t)info.size>capacity-(size_t)info.offset){failed=1;}
    else {char path[128];snprintf(path,sizeof(path),"/data/local/tmp/sq2-ndk-frame-%u.bin",frames);FILE *out=fopen(path,"wb");if(!out)failed=1;else {if(fwrite(buf+info.offset,1,info.size,out)!=(size_t)info.size)failed=1;fclose(out);}frames++;}
   }
   eos=!!(info.flags&AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
   int render=surface&&info.size>0;if(render)rendered++;
   if(AMediaCodec_releaseOutputBuffer(c,o,render)!=AMEDIA_OK)failed=1;
  }else if(o!=AMEDIACODEC_INFO_TRY_AGAIN_LATER&&o!=AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED&&o!=AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED){printf("OUTPUT_ERROR=%zd\n",o);failed=1;}
  if(surface){
   for(unsigned n=0;n<8;n++){
    AImage *im=NULL;int r=AImageReader_acquireNextImage(reader,&im);
    if(r==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE)break;
    if(r||!im){printf("IMAGE_ERROR=%d\n",r);failed=1;break;}
    if(frames>=SQ2_VEC_FRAMES||!save_image(im,frames))failed=1;else frames++;
    AImage_delete(im);
   }
  }
 }
 printf("TRANSPORT sent=%u frames=%u eos=%d failed=%d PIXELS_UNVERIFIED\n",sent,frames,eos,failed);
 AMediaCodec_stop(c);AMediaCodec_delete(c);
 if(reader)AImageReader_delete(reader);
 return !failed&&frames==SQ2_VEC_FRAMES&&eos?0:5;
}
