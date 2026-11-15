#include "../probe/sensors/spx-ssc.cpp"
#include <cassert>
int main(){
 using namespace spx;
 std::vector<Field> fields;
 assert(parse({9,1,2,3,4,5,6,7,8,0x10,3},fields));
 assert(fields.size()==2&&fields[0].value==0x0807060504030201ULL&&fields[1].value==3);
 for(Bytes b:std::vector<Bytes>{{9,1},{0},{0x0a,5,1},{0x0f},{8,128},{8,255,255,255,255,255,255,255,255,255,2}})
  assert(!parse(b,fields));
 Bytes id{9};le(id,123,8);id.push_back(17);le(id,456,8);
 Bytes sample{13};le(sample,0x3f800000,4);sample.push_back(13);le(sample,0x40000000,4);sample.push_back(13);le(sample,0x40400000,4);append(sample,{16,3});
 Bytes event{13};le(event,1025,4);event.push_back(17);le(event,19200000,8);append(event,blob(3,sample));
 Bytes body=blob(1,id);append(body,blob(2,event));
 Bytes packet{4,1,0,0x22,0};le(packet,body.size()+5,2);packet.push_back(2);le(packet,body.size()+2,2);le(packet,body.size(),2);append(packet,body);
 Bytes actual;std::vector<Field> ev;
 assert(events(packet,actual,ev)&&actual==id&&ev.size()==1);
 assert(parse(ev[0].bytes,fields)&&find(fields,1,5)->value==1025);
 packet.pop_back();ev.clear();assert(!events(packet,actual,ev));
}
