#!/usr/bin/env python3
"""Exercise actual OMX input/EBD/flush/EOS code and CSD helper with sanitizers."""
from pathlib import Path
import argparse,hashlib,json,subprocess,shutil
parser=argparse.ArgumentParser(__doc__)
for name in ('source','parameters','picture','output'):
    parser.add_argument('--'+name,type=Path,required=True)
args=parser.parse_args()
P=args.output.resolve();P.mkdir(parents=True,exist_ok=False)
S=args.source/'mm-video-v4l2/vidc/vdec/src/omx_vdec_v4l2.cpp'
shutil.copy2(args.source/'mm-video-v4l2/vidc/vdec/inc/sq2-hevc-csd-prefix.h',P/'sq2-hevc-csd-prefix.h')
text=S.read_text()
block='    bool sq2_joined = false;'+text.split('    bool sq2_joined = false;',1)[1].split('    /*for use buffer we need to memcpy the data*/',1)[0]
ebd=text.split('OMX_ERRORTYPE omx_vdec::empty_buffer_done(',1)[1].split('\nint omx_vdec::async_message_process',1)[0]
ebd='OMX_ERRORTYPE Test::empty_buffer_done('+ebd
flush=text.split('    if (in_reconfig && flushType == OMX_CORE_OUTPUT_PORT_INDEX)',1)[1].split('    if (ioctl(drv_ctx.video_driver_fd, VIDIOC_DECODER_CMD, &dec))',1)[0]
flush='    if (in_reconfig && flushType == OMX_CORE_OUTPUT_PORT_INDEX)'+flush
eos=text.split('    if (temp_buffer->buffer_len == 0 && (buffer->nFlags & OMX_BUFFERFLAG_EOS)) {',1)[1].split('\n    if (buffer->nFlags & OMX_BUFFERFLAG_EOS) {',1)[0]
eos='    if (temp_buffer->buffer_len == 0 && (buffer->nFlags & OMX_BUFFERFLAG_EOS)) {'+eos
prefix=r"""
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <vector>
#include <fstream>
#include <iterator>
#include <linux/videodev2.h>
#include "sq2-hevc-csd-prefix.h"
#define OMX_BUFFERFLAG_CODECCONFIG 128
#define OMX_BUFFERFLAG_EOS 1
#define OMX_ErrorNone 0
#define OMX_ErrorBadParameter 1
#define OMX_ErrorInsufficientResources 2
#define OMX_ErrorHardware 3
#define OMX_COMPONENT_GENERATE_EBD 7
#define OMX_CORE_OUTPUT_PORT_INDEX 1
#define V4L2_CMD_FLUSH_CAPTURE 1
#define V4L2_CMD_FLUSH_OUTPUT 2
#define VDEC_CODECTYPE_HEVC 4
#define VDEC_S_SUCCESS 0
#define DEBUG_PRINT_HIGH(...) ((void)0)
#define DEBUG_PRINT_LOW(...) ((void)0)
#define OUTPUT_PORT 0
#define DEBUG_PRINT_ERROR(...) ((void)0)
#define VIDC_TRACE_NAME_HIGH(...) ((void)0)
#define VIDC_TRACE_INT_LOW(...) ((void)0)
using OMX_ERRORTYPE=int;
using OMX_HANDLETYPE=void*;
using OMX_U8=uint8_t;
struct Buffer {
 uint8_t*pBuffer; unsigned nAllocLen,nOffset,nFilledLen,nFlags;
 int64_t nTimeStamp; void*pMarkData;
};
using OMX_BUFFERHEADERTYPE=Buffer;
struct Payload { void*bufferaddr; size_t mmaped_size,offset; int pmem_fd; size_t buffer_len=0; };
struct auto_lock { explicit auto_lock(void*){} };
static bool fail_alloc=false;
extern "C" void *__real_realloc(void*,size_t);
extern "C" void *__wrap_realloc(void*p,size_t n) {
 if(fail_alloc){fail_alloc=false;return NULL;}return __real_realloc(p,n);
}
struct Test {
 Sq2HevcCsdPrefix m_sq2_hevc_csd;
 struct {int decoder_format=VDEC_CODECTYPE_HEVC,video_driver_fd=3; bool disable_dmx=false;
  struct {unsigned actualcount=2;size_t buffer_size=10000;} ip_buf;
  Payload ptr_inputbuffer[2];
 }drv_ctx;
 bool secure_mode=false,m_input_pass_buffer_fd=false,allocate_native_handle=false,input_use_buffer=false;
 bool input_flush_progress=false,output_flush_progress=false,in_reconfig=false,qbuf_fail=false;
 Buffer mem[2]{},heap[2]{}; Buffer*m_inp_mem_ptr=mem,*m_inp_heap_ptr=heap;
 uint8_t dma[2][10000]{},client[2][10000]{};
 int pending_input_buffers=0,callbacks=0,qbufs=0,stop_calls=0,stop_result=0;
 bool streaming[2]{};
 size_t last_length=0;
 void*buf_lock=nullptr,*m_app_data=this;
 Buffer*last_callback=nullptr;
 struct Callbacks{void(*EmptyBufferDone)(void*,void*,Buffer*);} m_cb;
 std::vector<Buffer*> returned;
 Test(){
  for(int i=0;i<2;i++){
   mem[i].pBuffer=dma[i];mem[i].nAllocLen=sizeof(dma[i]);
   heap[i].pBuffer=client[i];heap[i].nAllocLen=sizeof(client[i]);
   drv_ctx.ptr_inputbuffer[i]={dma[i],sizeof(dma[i]),0,-1};
  }
  m_cb.EmptyBufferDone=[](void*,void*p,Buffer*b){auto*t=static_cast<Test*>(p);t->callbacks++;t->last_callback=b;};
 }
 int ioctl(int,unsigned long request,void*){if(request==VIDIOC_DECODER_CMD){stop_calls++;return stop_result;}return 0;}
 void print_omx_buffer(const char*,Buffer*){}
 void post_event(unsigned long p,int,int){returned.push_back(reinterpret_cast<Buffer*>(p));}
 void drain(){for(auto*b:returned)assert(empty_buffer_done(this,b)==0);returned.clear();}
 void fill(int i,const std::vector<uint8_t>&v,unsigned flags=0,int64_t pts=33333,unsigned off=0){
  Buffer&b=input_use_buffer?heap[i]:mem[i];assert(off+v.size()<=b.nAllocLen);
  if(!v.empty())memcpy(b.pBuffer+off,v.data(),v.size());
  b.nOffset=off;b.nFilledLen=v.size();b.nFlags=flags;b.nTimeStamp=pts;b.pMarkData=(void*)0x1234;
  if(input_use_buffer){mem[i].nFilledLen=b.nFilledLen;mem[i].nFlags=flags;mem[i].nTimeStamp=pts;mem[i].pMarkData=b.pMarkData;}
 }
 int process(unsigned nPortIndex){
  Buffer*buffer=&mem[nPortIndex];Payload*temp_buffer=&drv_ctx.ptr_inputbuffer[nPortIndex];
  pending_input_buffers++;
"""
suffix=r"""
  last_length=sq2_joined_length;qbufs++;
  if(qbuf_fail){
   if(sq2_joined){m_sq2_hevc_csd.reset();post_event((unsigned long)buffer,VDEC_S_SUCCESS,OMX_COMPONENT_GENERATE_EBD);return OMX_ErrorHardware;}
  }
  if(sq2_joined)m_sq2_hevc_csd.reset();
  post_event((unsigned long)buffer,VDEC_S_SUCCESS,OMX_COMPONENT_GENERATE_EBD);
  return OMX_ErrorNone;
 }
 int empty_buffer_done(void*,Buffer*);
 void flush(unsigned flushType){v4l2_decoder_cmd dec{};
"""
end=r"""
  (void)dec;
 }
};
"""
tests=r"""
std::vector<uint8_t> read(const char*p){
 std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc,char**argv){
 assert(argc==3);auto csd=read(argv[1]),pic=read(argv[2]);assert(csd.size()==83&&pic.size()==5078);
 std::vector<uint8_t> joined=csd;joined.insert(joined.end(),pic.begin(),pic.end());
 int cases=0;
 #define CHECK_DONE() (++cases)
 {
  Test t;t.fill(0,csd,128,-777);assert(t.process(0)==0);assert(t.qbufs==0&&t.callbacks==0&&t.pending_input_buffers==1);
  t.drain();assert(t.callbacks==1&&t.last_callback==&t.mem[0]&&t.pending_input_buffers==0);
  memset(t.dma[0],0,sizeof(t.dma[0]));t.fill(1,pic,16,987654);
  auto mark=t.mem[1].pMarkData;assert(t.process(1)==0);assert(t.qbufs==1&&t.last_length==5161);
  assert(!memcmp(t.dma[1],joined.data(),joined.size()));assert(t.mem[1].nTimeStamp==987654&&t.mem[1].nFlags==16&&t.mem[1].pMarkData==mark);
  assert(t.m_sq2_hevc_csd.size()==0);t.drain();assert(t.callbacks==2&&t.pending_input_buffers==0);CHECK_DONE();
 }
 {
  Test t;t.input_use_buffer=true;t.fill(0,csd,128,-777,11);assert(t.process(0)==0);t.drain();assert(t.last_callback==&t.heap[0]);
  t.fill(1,pic,17,123456,17);assert(t.process(1)==0);assert(!memcmp(t.dma[1],joined.data(),joined.size()));
  assert(t.mem[1].nTimeStamp==123456&&t.mem[1].nFlags==17);t.drain();assert(t.last_callback==&t.heap[1]&&t.pending_input_buffers==0);CHECK_DONE();
 }
 {
  Test t;size_t pos=0;for(size_t n:{size_t(1),size_t(4),size_t(78)}){std::vector<uint8_t>v(csd.begin()+pos,csd.begin()+pos+n);t.fill(0,v,128);assert(t.process(0)==0);t.drain();pos+=n;}
  t.fill(1,pic);assert(t.process(1)==0&&t.last_length==5161);assert(!memcmp(t.dma[1],joined.data(),joined.size()));t.drain();assert(t.callbacks==4&&t.pending_input_buffers==0);CHECK_DONE();
 }
 for(int which=0;which<4;which++){
  Test t;t.fill(0,csd,128);if(which==0)t.mem[0].nOffset=10001;if(which==1)t.mem[0].nFilledLen=10001;
  if(which==2)t.drv_ctx.ptr_inputbuffer[0].offset=1;if(which==3)t.drv_ctx.ptr_inputbuffer[0].mmaped_size=82;
  assert(t.process(0)==OMX_ErrorBadParameter&&t.qbufs==0);t.drain();assert(t.callbacks==1&&t.pending_input_buffers==0&&t.m_sq2_hevc_csd.size()==0);CHECK_DONE();
 }
 {
  Test t;fail_alloc=true;t.fill(0,csd,128);assert(t.process(0)==OMX_ErrorInsufficientResources&&t.qbufs==0);t.drain();assert(t.callbacks==1&&t.pending_input_buffers==0);CHECK_DONE();
 }
 {
  Test t;t.fill(0,csd,128);assert(t.process(0)==0);t.drain();t.qbuf_fail=true;t.fill(1,pic);
  assert(t.process(1)==OMX_ErrorHardware&&t.m_sq2_hevc_csd.size()==0);t.drain();assert(t.callbacks==2&&t.pending_input_buffers==0);CHECK_DONE();
 }
 for(int which=0;which<3;which++){
  Test t;t.fill(0,csd,128);assert(t.process(0)==0);t.drain();t.in_reconfig=(which==0);t.flush(which==2?0:1);
  assert(t.m_sq2_hevc_csd.size()==(which==0?83:0));CHECK_DONE();
 }
 {
  Test t;t.fill(0,csd,128);assert(t.process(0)==0);t.drain();t.fill(1,{},1);
  assert(t.process(1)==OMX_ErrorNone&&t.qbufs==0&&t.stop_calls==1&&t.m_sq2_hevc_csd.size()==0);t.drain();assert(t.pending_input_buffers==0&&t.callbacks==2);CHECK_DONE();
 }
 {
  Test t;t.fill(0,csd,129);assert(t.process(0)==OMX_ErrorBadParameter&&t.qbufs==0);t.drain();assert(t.pending_input_buffers==0);CHECK_DONE();
 }
 for(int which=0;which<5;which++){
  Test t;if(which==0)t.secure_mode=true;if(which==1)t.m_input_pass_buffer_fd=true;if(which==2)t.allocate_native_handle=true;if(which==3)t.drv_ctx.disable_dmx=true;if(which==4)t.drv_ctx.decoder_format=0;
  t.fill(0,csd,128);assert(t.process(0)==0&&t.qbufs==1&&t.m_sq2_hevc_csd.size()==0);t.drain();CHECK_DONE();
 }
 {
  Test t;t.fill(0,pic,16,4444);assert(t.process(0)==0&&t.last_length==5078);assert(!memcmp(t.dma[0],pic.data(),pic.size()));t.drain();CHECK_DONE();
 }
 for(int which=0;which<7;which++){
  Sq2HevcCsdPrefix p;uint8_t out[20000]{};size_t n;auto invalid=csd;
  if(which==0)invalid={1,2,3,4,5,6};if(which==1)invalid={0,0,1,64,1};if(which==2)invalid[4]|=128;
  if(which==3)invalid[5]&=0xf8;if(which==4)invalid=pic;
  assert(p.consume(out,sizeof(out),invalid.data(),invalid.size(),true,false,&n)==Sq2HevcCsdPrefix::Stashed);
  auto next=pic;if(which==5)next=csd;if(which==6)next.clear();
  auto r=p.consume(out,sizeof(out),next.data(),next.size(),false,which==6,&n);assert(r==(which==6?Sq2HevcCsdPrefix::Pass:Sq2HevcCsdPrefix::BadInput));CHECK_DONE();
 }
 {
  Sq2HevcCsdPrefix p;uint8_t out[5161]{};size_t n;
  assert(p.consume(out,sizeof(out),csd.data(),csd.size(),true,false,&n)==Sq2HevcCsdPrefix::Stashed);
  assert(p.consume(out,sizeof(out)-1,pic.data(),pic.size(),false,false,&n)==Sq2HevcCsdPrefix::NoSpace);
  assert(p.consume(out,sizeof(out),pic.data(),SIZE_MAX,false,false,&n)==Sq2HevcCsdPrefix::NoSpace);
  assert(p.consume(out,sizeof(out),pic.data(),pic.size(),false,false,&n)==Sq2HevcCsdPrefix::Joined);
  assert(n==sizeof(out)&&!memcmp(out,joined.data(),n));p.reset();assert(p.size()==0);CHECK_DONE();
 }
 {
  Sq2HevcCsdPrefix p;uint8_t out[10000]{};size_t n;
  assert(p.consume(out,sizeof(out),csd.data(),csd.size(),true,false,&n)==Sq2HevcCsdPrefix::Stashed);
  fail_alloc=true;assert(p.consume(out,sizeof(out),csd.data(),csd.size(),true,false,&n)==Sq2HevcCsdPrefix::NoMemory);
  assert(p.size()==83);assert(p.consume(out,sizeof(out),pic.data(),pic.size(),false,false,&n)==Sq2HevcCsdPrefix::Joined);
  assert(!memcmp(out,joined.data(),n));CHECK_DONE();
 }
 {
  Test t;t.fill(0,csd,128);assert(t.process(0)==0);t.drain();t.stop_result=-1;t.fill(1,{},1);
  assert(t.process(1)==OMX_ErrorHardware&&t.stop_calls==1&&t.qbufs==0&&t.m_sq2_hevc_csd.size()==0);t.drain();assert(t.pending_input_buffers==0&&t.callbacks==2);CHECK_DONE();
 }
 printf("PASS %d actual helper/injected-block/EBD/flush/EOS cases\n",cases);
}
"""
code=prefix+block+"  temp_buffer->buffer_len=sq2_joined_length;\n  int rc;\n"+eos+suffix+flush+end+ebd+tests
(P/'host-prefix.cpp').write_text(code)
cmd=['g++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie','-no-pie','-Wl,--wrap=realloc',str(P/'host-prefix.cpp'),'-o',str(P/'host-prefix')]
r=subprocess.run(cmd,capture_output=True,text=True);(P/'host-build.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
run=[str(P/'host-prefix'),str(args.parameters),str(args.picture)]
r=subprocess.run(run,capture_output=True,text=True);(P/'host-run.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
j={'pass':True,'build_command':cmd,'run_command':run,'stdout':r.stdout,'scope':'Actual helper and injected input block, actual EBD routine and actual flush decision/reset extracted from candidate. Actual empty-EOS STOP branch is also extracted. Driver/QBUF and event delivery are host mocks; not device acceptance.', 'sha256':{str(f.resolve()):hashlib.sha256(f.read_bytes()).hexdigest() for f in [P/'sq2-hevc-csd-prefix.h',S,P/'host-prefix.cpp',Path(__file__)]}}
(P/'HOST.json').write_text(json.dumps(j,indent=2)+'\n')
print(r.stdout)
