// Decode a real container through MediaCodec and dump the first six raw frames.
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
extern void ABinderProcess_startThreadPool(void);
static double now(void) {struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec/1e9;}
int main(int argc,char**argv) {
 if(argc!=4){fprintf(stderr,"usage: FILE CODEC FRESH_OUTPUT_DIR\n");return 64;}
 setvbuf(stdout,NULL,_IONBF,0);ABinderProcess_startThreadPool();
 int fd=open(argv[1],O_RDONLY|O_CLOEXEC);if(fd<0)return 2;
 off_t size=lseek(fd,0,SEEK_END);if(size<=0){close(fd);return 2;}
 AMediaExtractor* ex=AMediaExtractor_new();
 media_status_t r=AMediaExtractor_setDataSourceFd(ex,fd,0,size);close(fd);
 if(r||AMediaExtractor_getTrackCount(ex)!=1){AMediaExtractor_delete(ex);return 3;}
 AMediaExtractor_selectTrack(ex,0);AMediaFormat* format=AMediaExtractor_getTrackFormat(ex,0);
 printf("INPUT %s\n",AMediaFormat_toString(format));
 AMediaCodec* codec=AMediaCodec_createCodecByName(argv[2]);if(!codec)return 4;
 r=AMediaCodec_configure(codec,format,NULL,NULL,0);printf("CONFIGURE=%d\n",r);
 AMediaFormat_delete(format);if(r){AMediaCodec_delete(codec);AMediaExtractor_delete(ex);return 5;}
 r=AMediaCodec_start(codec);printf("START=%d\n",r);
 if(r){AMediaCodec_delete(codec);AMediaExtractor_delete(ex);return 6;}
 unsigned inputs=0,outputs=0;int in_eos=0,out_eos=0,failed=0;
 double deadline=now()+25;int64_t last=-1;
 while(!failed&&!out_eos&&now()<deadline) {
  if(!in_eos) {
   ssize_t index=AMediaCodec_dequeueInputBuffer(codec,1000);
   if(index>=0) {
    size_t cap=0;uint8_t* data=AMediaCodec_getInputBuffer(codec,index,&cap);
    if(!data){failed=1;break;}
    ssize_t bytes=AMediaExtractor_readSampleData(ex,data,cap);
    if(bytes>=0 && (uint64_t)bytes>cap){failed=1;break;}
    if(bytes>=0 && inputs<6) {
     printf("SAMPLE index=%u bytes=%zd prefix=",inputs,bytes);
     for(size_t j=0;j<16 && j<(size_t)bytes;j++)printf("%02x",data[j]);
     puts("");
    }
    int64_t pts=bytes<0?0:AMediaExtractor_getSampleTime(ex);
    r=AMediaCodec_queueInputBuffer(codec,index,0,bytes<0?0:(size_t)bytes,pts,bytes<0?AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM:0);
    if(r){printf("QUEUE_ERROR=%d\n",r);failed=1;break;}
    if(bytes<0)in_eos=1;else{++inputs;AMediaExtractor_advance(ex);}
   }
  }
  AMediaCodecBufferInfo info;ssize_t index=AMediaCodec_dequeueOutputBuffer(codec,&info,1000);
  if(index==AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED){
   AMediaFormat* out=AMediaCodec_getOutputFormat(codec);
   printf("OUTPUT_FORMAT %s\n",AMediaFormat_toString(out));AMediaFormat_delete(out);
  } else if(index>=0){
   if(info.size){
    size_t cap=0;uint8_t* data=AMediaCodec_getOutputBuffer(codec,index,&cap);
    if(!data||info.offset<0||info.size<0||(uint64_t)info.offset+info.size>cap||info.presentationTimeUs<=last)failed=1;
    else {
     last=info.presentationTimeUs;
     if(outputs<6){
      char path[512];int n=snprintf(path,sizeof(path),"%s/frame-%u.bin",argv[3],outputs);
      FILE* f=n>0&&(size_t)n<sizeof(path)?fopen(path,"wbx"):NULL;
      if(!f)failed=1;else{if(fwrite(data+info.offset,1,info.size,f)!=(size_t)info.size)failed=1;if(fclose(f))failed=1;}
      printf("FRAME index=%u pts=%lld bytes=%d\n",outputs,(long long)info.presentationTimeUs,info.size);
     }
     ++outputs;
    }
   }
   if(info.flags&AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)out_eos=1;
   r=AMediaCodec_releaseOutputBuffer(codec,index,false);if(r)failed=1;
  }
 }
 if(!out_eos && now()>=deadline){puts("DECODE_TIMEOUT");failed=1;}
 printf("RESULT inputs=%u outputs=%u eos=%d failed=%d\n",inputs,outputs,out_eos,failed);
 r=AMediaCodec_stop(codec);printf("STOP=%d\n",r);if(r)failed=1;
 r=AMediaCodec_delete(codec);printf("DELETE=%d\n",r);if(r)failed=1;
 AMediaExtractor_delete(ex);
 return failed||!out_eos||!outputs||outputs!=inputs?1:0;
}
