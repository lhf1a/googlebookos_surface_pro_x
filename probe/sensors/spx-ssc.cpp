// SPDX-License-Identifier: GPL-2.0-only
// Wire schemas: libqmi qmi-service-ssc.json and libssc/data/ssc-{common,sensor-suid,sensor-accelerometer}.proto.
#include "spx-ssc.h"
#include <sys/socket.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace spx {
using Bytes=std::vector<uint8_t>;
static uint16_t u16(const uint8_t*p){return p[0]|uint16_t(p[1])<<8;}
static uint64_t little(const uint8_t*p,size_t n){uint64_t x=0;for(size_t j=0;j<n;j++)x|=uint64_t(p[j])<<(8*j);return x;}
static void le(Bytes& b,uint64_t v,int n){for(int j=0;j<n;j++)b.push_back(v>>(8*j));}
static void var(Bytes& b,uint64_t v){while(v>127){b.push_back((v&127)|128);v>>=7;}b.push_back(v);}
static Bytes blob(int field,const Bytes& data){Bytes b;var(b,field*8+2);var(b,data.size());b.insert(b.end(),data.begin(),data.end());return b;}
static void append(Bytes&a,const Bytes&b){a.insert(a.end(),b.begin(),b.end());}
struct Field {unsigned key,wire;uint64_t value=0;Bytes bytes;};
static bool parse(const Bytes&b,std::vector<Field>&out){
 size_t p=0;out.clear();
 auto vi=[&](uint64_t&v){v=0;for(unsigned s=0;s<64;s+=7){if(p>=b.size())return false;uint8_t x=b[p++];if(s==63&&x>1)return false;v|=uint64_t(x&127)<<s;if(x<128)return true;}return false;};
 while(p<b.size()){
  uint64_t k;if(!vi(k)||!(k>>3)||k>UINT32_MAX)return false;
  Field f{unsigned(k>>3),unsigned(k&7),0,{}};
  if(f.wire==0){if(!vi(f.value))return false;}
  else {
   uint64_t n=f.wire==1?8:f.wire==5?4:0;
   if(f.wire==2){if(!vi(n))return false;}else if(!n)return false;
   if(n>b.size()-p)return false;
   f.bytes.assign(b.begin()+p,b.begin()+p+n);p+=n;
   if(f.wire!=2)f.value=little(f.bytes.data(),n);
  }
  out.push_back(std::move(f));
 }
 return true;
}
static const Field* find(const std::vector<Field>&f,unsigned k,unsigned wire){
 for(const auto&v:f)if(v.key==k&&v.wire==wire)return &v;
 return nullptr;
}
static bool events(const Bytes&packet,Bytes&uid,std::vector<Field>&ev){
 if(packet.size()<7||packet[0]!=4||u16(packet.data()+5)!=packet.size()-7)return false;
 if(u16(packet.data()+3)!=0x21&&u16(packet.data()+3)!=0x22)return false;
 for(size_t p=7;p<packet.size();){
  if(packet.size()-p<3)return false;
  unsigned type=packet[p],n=u16(packet.data()+p+1);p+=3;
  if(n>packet.size()-p)return false;
  if(type==2){
   if(n<2||u16(packet.data()+p)!=n-2)return false;
   Bytes b(packet.begin()+p+2,packet.begin()+p+n);std::vector<Field>root;
   if(!parse(b,root))return false;
   auto id=find(root,1,2);if(!id)return false;uid=id->bytes;
   for(auto&f:root)if(f.key==2&&f.wire==2)ev.push_back(std::move(f));
  }
  p+=n;
 }
 return !ev.empty();
}
static int64_t now(){timespec t{};clock_gettime(CLOCK_BOOTTIME,&t);return int64_t(t.tv_sec)*1000000000+t.tv_nsec;}
void SscAccel::close(){if(fd>=0)::close(fd);fd=-1;uid.clear();pending.clear();timeSet=false;}
bool SscAccel::receive(Bytes& b,int timeoutMs){
 pollfd p{fd,POLLIN,0};
 if(poll(&p,1,timeoutMs)!=1)return false;
 if(p.revents&(POLLERR|POLLHUP|POLLNVAL)){close();return false;}
 if(!(p.revents&POLLIN))return false;
 uint8_t buf[65536];sockaddr_qrtr from{};socklen_t len=sizeof(from);
 ssize_t n=recvfrom(fd,buf,sizeof(buf),MSG_TRUNC,reinterpret_cast<sockaddr*>(&from),&len);
 if(n<=0||n>ssize_t(sizeof(buf))||len!=sizeof(from)||from.sq_node!=node||from.sq_port!=port)return false;
 b.assign(buf,buf+n);return true;
}
bool SscAccel::send(uint32_t msg,const Bytes&data){
 Bytes pb=blob(1,uid);pb.push_back(0x15);le(pb,msg,4);append(pb,blob(3,{8,1,16,0}));append(pb,blob(4,blob(2,data)));
 Bytes body{1};le(body,pb.size()+2,2);le(body,pb.size(),2);append(body,pb);append(body,{16,1,0,1});
 Bytes packet{0};le(packet,++transaction,2);le(packet,0x20,2);le(packet,body.size(),2);append(packet,body);
 sockaddr_qrtr dst{AF_QIPCRTR,node,port};
 return sendto(fd,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&dst),sizeof(dst))==ssize_t(packet.size());
}
bool SscAccel::open(){
 close();fd=socket(AF_QIPCRTR,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return false;
 sockaddr_qrtr local{AF_QIPCRTR,0,0};socklen_t len=sizeof(local);
 // QRTR bind requires the kernel-assigned local node, not node zero.
 if(getsockname(fd,reinterpret_cast<sockaddr*>(&local),&len)||bind(fd,reinterpret_cast<sockaddr*>(&local),len)){close();return false;}
 node=local.sq_node;port=QRTR_PORT_CTRL;
 sockaddr_qrtr control{AF_QIPCRTR,node,port};
 qrtr_ctrl_pkt lookup{};lookup.cmd=QRTR_TYPE_NEW_LOOKUP;lookup.server.service=400;
 if(sendto(fd,&lookup,sizeof(lookup),0,reinterpret_cast<sockaddr*>(&control),sizeof(control))!=sizeof(lookup)){close();return false;}
 Bytes packet;bool found=false;int64_t deadline=now()+1500000000;
 while(now()<deadline){
  if(!receive(packet,200))continue;
  if(packet.size()>=sizeof(qrtr_ctrl_pkt)){
   qrtr_ctrl_pkt p{};std::memcpy(&p,packet.data(),sizeof(p));
   if(p.cmd==QRTR_TYPE_NEW_SERVER&&p.server.service==400&&p.server.port){node=p.server.node;port=p.server.port;found=true;break;}
  }
 }
 if(!found){close();return false;}
 uid={9};uid.insert(uid.end(),8,0xab);uid.push_back(17);uid.insert(uid.end(),8,0xab);
 Bytes query=blob(1,{'a','c','c','e','l'});append(query,{16,0,24,1});
 if(!send(512,query)){close();return false;}
 deadline=now()+1500000000;
 while(now()<deadline){
  if(!receive(packet,200))continue;
  Bytes origin;std::vector<Field> ev;
  if(!events(packet,origin,ev)||origin!=uid)continue;
  for(auto&e:ev){
   std::vector<Field> f,suids,id;
   if(!parse(e.bytes,f))continue;
   auto mid=find(f,1,5);auto payload=find(f,3,2);
   if(!mid||mid->value!=768||!payload||!parse(payload->bytes,suids))continue;
   auto type=find(suids,1,2);auto sensor=find(suids,2,2);
   if(!type||type->bytes!=Bytes({'a','c','c','e','l'})||!sensor||!parse(sensor->bytes,id))continue;
   auto low=find(id,1,1),high=find(id,2,1);
   if(low&&high){uid={9};le(uid,low->value,8);uid.push_back(17);le(uid,high->value,8);return true;}
  }
 }
 close();return false;
}
bool SscAccel::rate(float hz){
 if(fd<0||!std::isfinite(hz)||hz<=0)return false;
 uint32_t bits;std::memcpy(&bits,&hz,4);Bytes payload{13};le(payload,bits,4);return send(513,payload);
}
int SscAccel::read(Acceleration&out,int timeoutMs){
 if(!pending.empty()){out=pending.front();pending.erase(pending.begin());return true;}
 Bytes packet;if(fd<0||!receive(packet,timeoutMs))return fd<0?-1:0;
 Bytes origin;std::vector<Field> ev;
 if(!events(packet,origin,ev)||origin!=uid)return false;
 for(const auto&e:ev){
  std::vector<Field> f,values;
  if(!parse(e.bytes,f))continue;
  auto mid=find(f,1,5),ts=find(f,2,1),payload=find(f,3,2);
  if(!mid||mid->value!=1025||!ts||!payload||!parse(payload->bytes,values))continue;
  Acceleration a{};size_t n=0;bool valid=true;
  for(const auto&v:values)if(v.key==1){
   if(v.wire==5&&n<3){uint32_t bits=v.value;std::memcpy(&a.xyz[n++],&bits,4);}
   else if(v.wire==2&&v.bytes.size()==12&&n==0){for(int c=0;c<3;c++){uint32_t bits=little(v.bytes.data()+4*c,4);std::memcpy(&a.xyz[n++],&bits,4);}}
   else valid=false;
  }
  auto accuracy=find(values,2,0);
  for(float x:a.xyz)if(!std::isfinite(x))valid=false;
  if(!valid||n!=3||!accuracy||accuracy->value>3)continue;
  a.accuracy=accuracy->value;
  // DSP ticks are 19.2 MHz. Anchor once to Android CLOCK_BOOTTIME.
  if(ts->value>UINT64_MAX/1000)continue;
  int64_t timestamp=(ts->value/192)*10000+(ts->value%192)*10000/192;
  if(!timeSet){timeOffset=now()-timestamp;timeSet=true;}
  a.timestamp=timestamp+timeOffset;pending.push_back(a);
 }
 if(pending.empty())return false;
 out=pending.front();pending.erase(pending.begin());return true;
}
}
