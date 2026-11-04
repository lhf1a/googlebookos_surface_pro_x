#include "../probe/lte/sq2-radio-qmi.h"
#include <cassert>
#include <functional>
using namespace sq2;
static void rejects(const std::function<void()>& f) {
    bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="selftest") {
            Bytes b{2,1,0,0x24,0,7,0,2,4,0,0,0,0,0};
            assert(decode(b,1,0x24).error==0);
            rejects([&]{decode(b,2,0x24);});
            for(size_t n=0;n<b.size();n++)rejects([&]{decode(Bytes(b.begin(),b.begin()+n),1,0x24);});
            auto dup=b;dup.insert(dup.end(),b.begin()+7,b.end());dup[5]=14;rejects([&]{decode(dup,1,0x24);});
            assert(hex(unhex("3f007fff"))=="3F007FFF");rejects([]{unhex("X0");});rejects([]{unhex("1");});
            assert(bcd({0x98,0x10,0x32,0xf4})=="8901234");
            assert(bcd({8,0x19,0x32,0x54,0x76,0x98,0x10,0x32,0x54},true)=="123456789012345");
            Bytes card{0,0,255,255,255,255,255,255,1,1,3,3,10,0,1,2,7,2,0,3,10,3,0xa0,0,0,0,3,3,10,1,3,10};
            auto cs=cards(card);assert(cs.size()==1 && cs[0].apps[0].state==7);
            for(size_t n=0;n<card.size();n++)rejects([&]{cards(Bytes(card.begin(),card.begin()+n));});
            Response r;r.fields[1]={1,2,1,2,1,8};r.fields[0x12]={0xcc,1,0,0,0};r.fields[0x27]={0xcc,1,0,0,0};r.fields[0x10]={1};
            auto s=serving(r);assert(s.mcc=="460" && s.mnc=="00" && s.lte && !s.roaming);
            r.fields[0x27][4]=1;assert(serving(r).mnc=="000");
            r.fields[0x14]={uint8_t(-65),uint8_t(-10),uint8_t(-95),255,120,0};
            auto sig=signal(r);assert(sig.rsrp==-95 && sig.rsrq==-10 && sig.snr==120);
            assert(hex(fileRequest(0x6f07,"3F007FFF",""))=="0102000000020700076F04003FFF7F");
            assert(operatorText(unhex("2D4EFD567F5E3575")).empty());
            assert(operatorText(unhex("E4B8ADE59BBDE5B9BFE794B5"))=="中国广电");
            for(auto bad:{"00","C080","EDA080","F4908080","E4B8","FF"})assert(operatorText(unhex(bad)).empty());
            puts("PASS QMI bounds, duplicate TLVs, SIM arrays/BCD, PLMN width, LTE signal and file request");return 0;
        }
        return 2;
    }catch(const std::exception& e){fprintf(stderr,"query: %s\n",e.what());return 1;}
}
