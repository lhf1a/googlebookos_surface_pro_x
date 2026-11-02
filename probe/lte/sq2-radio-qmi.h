// Read-only SIM/NAS/DMS queries. Wire layouts: libqmi data/qmi-service-*.json.
#pragma once
#include <sys/socket.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace sq2 {
using Bytes = std::vector<uint8_t>;
inline unsigned le16(const uint8_t* p) { return p[0] | unsigned(p[1]) << 8; }
inline unsigned le32(const uint8_t* p) { return le16(p) | le16(p+2) << 16; }
inline void put16(Bytes& b, unsigned v) { b.push_back(v); b.push_back(v >> 8); }
inline void tlv(Bytes& b, uint8_t t, const Bytes& v) {
    if (v.size() > 65535) throw std::runtime_error("TLV size");
    b.push_back(t); put16(b, v.size()); b.insert(b.end(), v.begin(), v.end());
}
inline std::string hex(const Bytes& b) {
    const char* digits="0123456789ABCDEF"; std::string s;
    for (auto x:b) { s+=digits[x>>4]; s+=digits[x&15]; } return s;
}
inline Bytes unhex(const std::string& s) {
    if (s.size()%2 || s.size()>512) throw std::runtime_error("hex size");
    Bytes b; int high=-1;
    for (char c:s) {
        int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if (n<0) throw std::runtime_error("hex digit");
        if(high<0) high=n; else { b.push_back(high*16+n);high=-1; }
    } return b;
}
inline std::string bcd(const Bytes& b, bool imsi=false) {
    std::string s;
    for (size_t i=0;i<b.size();++i) {
        if(imsi && i==0) continue;
        for (int shift:{0,4}) {
            if(imsi && i==1 && shift==0) continue;
            unsigned digit=(b[i]>>shift)&15;
            if(digit==15) continue;
            if(digit>9) throw std::runtime_error("BCD digit");
            s+=char('0'+digit);
        }
    }
    if(imsi && (b.empty() || b[0]+1u>b.size() || s.size()<6 || s.size()>15)) throw std::runtime_error("IMSI size");
    return s;
}
struct Response {
    std::map<unsigned,Bytes> fields;
    unsigned error=0;
    const Bytes& get(unsigned t) const { static const Bytes empty; auto i=fields.find(t);return i==fields.end()?empty:i->second; }
};
inline Response decode(const Bytes& b, unsigned txn, unsigned msg) {
    if(b.size()<7 || b[0]!=2 || le16(b.data()+1)!=txn || le16(b.data()+3)!=msg || le16(b.data()+5)!=b.size()-7)
        throw std::runtime_error("QMI envelope");
    Response r;
    for(size_t p=7;p<b.size();) {
        if(b.size()-p<3) throw std::runtime_error("TLV header");
        unsigned t=b[p],n=le16(b.data()+p+1);p+=3;
        if(n>b.size()-p || r.fields.count(t)) throw std::runtime_error("TLV bounds/duplicate");
        r.fields[t]=Bytes(b.begin()+p,b.begin()+p+n);p+=n;
    }
    auto& result=r.get(2);
    if(result.size()!=4) throw std::runtime_error("QMI result");
    r.error=le16(result.data()+2);
    if(le16(result.data()) && !r.error) throw std::runtime_error("QMI inconsistent result");
    return r;
}
inline int64_t millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::map<unsigned,unsigned> discover() {
    std::map<unsigned,unsigned> ports;
    FILE* f=popen("/vendor/bin/qrtr-lookup", "r");
    if(!f) throw std::runtime_error("QRTR lookup");
    char line[512];
    while(fgets(line,sizeof(line),f)) {
        unsigned service,version,instance,node,port;
        if(sscanf(line,"%u %u %u %u %u",&service,&version,&instance,&node,&port)==5 && node==0) {
            if(service!=1 && service!=2 && service!=3 && service!=11 && service!=26 && service!=47)continue;
            if(ports.count(service) && ports[service]!=port) { pclose(f);throw std::runtime_error("ambiguous QRTR service"); }
            ports[service]=port;
        }
    }
    if(pclose(f)) throw std::runtime_error("QRTR lookup failed");
    return ports;
}
class Client {
    int fd=-1;
    unsigned txn=0;
    std::mutex mutex;
public:
    explicit Client(unsigned port) {
        fd=socket(AF_QIPCRTR,SOCK_DGRAM|SOCK_CLOEXEC,0);
        sockaddr_qrtr addr{};addr.sq_family=AF_QIPCRTR;addr.sq_node=0;addr.sq_port=port;
        if(fd<0 || connect(fd,(sockaddr*)&addr,sizeof(addr))) { if(fd>=0)close(fd);throw std::runtime_error("QRTR connect"); }
    }
    ~Client() { if(fd>=0)close(fd); }
    Response call(unsigned msg, const Bytes& payload={}) {
        std::lock_guard<std::mutex> lock(mutex);
        txn=txn%65535+1;
        Bytes q{0};put16(q,txn);put16(q,msg);put16(q,payload.size());q.insert(q.end(),payload.begin(),payload.end());
        if(send(fd,q.data(),q.size(),0)!=ssize_t(q.size())) throw std::runtime_error("QMI send");
        auto deadline=millis()+5000;
        while(millis()<deadline) {
            pollfd f{fd,POLLIN,0};int rc=poll(&f,1,std::max<int64_t>(1,deadline-millis()));
            if(rc<0 && errno==EINTR) continue;
            if(rc<=0) break;
            Bytes b(16384);ssize_t n=recv(fd,b.data(),b.size(),MSG_TRUNC);
            if(n<7 || n>ssize_t(b.size())) throw std::runtime_error("QMI receive");
            b.resize(n);
            if(b[0]!=2 || le16(b.data()+1)!=txn || le16(b.data()+3)!=msg) continue;
            return decode(b,txn,msg);
        }
        throw std::runtime_error("QMI timeout");
    }
};
struct Cursor {
    const Bytes& b;size_t p=0;
    unsigned u8() { if(p==b.size())throw std::runtime_error("UIM truncated");return b[p++]; }
    unsigned u16() { auto lo=u8();return lo|(u8()<<8); }
    Bytes bytes(unsigned n) { if(n>b.size()-p)throw std::runtime_error("UIM array");Bytes v(b.begin()+p,b.begin()+p+n);p+=n;return v; }
};
struct App { unsigned type=0,state=0,perso=0,feature=0,upin=0,pin1=0,pin2=0;Bytes aid; };
struct Card { unsigned state=0,upin=0;std::vector<App> apps; };
inline std::vector<Card> cards(const Bytes& b) {
    Cursor c{b};for(int i=0;i<4;i++) c.u16();unsigned count=c.u8();
    if(count>5) throw std::runtime_error("UIM card count");
    std::vector<Card> out;
    while(count--) {
        Card card;card.state=c.u8();card.upin=c.u8();c.u8();c.u8();c.u8();unsigned apps=c.u8();
        if(apps>16) throw std::runtime_error("UIM app count");
        while(apps--) {
            App a;a.type=c.u8();a.state=c.u8();a.perso=c.u8();a.feature=c.u8();c.u8();c.u8();
            a.aid=c.bytes(c.u8());a.upin=c.u8();a.pin1=c.u8();c.u8();c.u8();a.pin2=c.u8();c.u8();c.u8();card.apps.push_back(a);
        } out.push_back(card);
    }
    if(c.p!=b.size()) throw std::runtime_error("UIM trailing bytes");
    return out;
}
struct Serving {
    unsigned registration=4,ps=0,cs=0;bool lte=false,roaming=false;
    std::string mcc,mnc,name;
    int tac=INT_MAX,ci=INT_MAX;
};
inline std::string operatorText(const Bytes& b) {
    // NAS descriptions on this modem may contain unlabelled UCS-2 bytes.
    // Only pass valid printable UTF-8; Android can use the real SIM SPN/PLMN.
    for(size_t p=0;p<b.size();) {
        unsigned c=b[p++],code=c,count=0,minimum=0;
        if(c<0x20 || c==0x7f)return {};
        if(c<0x80)continue;
        if(c>=0xc2 && c<=0xdf){code=c&31;count=1;minimum=0x80;}
        else if(c>=0xe0 && c<=0xef){code=c&15;count=2;minimum=0x800;}
        else if(c>=0xf0 && c<=0xf4){code=c&7;count=3;minimum=0x10000;}
        else return {};
        if(count>b.size()-p)return {};
        while(count--){unsigned next=b[p++];if((next&0xc0)!=0x80)return {};code=(code<<6)|(next&63);}
        if(code<minimum || code>0x10ffff || (code>=0xd800 && code<=0xdfff))return {};
    }
    return {b.begin(),b.end()};
}
inline Serving serving(const Response& r) {
    if(r.error) throw std::runtime_error("NAS serving error");
    Serving s;auto& a=r.get(1);
    if(a.size()<5 || a.size()!=5u+a[4]) throw std::runtime_error("NAS serving length");
    s.registration=a[0];s.cs=a[1];s.ps=a[2];s.lte=std::find(a.begin()+5,a.end(),8)!=a.end();
    auto& roam=r.get(0x10);if(roam.size()==1)s.roaming=roam[0]==0;
    auto& p=r.get(0x12);
    if(!p.empty()) {
        if(p.size()<5 || p.size()!=5u+p[4] || le16(p.data())>999 || le16(p.data()+2)>999)throw std::runtime_error("NAS PLMN");
        auto& pcs=r.get(0x27);bool three=le16(p.data()+2)>99;
        if(pcs.size()==5 && le16(pcs.data())==le16(p.data()) && le16(pcs.data()+2)==le16(p.data()+2))three=pcs[4];
        char text[8];snprintf(text,sizeof(text),"%03u",le16(p.data()));s.mcc=text;
        snprintf(text,sizeof(text),three?"%03u":"%02u",le16(p.data()+2));s.mnc=text;
        s.name=operatorText(Bytes(p.begin()+5,p.end()));
    }
    auto& tac=r.get(0x24);if(tac.size()==2)s.tac=le16(tac.data());
    auto& ci=r.get(0x1d);if(ci.size()==4 && le32(ci.data())<=0xfffffff)s.ci=le32(ci.data());
    return s;
}
struct Signal { int rssi=INT_MAX,rsrq=INT_MAX,rsrp=INT_MAX,snr=INT_MAX; };
inline Signal signal(const Response& r) {
    if(r.error)throw std::runtime_error("NAS signal error");
    Signal s;auto& b=r.get(0x14);
    if(b.empty())return s;
    if(b.size()!=6)throw std::runtime_error("NAS LTE signal length");
    s.rssi=int8_t(b[0]);s.rsrq=int8_t(b[1]);s.rsrp=int16_t(le16(b.data()+2));s.snr=int16_t(le16(b.data()+4));
    if(s.rsrp < -140 || s.rsrp > -44)s.rsrp=INT_MAX;
    if(s.rsrq < -34 || s.rsrq > 3)s.rsrq=INT_MAX;
    if(s.snr < -200 || s.snr>300)s.snr=INT_MAX;
    return s;
}
inline Bytes fileRequest(unsigned file, const std::string& path, const std::string& aid) {
    Bytes q,session,route=unhex(path),f;
    if(route.size()%2 || route.empty() || route.size()>20 || file>65535)throw std::runtime_error("UIM path");
    // MF/ICCID uses the physical card session; USIM files use the GW primary.
    if(route.size()==2)session={6,0}; else session={0,0};
    // Provisioning sessions select the modem's active app; reject a mismatched
    // AID at the HAL boundary, rather than opening another logical channel.
    (void)aid;
    tlv(q,1,session);put16(f,file);f.push_back(route.size());
    for(size_t i=0;i<route.size();i+=2){f.push_back(route[i+1]);f.push_back(route[i]);}
    tlv(q,2,f);return q;
}
struct Io { unsigned sw1=0x6f,sw2=0;Bytes data;bool success=false; };
inline Io fileIo(Client& uim, unsigned command, unsigned file, const std::string& path,
                 const std::string& aid, unsigned p1=0,unsigned p2=0,unsigned length=0) {
    auto q=fileRequest(file,path,aid);unsigned msg;
    if(command==0xc0)msg=0x24;
    else if(command==0xb0 || command==0xb2) {
        msg=command==0xb0?0x20:0x21;Bytes read;
        if(p1>255||p2>255||length>256 || (command==0xb2 && (p2!=4 || !p1)))throw std::runtime_error("UIM read range");
        put16(read,command==0xb0?(p1<<8|p2):p1);put16(read,length);tlv(q,3,read);
    } else throw std::runtime_error("UIM command unsupported");
    auto r=uim.call(msg,q);Io io;auto& sw=r.get(0x10);
    if(sw.size()==2){io.sw1=sw[0];io.sw2=sw[1];}
    if(r.error)return io;
    auto& data=r.get(0x11);
    if(command==0xc0) {
        if(data.size()<26 || le16(data.data()+2)!=file || le16(data.data()+24)!=data.size()-26)throw std::runtime_error("UIM attributes");
        // IccFileHandler expects the classic 15-byte response even for USIM.
        // Populate it from actual modem attributes, never from invented EF sizes.
        io.data=Bytes(15);unsigned size=le16(data.data()),record=le16(data.data()+5);
        io.data[2]=size>>8;io.data[3]=size;io.data[4]=file>>8;io.data[5]=file;io.data[6]=4;
        io.data[11]=1;io.data[12]=2;io.data[13]=data[4]==0?0:data[4]==1?1:data[4]==2?3:255;
        if(io.data[13]==255 || record>255)throw std::runtime_error("UIM file type/record size");
        io.data[14]=record;
    } else {
        if(data.size()<2 || le16(data.data())!=data.size()-2)throw std::runtime_error("UIM read length");
        io.data=Bytes(data.begin()+2,data.end());
    }
    io.success=true;return io;
}
} // namespace sq2
