// One real USIM and LTE IPv4 bearer. Android owns subscriptions, APNs and policy.
#include "sq2-radio-qmi.h"
#include <aidl/android/hardware/radio/RadioResponseInfo.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <signal.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <queue>
#include <thread>
using namespace aidl::android::hardware::radio;
using Status=ndk::ScopedAStatus;
static Status ok() { return Status::ok(); }
static RadioResponseInfo info(int serial,RadioError error=RadioError::NONE) {
    RadioResponseInfo r;r.serial=serial;r.type=RadioResponseType::SOLICITED;r.error=error;return r;
}
#include <sq2-radio-defaults.h>
namespace cfg=aidl::android::hardware::radio::config;
namespace sim=aidl::android::hardware::radio::sim;
namespace net=aidl::android::hardware::radio::network;
namespace mdm=aidl::android::hardware::radio::modem;
namespace data=aidl::android::hardware::radio::data;
constexpr auto UNSOL=RadioIndicationType::UNSOLICITED;
constexpr int LTE_RAF=1<<14;
static void log(const char* message) { __android_log_print(ANDROID_LOG_INFO,"SQ2Radio","%s",message); }
template<class T,class F> static T query(F f,RadioError& error) {
    try { return f(); } catch(const std::exception& e) { log(e.what());error=RadioError::RADIO_NOT_AVAILABLE;return {}; }
}
class Queue {
    std::mutex mutex;std::condition_variable condition;std::queue<std::function<void()>> jobs;
public:
    Queue() { std::thread([this]{for(;;){std::function<void()> job;{std::unique_lock<std::mutex> l(mutex);condition.wait(l,[this]{return !jobs.empty();});job=std::move(jobs.front());jobs.pop();}job();}}).detach(); }
    void post(std::function<void()> f) { std::lock_guard<std::mutex> l(mutex);jobs.push(std::move(f));condition.notify_one(); }
};
static int command(const std::vector<std::string>& args) {
    std::vector<char*> v;for(auto& s:args)v.push_back(const_cast<char*>(s.c_str()));v.push_back(nullptr);
    pid_t pid=fork();if(pid<0)return -1;
    if(!pid){int fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,1);dup2(fd,2);close(fd);}execv(v[0],v.data());_exit(127);}
    int status;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)return -1;}return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
static std::string ascii(const sq2::Bytes& b) {
    if(std::any_of(b.begin(),b.end(),[](auto c){return c<32 || c>126;}))throw std::runtime_error("DMS string");
    return {b.begin(),b.end()};
}
class Core {
public:
    std::map<unsigned,unsigned> ports;
    sq2::Client uim,nas,dms;
    Queue changes;
    std::mutex bearerMutex;
    pid_t bearer=-1;int output=-1;std::string address,apn;
    data::SetupDataCallResult call;
    std::function<void()> dataChanged;
    bool dataAllowed=true;
    std::atomic<bool> networkDirty{true};
    explicit Core():ports(sq2::discover()),uim(ports.at(11)),nas(ports.at(3)),dms(ports.at(2)) {}
    sim::CardStatus card() {
        auto reply=uim.call(0x2f);if(reply.error)throw std::runtime_error("UIM card status");
        auto cards=sq2::cards(reply.get(0x10));if(cards.size()!=1)throw std::runtime_error("single physical SIM required");
        sim::CardStatus s;s.cardState=cards[0].state;s.universalPinState=sim::PinState(cards[0].upin);
        s.gsmUmtsSubscriptionAppIndex=s.cdmaSubscriptionAppIndex=s.imsSubscriptionAppIndex=-1;
        s.slotMap.physicalSlotId=0;s.slotMap.portId=0;
        for(auto& a:cards[0].apps) {
            sim::AppStatus v;v.appType=a.type;
            static const int states[]={0,1,2,3,4,3,0,5};v.appState=a.state<8?states[a.state]:0;
            v.persoSubstate=a.state==7?sim::PersoSubstate::READY:sim::PersoSubstate::UNKNOWN;
            v.aidPtr=sq2::hex(a.aid);v.pin1Replaced=a.upin;v.pin1=sim::PinState(a.pin1);v.pin2=sim::PinState(a.pin2);
            if(a.type==2)s.gsmUmtsSubscriptionAppIndex=s.applications.size();
            s.applications.push_back(v);
        }
        if(s.cardState==1) {
            auto io=sq2::fileIo(uim,0xb0,0x2fe2,"3F00","");
            if(!io.success)throw std::runtime_error("ICCID unavailable");s.iccid=sq2::bcd(io.data);
        }
        return s;
    }
    std::string activeAid() { auto s=card();if(s.gsmUmtsSubscriptionAppIndex<0)throw std::runtime_error("USIM absent");return s.applications[s.gsmUmtsSubscriptionAppIndex].aidPtr; }
    sim::IccIoResult io(const sim::IccIo& request) {
        if(!request.aid.empty() && request.aid!=activeAid())throw std::runtime_error("SIM AID mismatch");
        auto result=sq2::fileIo(uim,request.command,request.fileId,request.path,request.aid,request.p1,request.p2,request.p3);
        sim::IccIoResult out;out.sw1=result.sw1;out.sw2=result.sw2;out.simResponse=sq2::hex(result.data);return out;
    }
    int mode() { auto r=dms.call(0x2d);if(r.error||r.get(1).size()!=1)throw std::runtime_error("DMS mode query");return r.get(1)[0]; }
    mdm::RadioState state() { int m=mode();return m==0?mdm::RadioState::ON:m==1?mdm::RadioState::OFF:mdm::RadioState::UNAVAILABLE; }
    void power(bool on) {
        if(!on)stop();
        // Existing helper restricts modes to 0/1 and verifies asynchronous completion.
        if(command({"/vendor/bin/sq2-qmi-mode","0",std::to_string(ports.at(2)),on?"online":"low-power"}))throw std::runtime_error("DMS power change failed");
        networkDirty=true;
    }
    net::RegStateResult registration(bool voice=false) {
        auto s=sq2::serving(nas.call(0x24));net::RegStateResult r;
        // This adapter implements no circuit-switched or IMS calling.
        unsigned state=voice?0:s.registration;
        if(state==1 && s.ps!=1)state=0;
        if(state==1 && s.roaming)state=5;
        r.regState=net::RegState(state);r.rat=s.lte?RadioTechnology::LTE:RadioTechnology::UNKNOWN;
        net::CellIdentityLte cell;cell.mcc=s.mcc;cell.mnc=s.mnc;cell.ci=s.ci;cell.tac=s.tac;
        cell.pci=cell.earfcn=cell.bandwidth=INT_MAX;
        cell.operatorNames.alphaLong=cell.operatorNames.alphaShort=s.name;cell.operatorNames.operatorNumeric=s.mcc+s.mnc;
        r.cellIdentity.set<net::CellIdentity::Tag::lte>(cell);r.registeredPlmn=s.mcc+s.mnc;
        net::EutranRegistrationInfo eps;eps.lteAttachResultType=s.ps==1?net::EutranRegistrationInfo::AttachResultType::EPS_ONLY:net::EutranRegistrationInfo::AttachResultType::NONE;
        r.accessTechnologySpecificInfo.set<net::AccessTechnologySpecificInfo::Tag::eutranInfo>(eps);return r;
    }
    net::SignalStrength strength() {
        auto s=sq2::signal(nas.call(0x4f));net::SignalStrength v;
        v.gsm.signalStrength=99;v.gsm.bitErrorRate=99;v.gsm.timingAdvance=INT_MAX;
        v.cdma.dbm=v.cdma.ecio=v.evdo.dbm=v.evdo.ecio=v.evdo.signalNoiseRatio=INT_MAX;
        v.wcdma.signalStrength=v.wcdma.bitErrorRate=99;v.wcdma.rscp=v.wcdma.ecno=INT_MAX;
        v.tdscdma.signalStrength=v.tdscdma.bitErrorRate=99;v.tdscdma.rscp=INT_MAX;
        v.nr.ssRsrp=v.nr.ssRsrq=v.nr.ssSinr=v.nr.csiRsrp=v.nr.csiRsrq=v.nr.csiSinr=v.nr.csiCqiTableIndex=INT_MAX;
        v.lte.signalStrength=s.rssi>=-113&&s.rssi<=-51?(s.rssi+113)/2:99;
        v.lte.rsrp=s.rsrp==INT_MAX?INT_MAX:-s.rsrp;v.lte.rsrq=s.rsrq==INT_MAX?INT_MAX:-s.rsrq;
        v.lte.rssnr=s.snr;v.lte.cqi=v.lte.timingAdvance=v.lte.cqiTableIndex=INT_MAX;return v;
    }
    std::vector<data::SetupDataCallResult> calls() { std::lock_guard<std::mutex> l(bearerMutex);return call.active==2?std::vector<data::SetupDataCallResult>{call}:std::vector<data::SetupDataCallResult>{}; }
    static bool ipv4(const std::string& s) { in_addr a;return inet_pton(AF_INET,s.c_str(),&a)==1 && a.s_addr!=0; }
    void stop() {
        std::lock_guard<std::mutex> l(bearerMutex);
        if(bearer>0) {
            kill(bearer,SIGTERM);auto end=sq2::millis()+35000;int status=0;
            while(waitpid(bearer,&status,WNOHANG)==0 && sq2::millis()<end)usleep(100000);
            if(waitpid(bearer,&status,WNOHANG)==0){kill(bearer,SIGKILL);waitpid(bearer,&status,0);}bearer=-1;
        }
        if(output>=0){close(output);output=-1;}
        if(!address.empty())command({"/system/bin/ip","addr","del",address+"/32","dev","rmnet_data0"});
        address.clear();apn.clear();call={};
    }
    data::SetupDataCallResult start(const data::DataProfileInfo& profile,bool roamingAllowed) {
        auto serving=sq2::serving(nas.call(0x24));
        if(serving.roaming && !roamingAllowed)throw std::runtime_error("roaming prohibited");
        if(!dataAllowed || mode()!=0)throw std::runtime_error("data disabled");
        if(profile.apn.empty() || profile.apn.size()>100 || profile.apn.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-")!=std::string::npos)
            throw std::runtime_error("invalid APN");
        if(profile.authType!=data::ApnAuthType::NO_PAP_NO_CHAP || !profile.user.empty() || !profile.password.empty())throw std::runtime_error("APN authentication unsupported");
        if(profile.protocol!=data::PdpProtocolType::IP && profile.protocol!=data::PdpProtocolType::IPV4V6)throw std::runtime_error("IPv4 required");
        if(access("/dev/sq2-lte/radio-data-ready",F_OK))throw std::runtime_error("IPA preparation pending");
        {std::lock_guard<std::mutex> l(bearerMutex);if(call.active==2){if(apn==profile.apn)return call;throw std::runtime_error("one bearer already active");}}
        stop();std::lock_guard<std::mutex> l(bearerMutex);
        int fds[2];if(pipe2(fds,O_CLOEXEC))throw std::runtime_error("bearer pipe");
        std::string dpm=std::to_string(ports.at(47)),wda=std::to_string(ports.at(26)),wds=std::to_string(ports.at(1));
        bearer=fork();
        if(!bearer) {
            dup2(fds[1],1);dup2(fds[1],2);close(fds[0]);close(fds[1]);
            execl("/vendor/bin/sq2-qmi-data","sq2-qmi-data",dpm.c_str(),wda.c_str(),wds.c_str(),profile.apn.c_str(),nullptr);_exit(127);
        }
        close(fds[1]);output=fds[0];if(bearer<0)throw std::runtime_error("bearer fork");
        std::map<std::string,std::string> values;std::string pending;auto end=sq2::millis()+60000;bool ready=false;
        while(sq2::millis()<end && !ready) {
            pollfd fd{output,POLLIN,0};if(poll(&fd,1,1000)<0){if(errno==EINTR)continue;break;}if(!(fd.revents&POLLIN))continue;
            char buffer[1024];ssize_t n=read(output,buffer,sizeof(buffer));if(n<=0)break;pending.append(buffer,n);
            if(pending.size()>16384)throw std::runtime_error("bearer output bounds");
            size_t newline;
            while((newline=pending.find('\n'))!=std::string::npos) {
                auto line=pending.substr(0,newline);pending.erase(0,newline+1);auto eq=line.find('=');
                if(eq!=std::string::npos)values[line.substr(0,eq)]=line.substr(eq+1);
                if(line=="BEARER_READY=1")ready=true;
            }
        }
        if(!ready || !ipv4(values["IP_ADDRESS"]) || !ipv4(values["DNS1"]) || !ipv4(values["DNS2"]))throw std::runtime_error("bearer did not provide IPv4/DNS");
        // A crashed HAL's helper closes its bearer via PDEATHSIG; remove the
        // address it could not remove. The boot stage enforces one owner.
        if(command({"/system/bin/ip","addr","flush","dev","rmnet_data0","scope","global"}))throw std::runtime_error("RMNET stale address cleanup");
        address=values["IP_ADDRESS"];apn=profile.apn;
        if(command({"/system/bin/ip","addr","add",address+"/32","dev","rmnet_data0"}))throw std::runtime_error("RMNET address");
        call={};call.cid=1;call.active=2;call.type=data::PdpProtocolType::IP;call.ifname="rmnet_data0";call.mtuV4=1496;
        // AIDL uses 0 for an LTE call with no 5G PDU session ID.
        call.pduSessionId=0;call.suggestedRetryTime=-1;
        data::LinkAddress a;a.address=address+"/32";a.deprecationTime=a.expirationTime=INT64_MAX;call.addresses={a};
        call.dnses={values["DNS1"],values["DNS2"]};call.gateways={"0.0.0.0"};return call;
    }
    void checkBearer() {
        bool ended=false;
        {std::lock_guard<std::mutex> l(bearerMutex);if(bearer>0 && call.active==2){int status;ended=waitpid(bearer,&status,WNOHANG)==bearer;if(ended)bearer=-1;}}
        if(ended){stop();if(dataChanged)dataChanged();}
    }
};
static Core* core;

class RadioConfig:public RadioConfigBase {
public:
    Status getHalDeviceCapabilities(int32_t s) override { if(auto r=R())r->getHalDeviceCapabilitiesResponse(info(s),true);return ok(); }
    Status getNumOfLiveModems(int32_t s) override { if(auto r=R())r->getNumOfLiveModemsResponse(info(s),1);return ok(); }
    Status getPhoneCapability(int32_t s) override { cfg::PhoneCapability c;c.maxActiveData=c.maxActiveInternetData=1;c.maxActiveVoice=0;c.logicalModemIds={0};if(auto r=R())r->getPhoneCapabilityResponse(info(s),c);return ok(); }
    Status setPreferredDataModem(int32_t s,int8_t id) override { if(auto r=R())r->setPreferredDataModemResponse(info(s,id==0?RadioError::NONE:RadioError::INVALID_ARGUMENTS));return ok(); }
    Status setNumOfLiveModems(int32_t s,int8_t n) override { if(auto r=R())r->setNumOfLiveModemsResponse(info(s,n==1?RadioError::NONE:RadioError::REQUEST_NOT_SUPPORTED));return ok(); }
    Status setSimSlotsMapping(int32_t s,const std::vector<cfg::SlotPortMapping>& m) override {
        bool valid=m.size()==1 && m[0].physicalSlotId==0 && m[0].portId==0;
        if(auto r=R())r->setSimSlotsMappingResponse(info(s,valid?RadioError::NONE:RadioError::REQUEST_NOT_SUPPORTED));return ok();
    }
    Status getSimSlotsStatus(int32_t s) override {
        RadioError e=RadioError::NONE;auto c=query<sim::CardStatus>([]{return core->card();},e);
        cfg::SimSlotStatus slot;slot.cardState=c.cardState;slot.atr=c.atr;cfg::SimPortInfo p;p.iccId=c.iccid;p.logicalSlotId=0;p.portActive=true;slot.portInfo={p};
        if(auto r=R())r->getSimSlotsStatusResponse(info(s,e),{slot});return ok();
    }
};
class RadioSim:public RadioSimBase {
public:
    Status getIccCardStatus(int32_t s) override { RadioError e=RadioError::NONE;auto c=query<sim::CardStatus>([]{return core->card();},e);if(auto r=R())r->getIccCardStatusResponse(info(s,e),c);return ok(); }
    Status getImsiForApp(int32_t s,const std::string& aid) override {
        RadioError e=RadioError::NONE;auto value=query<std::string>([&]{
            if(!aid.empty() && aid!=core->activeAid())throw std::runtime_error("IMSI AID mismatch");
            auto io=sq2::fileIo(core->uim,0xb0,0x6f07,"3F007FFF",aid);
            if(!io.success)throw std::runtime_error("IMSI unavailable");return sq2::bcd(io.data,true);
        },e);if(auto r=R())r->getImsiForAppResponse(info(s,e),value);return ok();
    }
    Status iccIoForApp(int32_t s,const sim::IccIo& request) override {
        RadioError e=RadioError::NONE;sim::IccIoResult value;
        if(request.command!=0xb0 && request.command!=0xb2 && request.command!=0xc0)e=RadioError::REQUEST_NOT_SUPPORTED;
        else value=query<sim::IccIoResult>([&]{return core->io(request);},e);
        if(auto r=R())r->iccIoForAppResponse(info(s,e),value);return ok();
    }
    Status areUiccApplicationsEnabled(int32_t s) override { RadioError e=RadioError::NONE;auto c=query<sim::CardStatus>([]{return core->card();},e);if(auto r=R())r->areUiccApplicationsEnabledResponse(info(s,e),!c.applications.empty());return ok(); }
    Status getFacilityLockForApp(int32_t s,const std::string& facility,const std::string&,int32_t,const std::string&) override {
        RadioError e=RadioError::NONE;int locked=0;
        if(facility!="SC")e=RadioError::REQUEST_NOT_SUPPORTED;
        else {auto c=query<sim::CardStatus>([]{return core->card();},e);if(c.gsmUmtsSubscriptionAppIndex>=0)locked=c.applications[c.gsmUmtsSubscriptionAppIndex].pin1!=sim::PinState::DISABLED;}
        if(auto r=R())r->getFacilityLockForAppResponse(info(s,e),locked);return ok();
    }
    void changed() { if(auto i=I())i->simStatusChanged(UNSOL); }
};
class RadioNetwork:public RadioNetworkBase {
public:
    Status setResponseFunctions(const std::shared_ptr<net::IRadioNetworkResponse>& r,const std::shared_ptr<net::IRadioNetworkIndication>& i) override {
        RadioNetworkBase::setResponseFunctions(r,i);core->networkDirty=true;if(i)i->networkStateChanged(UNSOL);return ok();
    }
    Status getDataRegistrationState(int32_t s) override { RadioError e=RadioError::NONE;auto v=query<net::RegStateResult>([]{return core->registration();},e);if(auto r=R())r->getDataRegistrationStateResponse(info(s,e),v);return ok(); }
    Status getVoiceRegistrationState(int32_t s) override { RadioError e=RadioError::NONE;auto v=query<net::RegStateResult>([]{return core->registration(true);},e);if(auto r=R())r->getVoiceRegistrationStateResponse(info(s,e),v);return ok(); }
    Status getSignalStrength(int32_t s) override { RadioError e=RadioError::NONE;auto v=query<net::SignalStrength>([]{return core->strength();},e);if(auto r=R())r->getSignalStrengthResponse(info(s,e),v);return ok(); }
    Status getOperator(int32_t s) override { RadioError e=RadioError::NONE;auto v=query<sq2::Serving>([]{return sq2::serving(core->nas.call(0x24));},e);if(auto r=R())r->getOperatorResponse(info(s,e),v.name,v.name,v.mcc+v.mnc);return ok(); }
    Status getNetworkSelectionMode(int32_t s) override {
        RadioError e=RadioError::NONE;bool manual=query<bool>([]{auto r=core->nas.call(0x34);auto& v=r.get(0x16);if(r.error || v.size()!=1 || v[0]>1)throw std::runtime_error("network selection query");return v[0]==1;},e);
        if(auto r=R())r->getNetworkSelectionModeResponse(info(s,e),manual);return ok();
    }
    Status getAllowedNetworkTypesBitmap(int32_t s) override { if(auto r=R())r->getAllowedNetworkTypesBitmapResponse(info(s),LTE_RAF);return ok(); }
    Status getVoiceRadioTechnology(int32_t s) override { if(auto r=R())r->getVoiceRadioTechnologyResponse(info(s),RadioTechnology::LTE);return ok(); }
    Status getUsageSetting(int32_t s) override { if(auto r=R())r->getUsageSettingResponse(info(s),net::UsageSetting::DATA_CENTRIC);return ok(); }
    Status setUsageSetting(int32_t s,net::UsageSetting v) override { if(auto r=R())r->setUsageSettingResponse(info(s,v==net::UsageSetting::DATA_CENTRIC?RadioError::NONE:RadioError::REQUEST_NOT_SUPPORTED));return ok(); }
    Status setAllowedNetworkTypesBitmap(int32_t s,int32_t bitmap) override { if(auto r=R())r->setAllowedNetworkTypesBitmapResponse(info(s,(bitmap&LTE_RAF)?RadioError::NONE:RadioError::REQUEST_NOT_SUPPORTED));return ok(); }
    Status getCellInfoList(int32_t s) override { if(auto r=R())r->getCellInfoListResponse(info(s),{});return ok(); }
    Status setCellInfoListRate(int32_t s,int32_t) override { if(auto r=R())r->setCellInfoListRateResponse(info(s));return ok(); }
    Status setIndicationFilter(int32_t s,int32_t) override { if(auto r=R())r->setIndicationFilterResponse(info(s));return ok(); }
    Status setSignalStrengthReportingCriteria(int32_t s,const std::vector<net::SignalThresholdInfo>&) override { if(auto r=R())r->setSignalStrengthReportingCriteriaResponse(info(s));return ok(); }
    Status setLocationUpdates(int32_t s,bool) override { if(auto r=R())r->setLocationUpdatesResponse(info(s));return ok(); }
    void changed(const net::SignalStrength& signal,bool network,bool strength) { if(auto i=I()){if(network)i->networkStateChanged(UNSOL);if(strength)i->currentSignalStrength(UNSOL,signal);} }
};
class RadioModem:public RadioModemBase {
public:
    Status setResponseFunctions(const std::shared_ptr<mdm::IRadioModemResponse>& r,const std::shared_ptr<mdm::IRadioModemIndication>& i) override {
        RadioModemBase::setResponseFunctions(r,i);if(i){RadioError e=RadioError::NONE;auto state=query<mdm::RadioState>([]{return core->state();},e);i->rilConnected(UNSOL);i->radioStateChanged(UNSOL,e==RadioError::NONE?state:mdm::RadioState::UNAVAILABLE);}return ok();
    }
    Status getBasebandVersion(int32_t s) override { RadioError e=RadioError::NONE;auto v=query<std::string>([]{auto r=core->dms.call(0x23);if(r.error)throw std::runtime_error("baseband unavailable");return ascii(r.get(1));},e);if(auto r=R())r->getBasebandVersionResponse(info(s,e),v);return ok(); }
    Status getDeviceIdentity(int32_t s) override {
        RadioError e=RadioError::NONE;auto v=query<std::array<std::string,4>>([]{auto r=core->dms.call(0x25);if(r.error)throw std::runtime_error("device identity unavailable");return std::array<std::string,4>{ascii(r.get(0x11)),ascii(r.get(0x13)),ascii(r.get(0x10)),ascii(r.get(0x12))};},e);
        if(auto r=R())r->getDeviceIdentityResponse(info(s,e),v[0],v[1],v[2],v[3]);return ok();
    }
    Status getImei(int32_t s) override { RadioError e=RadioError::NONE;auto v=query<mdm::ImeiInfo>([]{auto r=core->dms.call(0x25);if(r.error)throw std::runtime_error("IMEI unavailable");mdm::ImeiInfo i;i.type=mdm::ImeiInfo::ImeiType::PRIMARY;i.imei=ascii(r.get(0x11));i.svn=ascii(r.get(0x13));return i;},e);if(auto r=R())r->getImeiResponse(info(s,e),v);return ok(); }
    Status getModemStackStatus(int32_t s) override { if(auto r=R())r->getModemStackStatusResponse(info(s),true);return ok(); }
    Status getRadioCapability(int32_t s) override { mdm::RadioCapability c;c.raf=LTE_RAF;c.logicalModemUuid="sq2-modem0";c.status=mdm::RadioCapability::STATUS_SUCCESS;if(auto r=R())r->getRadioCapabilityResponse(info(s),c);return ok(); }
    Status sendDeviceState(int32_t s,mdm::DeviceStateType,bool) override { if(auto r=R())r->sendDeviceStateResponse(info(s));return ok(); }
    Status setRadioPower(int32_t s,bool on,bool emergency,bool preferred) override {
        (void)preferred;
        core->changes.post([this,s,on,emergency]{RadioError e=RadioError::NONE;if(emergency)e=RadioError::REQUEST_NOT_SUPPORTED;else query<int>([&]{core->power(on);return 0;},e);if(auto r=R())r->setRadioPowerResponse(info(s,e));if(e==RadioError::NONE){if(auto i=I())i->radioStateChanged(UNSOL,on?mdm::RadioState::ON:mdm::RadioState::OFF);if(core->dataChanged)core->dataChanged();}});return ok();
    }
    void changed(mdm::RadioState state) { if(auto i=I())i->radioStateChanged(UNSOL,state); }
};
class RadioData:public RadioDataBase {
public:
    void changed() { if(auto i=I())i->dataCallListChanged(UNSOL,core->calls()); }
    Status getDataCallList(int32_t s) override { if(auto r=R())r->getDataCallListResponse(info(s),core->calls());return ok(); }
    Status setDataAllowed(int32_t s,bool allowed) override {core->changes.post([this,s,allowed]{core->dataAllowed=allowed;if(!allowed)core->stop();if(auto r=R())r->setDataAllowedResponse(info(s));changed();});return ok();}
    Status setDataProfile(int32_t s,const std::vector<data::DataProfileInfo>&) override { if(auto r=R())r->setDataProfileResponse(info(s));return ok(); }
    Status setInitialAttachApn(int32_t s,const std::optional<data::DataProfileInfo>&) override { if(auto r=R())r->setInitialAttachApnResponse(info(s));return ok(); }
    Status deactivateDataCall(int32_t s,int32_t cid,data::DataRequestReason) override {core->changes.post([this,s,cid]{if(cid==1)core->stop();if(auto r=R())r->deactivateDataCallResponse(info(s,cid==1?RadioError::NONE:RadioError::INVALID_ARGUMENTS));changed();});return ok();}
    Status setupDataCall(int32_t s,AccessNetwork access,const data::DataProfileInfo& profile,bool roamingAllowed,data::DataRequestReason reason,const std::vector<data::LinkAddress>&,const std::vector<std::string>&,int32_t,const std::optional<data::SliceInfo>&,bool) override {
        auto response=R();
        core->changes.post([this,response,s,access,profile,roamingAllowed,reason]{
            RadioError e=RadioError::NONE;data::SetupDataCallResult result;
            if(access!=AccessNetwork::EUTRAN || reason==data::DataRequestReason::HANDOVER)e=RadioError::REQUEST_NOT_SUPPORTED;
            else result=query<data::SetupDataCallResult>([&]{return core->start(profile,roamingAllowed);},e);
            bool established=e==RadioError::NONE;
            if(!established){if(core->calls().empty())core->stop();result.cause=data::DataCallFailCause::ERROR_UNSPECIFIED;result.suggestedRetryTime=10000;}
            // A transport still preparing after boot is a retryable call failure,
            // not a dead Radio service. Keep the real failure in the call result.
            if(response)response->setupDataCallResponse(info(s,e==RadioError::RADIO_NOT_AVAILABLE?RadioError::NONE:e),result);if(established)changed();
        });return ok();
    }
};
class RadioVoice:public RadioVoiceBase {
public:Status getCurrentCalls(int32_t s) override { if(auto r=R())r->getCurrentCallsResponse(info(s),{});return ok(); }
};
template<class T> static std::shared_ptr<T> service(const char* instance) {
    auto object=ndk::SharedRefBase::make<T>();std::string name=std::string(T::descriptor)+"/"+instance;
    if(AServiceManager_addService(object->asBinder().get(),name.c_str())!=STATUS_OK)throw std::runtime_error("Radio service registration failed");return object;
}
int main(int argc,char**argv) {
    if(argc==2 && std::string(argv[1])=="parcel-selftest") {
        auto object=ndk::SharedRefBase::make<RadioData>();ndk::ScopedAParcel parcel;
        int status=AIBinder_prepareTransaction(object->asBinder().get(),parcel.getR());printf("prepare=%d\n",status);if(status)return 1;
        int pos=AParcel_getDataPosition(parcel.get());
        data::SetupDataCallResult value;value.cid=1;value.active=2;value.type=data::PdpProtocolType::IP;value.ifname="rmnet_data0";value.mtuV4=1496;value.pduSessionId=0;value.suggestedRetryTime=-1;
        data::LinkAddress address;address.address="192.0.2.1/32";address.deprecationTime=address.expirationTime=INT64_MAX;value.addresses={address};value.dnses={"192.0.2.2"};value.gateways={"0.0.0.0"};
        status=value.writeToParcel(parcel.get());printf("write=%d\n",status);if(status)return 1;
        AParcel_setDataPosition(parcel.get(),pos);data::SetupDataCallResult result;status=result.readFromParcel(parcel.get());
        printf("read=%d equal=%d\n",status,result==value);return status || result!=value;
    }
    if(argc!=1){fprintf(stderr,"No debug registration or alternate instance supported\n");return 2;}(void)argv;
    try {
        signal(SIGPIPE,SIG_IGN);
        int lock=open("/dev/sq2-radio-owner",O_CREAT|O_RDWR|O_CLOEXEC,0600);
        if(lock<0 || flock(lock,LOCK_EX|LOCK_NB))throw std::runtime_error("Radio owner lock");
        core=new Core;
        ABinderProcess_setThreadPoolMaxThreadCount(8);ABinderProcess_startThreadPool();
        auto config=service<RadioConfig>("default");auto sim=service<RadioSim>("slot1");
        auto network=service<RadioNetwork>("slot1");auto modem=service<RadioModem>("slot1");
        auto data=service<RadioData>("slot1");auto voice=service<RadioVoice>("slot1");auto messaging=service<RadioMessagingBase>("slot1");
        core->dataChanged=[data]{data->changed();};
        std::thread([network,sim,modem]{
            net::RegStateResult oldReg;net::SignalStrength oldSignal;sim::CardStatus oldCard;mdm::RadioState oldState=mdm::RadioState::UNAVAILABLE;
            bool first=true;unsigned tick=0;
            for(;;){
                try {
                    auto state=core->state();if(first || state!=oldState){modem->changed(state);oldState=state;}
                }catch(const std::exception&){if(oldState!=mdm::RadioState::UNAVAILABLE){modem->changed(mdm::RadioState::UNAVAILABLE);oldState=mdm::RadioState::UNAVAILABLE;}}
                try {
                    auto reg=core->registration();auto signal=core->strength();
                    bool dirty=core->networkDirty.exchange(false);
                    network->changed(signal,first || dirty || reg!=oldReg,first || dirty || signal!=oldSignal);oldReg=reg;oldSignal=signal;
                    first=false;
                }catch(const std::exception&){/* Missing NAS signal while offline is not a modem failure. */}
                try {if(tick++%6==0){auto card=core->card();if(card!=oldCard){sim->changed();oldCard=card;}}}catch(const std::exception&){}
                core->changes.post([]{core->checkBearer();});std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }).detach();
        log("data-only Radio AIDL ready");ABinderProcess_joinThreadPool();
    }catch(const std::exception& e){log(e.what());return 1;}return 0;
}
