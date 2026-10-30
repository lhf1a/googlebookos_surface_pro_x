#include "../probe/video/sq2-avc-config.h"
#include <cassert>
#include <cstdio>

int main(){
 Sq2AvcConfig c;
 const uint8_t sps[]={0,0,0,1,0x67,1},pps[]={0,0,1,0x68,2},au[]={0,0,1,0x65,3};
 assert(c.append(sps,sizeof sps)==0);
 assert(c.append(pps,sizeof pps)==0);
 std::vector<uint8_t> expected(sps,sps+sizeof sps);
 expected.insert(expected.end(),pps,pps+sizeof pps);
 expected.insert(expected.end(),au,au+sizeof au);
 int calls=0;
 auto retry=[&](const uint8_t* p,size_t n){
  ++calls;assert(std::vector<uint8_t>(p,p+n)==expected);return -EAGAIN;
 };
 assert(c.submit(au,sizeof au,retry)==-EAGAIN);
 assert(c.size()==sizeof sps+sizeof pps);
 assert(c.submit(au,sizeof au,[&](const uint8_t* p,size_t n){
  ++calls;assert(std::vector<uint8_t>(p,p+n)==expected);return 0;
 })==0);
 assert(c.size()==0&&calls==2);
 assert(c.submit(au,sizeof au,[&](const uint8_t* p,size_t n){
  assert(p==au&&n==sizeof au);return 0;
 })==0);
 std::vector<uint8_t> full(Sq2AvcConfig::Limit,7);
 assert(c.append(full.data(),full.size())==0);
 assert(c.append(sps,sizeof sps)==-EMSGSIZE);
 assert(c.submit(au,sizeof au,[](const uint8_t*,size_t){assert(false);return 0;})==-EMSGSIZE);
 assert(c.size()==full.size());c.clear();assert(c.size()==0);
 assert(c.append(nullptr,1)==-EINVAL&&c.append(sps,0)==-EINVAL);
 assert(c.submit(nullptr,1,[](const uint8_t*,size_t){assert(false);return 0;})==-EINVAL);
 puts("CSD concatenation/retry/consume/bounds/reset passed");
}
