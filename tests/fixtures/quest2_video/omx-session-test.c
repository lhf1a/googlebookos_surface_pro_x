#include "quest2-omx-session.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned frames;
static int sink(void *opaque, const struct quest2_omx_frame *f) {
 (void)opaque;
 static const unsigned tokens[]={0,1,2};
 assert(frames<3 && f->token==tokens[frames] && f->width==4 && f->height==4);
 assert(f->stride>=4 && f->slice>=4 && f->bytes>=f->stride*(f->slice+2));
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)assert(f->data[y*f->stride+x]==0x10+frames);
 for(unsigned y=0;y<2;y++)for(unsigned x=0;x<4;x++)assert(f->data[f->stride*(f->slice+y)+x]==0x50+frames);
 frames++;return 0;
}
int main(int argc,char **argv) {
 assert(argc==2);
 struct quest2_omx_session *s=NULL;
 int r=quest2_omx_open(&s,argv[1],4,4,sink,NULL);
 if(getenv("MOCK_FORMAT_FAIL")||getenv("MOCK_ALLOC_FAIL")){assert(r<0 && !s);return 0;}
 assert(!r && s);
 const unsigned char data[]={1,2,3,4,5};
 assert(quest2_omx_submit(s,data,5,0)==-EMSGSIZE);
 unsigned tokens[]={0,2,1};
 for(unsigned i=0;i<3;i++){
  r=quest2_omx_submit(s,data,4,tokens[i]);
  if(getenv("MOCK_ETB_FAIL")){assert(r==-EIO);assert(!quest2_omx_close(s));return 0;}
  assert(!r);assert(!quest2_omx_pump(s,0));
 }
 assert(!quest2_omx_drain(s));assert(quest2_omx_submit(s,data,4,3)==-EPIPE);
 for(unsigned i=0;i<10&&!quest2_omx_ended(s);i++){
  r=quest2_omx_pump(s,1);
  if(getenv("MOCK_BAD_BOUNDS") && r){assert(r==-EIO && !frames);assert(!quest2_omx_close(s));return 0;}
  assert(!r);
 }
 assert(frames==3 && quest2_omx_ended(s));assert(!quest2_omx_close(s));
 puts("SESSION_PASS actual_callback_pixels=3 eos=1");return 0;
}
