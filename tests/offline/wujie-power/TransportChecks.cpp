// Compile the real protocol against an in-memory EcAccess double. No OS adapter.
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/Performance.h"
#include <array>
#include <vector>
#include <iostream>
#include <stdexcept>

namespace mc {
class EcAccess::Impl {public:std::unique_ptr<IEcBackend> backend;std::recursive_mutex gate;explicit Impl(std::unique_ptr<IEcBackend> b):backend(std::move(b)){};};
EcAccess::EcAccess(std::unique_ptr<IEcBackend> b):impl_(std::make_unique<Impl>(std::move(b))){}
EcAccess::~EcAccess()=default;
bool EcAccess::boardVerified()const noexcept{return true;}
const BoardCheck& EcAccess::verifyBoard(){static const BoardCheck result{true};return result;}
const DeviceProfile& deviceProfile(const BoardDescriptor&) noexcept {static const DeviceProfile profile{.id="wu-jie-15-xpro"};return profile;}
int selectedCustomBase(std::string_view){return 1;}
void EcAccess::ensureCapability(DeviceFeature feature,std::string_view protocol)const{if(feature!=DeviceFeature::PowerLimits||protocol!="uniwill-x5sp4nag")throw std::runtime_error("wrong protocol");}
std::uint8_t EcAccess::readStable(std::uint16_t a){return impl_->backend->read(a);}
void EcAccess::writeVerified(std::uint16_t a,std::uint8_t v){impl_->backend->write(a,v);if(impl_->backend->read(a)!=v)throw std::runtime_error("readback mismatch");}
std::uint8_t EcAccess::updateBitsVerified(std::uint16_t a,std::uint8_t mask,std::uint8_t bits,std::uint8_t trigger){const auto value=static_cast<std::uint8_t>((readStable(a)&~mask)|(bits&mask)|trigger);writeVerified(a,value);return value;}
EcAccess::Transaction::Transaction(EcAccess& a):access_(&a),lock_(a.impl_->gate){}
EcAccess::Transaction::~Transaction()=default;
std::vector<std::uint8_t> IEcBackend::readBlock(std::uint16_t a,std::size_t n){std::vector<std::uint8_t> out;for(std::size_t i=0;i<n;++i)out.push_back(read(static_cast<std::uint16_t>(a+i)));return out;}
}
struct Memory final:mc::IEcBackend {
    std::array<std::uint8_t,65536> bytes;std::vector<std::uint16_t> writes;int failAt{-1};bool failAfter{};
    mc::BackendInfo meta{};
    Memory(){bytes.fill(0x55);bytes[mc::ec::kBoardEcVersion]=82;bytes[mc::ec::kBoardSystemId]=72;bytes[mc::ec::kBoardProjectId]=26;bytes[0x751]=0xd0;bytes[0x727]=0xc0;bytes[0x783]=65;bytes[0x784]=65;bytes[0x785]=70;}
    const mc::BackendInfo& info()const noexcept override{return meta;}
    std::uint8_t read(std::uint16_t a)override{return bytes[a];}
    void write(std::uint16_t a,std::uint8_t v)override{const bool fail=int(writes.size())==failAt;writes.push_back(a);if(fail){failAt=-1;if(failAfter)bytes[a]=v;throw std::runtime_error("injected write failure");}bytes[a]=v;}
};
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    {auto memory=std::make_unique<Memory>();auto& m=*memory;const auto before=m.bytes;mc::EcAccess ec(std::move(memory));
     mc::resetManualPowerLimits(ec);for(unsigned a=0;a<m.bytes.size();++a)check(m.bytes[a]==(a>=0x783&&a<=0x785?0:before[a]),"Auto touched751/FAN/other address");
     check(m.writes==std::vector<std::uint16_t>{0x783,0x784,0x785},"Unexpected Auto writes");mc::resetManualPowerLimits(ec);check(m.writes.size()==3,"Auto rewrote already-cleared bytes");}
    for(bool after:{false,true})for(int fail=0;fail<3;++fail){auto memory=std::make_unique<Memory>();auto& m=*memory;const auto before=m.bytes;m.failAt=fail;m.failAfter=after;mc::EcAccess ec(std::move(memory));bool failed{};try{mc::resetManualPowerLimits(ec);}catch(const std::exception&){failed=true;}check(failed&&m.bytes==before,"Failed Auto did not restore all original bytes");for(const auto a:m.writes)check(a>=0x783&&a<=0x785,"Rollback touched foreign fields");}
    {auto memory=std::make_unique<Memory>();auto& m=*memory;m.bytes[mc::ec::kBoardProjectId]=0;const auto before=m.bytes;mc::EcAccess ec(std::move(memory));bool failed{};try{mc::resetManualPowerLimits(ec);}catch(const std::exception&){failed=true;}check(failed&&m.writes.empty()&&m.bytes==before,"Wrong board accepted Auto");}
    {auto memory=std::make_unique<Memory>();auto& m=*memory;mc::EcAccess ec(std::move(memory));bool failed{};try{mc::applyManualPowerLimits(ec,{20,25,30});}catch(const std::exception&){failed=true;}check(failed&&m.writes.empty(),"Manual PL4 scale guard changed");}
    {auto memory=std::make_unique<Memory>();auto& m=*memory;m.bytes[0x727]=0x40;m.bytes[0x751]=0x10;
     m.bytes[0x46a]=10;m.bytes[0x46b]=10;m.bytes[0x46f]=20;mc::EcAccess ec(std::move(memory));
     mc::applyCustomPowerLimits(ec,{15,25,45});auto state=mc::readEcPerformance(ec);
     check(state.overrides==mc::EcPowerLimits{15,25,45}&&state.acceptedValid&&state.accepted==mc::EcPowerLimits{10,10,20},"old accepted limits were confused with requested Custom");
     m.bytes[0x46a]=15;m.bytes[0x46b]=25;m.bytes[0x46f]=45;state=mc::readEcPerformance(ec);
     check(state.acceptedValid&&state.accepted==mc::EcPowerLimits{15,25,45},"fake EC updated limits not read");
     m.bytes[0x46a]=0;check(!mc::readEcPerformance(ec).acceptedValid,"missing EC limit falsely confirmed");
     m.bytes[0x46a]=15;m.bytes[mc::ec::kBoardProjectId]=25;check(!mc::readEcPerformance(ec).acceptedValid,"foreign EC identity falsely confirmed");}
    std::cout<<"PASS real Wujie protocol + memory transport: Auto address allowlist, idempotence,6failure rollbacks, identity and legacy Manual scale guard; hardware IO=0\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
