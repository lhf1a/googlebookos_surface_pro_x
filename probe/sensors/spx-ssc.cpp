// SPDX-License-Identifier: GPL-2.0-only
// Wire schemas: libqmi qmi-service-ssc.json and libssc/data/ssc-{common,sensor-suid,sensor-accelerometer}.proto.
// Targeted SUID lookup only ("accel", "gyro", "mag"); never issue empty data_type enumeration.
#include "spx-ssc.h"
#include <sys/socket.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <utility>
namespace spx {
using Bytes=std::vector<uint8_t>;
static constexpr uint32_t kSscMsgStdFlushReq=2;
static constexpr uint32_t kSscMsgClientDisableReq=10;
static constexpr uint32_t kSscMsgSuidReq=512;
static constexpr uint32_t kSscMsgStdSensorConfig=513;
static constexpr uint8_t kSscDeliveryWakeup=0;
static constexpr uint8_t kSscDeliveryNoWakeup=1;
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
static Bytes suidServiceUid(){
 Bytes u{9};u.insert(u.end(),8,0xab);u.push_back(17);u.insert(u.end(),8,0xab);
 return u;
}
static bool buildSuidQuery(const std::string& dataType,Bytes& query){
 // Empty data_type requests full sensor enumeration which crashes SLPI firmware.
 if(dataType.empty())return false;
 Bytes name(dataType.begin(),dataType.end());
 query=blob(1,name);append(query,{16,0,24,1});
 return true;
}
static SscProbeResult parseSuidResponse(const Bytes& packet,const Bytes& serviceUid,const std::string& expectedType,Bytes& outUid){
 if(expectedType.empty())return SscProbeResult::Unavailable;
 Bytes origin;std::vector<Field> ev;
 if(!events(packet,origin,ev)||origin!=serviceUid)return SscProbeResult::Unavailable;
 Bytes wanted(expectedType.begin(),expectedType.end());
 for(const auto&e:ev){
  std::vector<Field> f,suids,id;
  if(!parse(e.bytes,f))continue;
  auto mid=find(f,1,5);auto payload=find(f,3,2);
  if(!mid||mid->value!=768||!payload||!parse(payload->bytes,suids))continue;
  auto dt=find(suids,1,2);
  if(!dt||dt->bytes!=wanted)continue;
  auto sensor=find(suids,2,2);
  if(!sensor||!parse(sensor->bytes,id))return SscProbeResult::Missing;
  auto low=find(id,1,1),high=find(id,2,1);
  if(!low||!high)return SscProbeResult::Missing;
  outUid={9};le(outUid,low->value,8);outUid.push_back(17);le(outUid,high->value,8);
  return SscProbeResult::Found;
 }
 return SscProbeResult::Unavailable;
}
static bool buildRatePayload(float hz,Bytes& payload){
 if(!std::isfinite(hz)||hz<=0)return false;
 uint32_t bits;std::memcpy(&bits,&hz,4);
 payload={13};le(payload,bits,4);
 return true;
}
static bool buildRequestPacket(const Bytes& targetUid,uint32_t msg,const Bytes& data,uint8_t deliveryType,uint16_t txn,Bytes& packet,uint32_t batchPeriodUs=0){
 if(targetUid.empty())return false;
 Bytes pb=blob(1,targetUid);
 pb.push_back(0x15);le(pb,msg,4);
 append(pb,blob(3,{8,1,16,deliveryType}));
 Bytes req;
 if(batchPeriodUs>0){
  Bytes bs;
  var(bs,1*8+0);var(bs,batchPeriodUs);
  var(bs,2*8+0);var(bs,batchPeriodUs);
  append(req,blob(1,bs));
 }
 if(!data.empty())append(req,blob(2,data));
 append(pb,blob(4,req));
 Bytes body{1};le(body,pb.size()+2,2);le(body,pb.size(),2);append(body,pb);append(body,{16,1,0,1});
 if(body.size()>UINT16_MAX)return false;
 packet={0};le(packet,txn,2);le(packet,0x20,2);le(packet,body.size(),2);append(packet,body);
 return true;
}
static bool isQmiClientResponse(const Bytes& packet,uint16_t expectedTxn){
 return packet.size()>=7&&packet[0]==2&&u16(packet.data()+1)==expectedTxn&&u16(packet.data()+3)==0x20&&u16(packet.data()+5)==packet.size()-7;
}
static bool isPeerDownCtrl(const Bytes& packet,uint32_t peerNode,uint32_t peerPort){
 if(packet.size()<sizeof(qrtr_ctrl_pkt))return false;
 qrtr_ctrl_pkt p{};std::memcpy(&p,packet.data(),sizeof(p));
 if(p.cmd==QRTR_TYPE_BYE)return p.client.node==peerNode;
 if(p.cmd==QRTR_TYPE_DEL_SERVER)return p.server.node==peerNode&&p.server.port==peerPort;
 return false;
}
static bool parseSamplePacket(const Bytes& packet,const Bytes& expectedUid,std::vector<SscSample>& out,int64_t& timeOffset,bool& timeSet,int64_t nowNs){
 Bytes origin;std::vector<Field> ev;
 if(!events(packet,origin,ev)||origin!=expectedUid)return false;
 size_t before=out.size();
 for(const auto&e:ev){
  std::vector<Field> f,values;
  if(!parse(e.bytes,f))continue;
  auto mid=find(f,1,5),ts=find(f,2,1),payload=find(f,3,2);
  if(!mid||mid->value!=1025||!ts||!payload||!parse(payload->bytes,values))continue;
  SscSample s{};size_t n=0;bool valid=true;
  for(const auto&v:values)if(v.key==1){
   if(v.wire==5&&n<3){uint32_t bits=v.value;std::memcpy(&s.xyz[n++],&bits,4);}
   else if(v.wire==2&&v.bytes.size()==12&&n==0){for(int c=0;c<3;c++){uint32_t bits=little(v.bytes.data()+4*c,4);std::memcpy(&s.xyz[n++],&bits,4);}}
   else valid=false;
  }
  auto accuracy=find(values,2,0);
  for(float x:s.xyz)if(!std::isfinite(x))valid=false;
  if(!valid||n!=3||!accuracy||accuracy->value>3)continue;
  s.accuracy=int(accuracy->value);
  // DSP ticks are 19.2 MHz. Anchor once to Android CLOCK_BOOTTIME.
  if(ts->value>UINT64_MAX/1000)continue;
  int64_t timestamp=int64_t((ts->value/192)*10000+(ts->value%192)*10000/192);
  if(!timeSet){timeOffset=nowNs-timestamp;timeSet=true;}
  s.timestamp=timestamp+timeOffset;out.push_back(s);
 }
 return out.size()>before;
}
static int64_t now(){timespec t{};clock_gettime(CLOCK_BOOTTIME,&t);return int64_t(t.tv_sec)*1000000000+t.tv_nsec;}
SscSensor::SscSensor(std::string dataType):type(std::move(dataType)){}
bool SscSensor::isOpen() const{return fd>=0&&!uid.empty();}
void SscSensor::closeRaw(){
 if(fd>=0)::close(fd);
 fd=-1;node=0;port=0;localNode=0;streaming=false;uid.clear();pending.clear();timeSet=false;
}
void SscSensor::drain(int quietMs){
 if(fd<0)return;
 for(int i=0;i<32;i++){
  int remaining=quietMs;
  pollfd p{fd,POLLIN,0};
  int pr=poll(&p,1,remaining);
  if(pr<0&&errno==EINTR)continue;
  if(pr!=1)break;
  if(p.revents&(POLLERR|POLLHUP|POLLNVAL)){closeRaw();break;}
  if(!(p.revents&POLLIN))break;
  uint8_t buf[65536];sockaddr_qrtr from{};socklen_t len=sizeof(from);
  ssize_t n=recvfrom(fd,buf,sizeof(buf),MSG_DONTWAIT|MSG_TRUNC,reinterpret_cast<sockaddr*>(&from),&len);
  if(n<=0)break;
  if(port!=QRTR_PORT_CTRL&&len==sizeof(from)&&from.sq_node==localNode&&from.sq_port==QRTR_PORT_CTRL&&n<=ssize_t(sizeof(buf))){
   Bytes pkt(buf,buf+n);
   if(isPeerDownCtrl(pkt,node,port)){closeRaw();break;}
  }
  quietMs=0;
 }
}
bool SscSensor::disable(int timeoutMs){
 if(fd<0||uid.empty()){streaming=false;pending.clear();timeSet=false;return false;}
 uint16_t txn=transaction+1;
 if(!send(kSscMsgClientDisableReq,{},kSscDeliveryWakeup)){closeRaw();return false;}
 Bytes packet;bool acked=false;int64_t deadline=now()+int64_t(std::max(1,timeoutMs))*1000000;
 while(now()<deadline&&fd>=0){
  int waitMs=int(std::min<int64_t>(50,std::max<int64_t>(1,(deadline-now()+999999)/1000000)));
  if(!receive(packet,waitMs))continue;
  if(isQmiClientResponse(packet,txn)){acked=true;break;}
 }
 streaming=false;pending.clear();timeSet=false;
 if(acked&&fd>=0){drain(15);return fd>=0&&!uid.empty();}
 closeRaw();return false;
}
void SscSensor::close(bool quiesce){
 if(fd>=0)drain(0);
 if(quiesce&&fd>=0&&streaming&&!uid.empty())disable(250);
 if(fd>=0)drain(0);
 closeRaw();
}
bool SscSensor::receive(Bytes& b,int timeoutMs,int wakeFd){
 if(fd<0)return false;
 int64_t deadline=timeoutMs>0?now()+int64_t(timeoutMs)*1000000:0;
 while(true){
  int wait=timeoutMs<=0?timeoutMs:int(std::max<int64_t>(1,(deadline-now()+999999)/1000000));
  pollfd pfds[2]={{fd,POLLIN,0},{wakeFd,POLLIN,0}};
  nfds_t nfds=wakeFd>=0?2:1;
  int pr=poll(pfds,nfds,wait);
  if(pr<0&&errno==EINTR){if(timeoutMs>0&&now()>=deadline)return false;continue;}
  if(pr<=0)return false;
  if(wakeFd>=0&&(pfds[1].revents&(POLLIN|POLLERR|POLLHUP)))return false;
  if(pfds[0].revents&(POLLERR|POLLHUP|POLLNVAL)){closeRaw();return false;}
  if(!(pfds[0].revents&POLLIN))return false;
  break;
 }
 uint8_t buf[65536];sockaddr_qrtr from{};socklen_t len=sizeof(from);
 ssize_t n=recvfrom(fd,buf,sizeof(buf),MSG_TRUNC,reinterpret_cast<sockaddr*>(&from),&len);
 if(n<=0||n>ssize_t(sizeof(buf))||len!=sizeof(from))return false;
 Bytes pkt(buf,buf+n);
 if(port!=QRTR_PORT_CTRL&&from.sq_node==localNode&&from.sq_port==QRTR_PORT_CTRL){
  if(isPeerDownCtrl(pkt,node,port))closeRaw();
  return false;
 }
 if(from.sq_node!=node||from.sq_port!=port)return false;
 b=std::move(pkt);return true;
}
bool SscSensor::send(uint32_t msg,const Bytes&data,uint8_t deliveryType,uint32_t batchPeriodUs){
 Bytes packet;
 if(fd<0||!buildRequestPacket(uid,msg,data,deliveryType,++transaction,packet,batchPeriodUs))return false;
 sockaddr_qrtr dst{AF_QIPCRTR,node,port};
 return sendto(fd,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&dst),sizeof(dst))==ssize_t(packet.size());
}
SscProbeResult SscSensor::openInternal(int timeoutMs){
 close();
 Bytes query;
 if(timeoutMs<=0||!buildSuidQuery(type,query))return SscProbeResult::Unavailable;
 fd=socket(AF_QIPCRTR,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return SscProbeResult::Unavailable;
 sockaddr_qrtr local{AF_QIPCRTR,0,0};socklen_t len=sizeof(local);
 // QRTR bind requires the kernel-assigned local node, not node zero.
 if(getsockname(fd,reinterpret_cast<sockaddr*>(&local),&len)||bind(fd,reinterpret_cast<sockaddr*>(&local),len)){closeRaw();return SscProbeResult::Unavailable;}
 localNode=local.sq_node;node=localNode;port=QRTR_PORT_CTRL;
 sockaddr_qrtr control{AF_QIPCRTR,node,port};
 qrtr_ctrl_pkt lookup{};lookup.cmd=QRTR_TYPE_NEW_LOOKUP;lookup.server.service=400;
 if(sendto(fd,&lookup,sizeof(lookup),0,reinterpret_cast<sockaddr*>(&control),sizeof(control))!=sizeof(lookup)){closeRaw();return SscProbeResult::Unavailable;}
 Bytes packet;bool found=false;int64_t deadline=now()+int64_t(timeoutMs)*1000000;
 while(now()<deadline){
  int waitMs=int(std::min<int64_t>(200,std::max<int64_t>(1,(deadline-now()+999999)/1000000)));
  if(!receive(packet,waitMs))continue;
  if(packet.size()>=sizeof(qrtr_ctrl_pkt)){
   qrtr_ctrl_pkt p{};std::memcpy(&p,packet.data(),sizeof(p));
   if(p.cmd==QRTR_TYPE_NEW_SERVER&&p.server.service==400&&p.server.port){node=p.server.node;port=p.server.port;found=true;break;}
  }
 }
 if(!found){closeRaw();return SscProbeResult::Unavailable;}
 uid=suidServiceUid();
 if(!send(kSscMsgSuidReq,query,kSscDeliveryWakeup)){closeRaw();return SscProbeResult::Unavailable;}
 deadline=now()+int64_t(timeoutMs)*1000000;
 while(now()<deadline){
  int waitMs=int(std::min<int64_t>(200,std::max<int64_t>(1,(deadline-now()+999999)/1000000)));
  if(!receive(packet,waitMs))continue;
  Bytes resolved;
  SscProbeResult r=parseSuidResponse(packet,uid,type,resolved);
  if(r==SscProbeResult::Found){uid=std::move(resolved);streaming=false;drain(0);return SscProbeResult::Found;}
  if(r==SscProbeResult::Missing){closeRaw();return SscProbeResult::Missing;}
 }
 closeRaw();return SscProbeResult::Unavailable;
}
bool SscSensor::open(int timeoutMs){return openInternal(timeoutMs)==SscProbeResult::Found;}
SscProbeResult SscSensor::probe(int timeoutMs){SscProbeResult r=openInternal(timeoutMs);close();return r;}
bool SscSensor::configure(float hz,uint32_t batchPeriodUs){
 Bytes payload;
 if(fd<0||!buildRatePayload(hz,payload))return false;
 if(!send(kSscMsgStdSensorConfig,payload,kSscDeliveryNoWakeup,batchPeriodUs))return false;
 streaming=true;return true;
}
bool SscSensor::rate(float hz){return configure(hz,0);}
bool SscSensor::flush(){
 if(fd<0||!streaming||uid.empty())return false;
 return send(kSscMsgStdFlushReq,{},kSscDeliveryWakeup,0);
}
int SscSensor::read(SscSample&out,int timeoutMs){
 if(!pending.empty()){out=pending.front();pending.erase(pending.begin());return 1;}
 Bytes packet;if(fd<0||!receive(packet,timeoutMs))return fd<0?-1:0;
 if(!parseSamplePacket(packet,uid,pending,timeOffset,timeSet,now()))return 0;
 out=pending.front();pending.erase(pending.begin());return 1;
}
int SscSensor::readBatch(std::vector<SscSample>&out,int timeoutMs,int wakeFd){
 out.clear();
 if(!pending.empty()){out.swap(pending);return int(out.size());}
 Bytes packet;if(fd<0||!receive(packet,timeoutMs,wakeFd))return fd<0?-1:0;
 if(!parseSamplePacket(packet,uid,out,timeOffset,timeSet,now()))return 0;
 return int(out.size());
}
}
