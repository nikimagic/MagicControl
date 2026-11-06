#include "NativeDefaultFan.h"
#include "DefaultFanData.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include <chrono>
#include <utility>

namespace mc::gm6 {
namespace {
constexpr std::array<std::uint16_t,18> contextAddresses{
    0x454,0x740,0x74c,0x7ca,0x6e6,0x466,0x4fe,0x6c2,0x751,
    0x759,0x75a,0x768,0x741,0x7c6,0x7a4,0x7c7,0x7c8,0x49f};
constexpr std::array<std::uint8_t,18> contextMasks{
    255,255,0x3f,1,255,0x10,8,255,0xd0,0x80,0x80,4,1,4,0x80,2,0x20,2};
std::optional<std::array<std::uint8_t,18>> context(EcAccess& ec){
    std::array<std::uint8_t,18> result{};
    for(std::size_t i=0;i<result.size();++i){
        const auto value=ec.readStable(contextAddresses[i]);
        if(value==0xff)return {}; // an unreadable host window is not a state
        result[i]=value&contextMasks[i];
    }
    return result;
}
std::optional<double> temperature(std::uint8_t value){
    return value>0&&value<128?std::optional<double>{value}:std::nullopt;
}
}

NativeDefaultFanReading decodeFirmwareDefaultFans(std::string_view profile,const DefaultFanContext& c,std::uint64_t at){
    using O=NativeDefaultFanObservation;
    if(profile!="mechrevo-gm6ir0c")return {O::Inactive,{}};
    const auto& v=c.context;
    // EC 1.12, exact Project25 and SKU18 from the current selector, not preview.
    if(v[0]!=0x4c||v[1]!=25||v[2]!=0x18||v[3]!=1)return {O::Inactive,{}};
    const auto mode=v[8]&0x90;
    if(v[4]!=1||v[5]||v[6]||v[7]||v[9]||v[10]||v[11]||
       (v[8]&0x40)||(v[12]&&v[13])||mode==0x90)return {O::Inactive,{}};
    // Alternate liquid-cooling pointer group is not inferred for this board.
    if(v[14]&&v[15]&&v[16])return {O::Inactive,{}};
    for(const auto value:c.selectors)if(value==0xff)return {O::Unavailable,{}};
    const auto group=static_cast<std::uint16_t>((unsigned(c.selectors[0])<<8)|c.selectors[1]);
    if((group!=0x5d48&&group!=0x5d60)||group!=(c.selectors[2]==0x15?0x5d60:0x5d48))
        return {O::Unavailable,{}};
    if(c.pwm[0]>200||c.pwm[1]>200||!at)return {O::Unavailable,{}};
    // Firmware chooses Office on 751.7, Turbo only when 49F.1 and 751.4
    // both hold, otherwise Gaming. This is the selected base table, not PL.
    const int modeIndex=(mode&0x80)?1:mode==0x10&&v[17]?2:0;
    const int variant=group==0x5d60?1:0;
    NativeDefaultFans out;
    for(int ch=0;ch<2;++ch){
        const auto& table=firmwareFanTables[variant*6+modeIndex*2+ch];
        auto& curve=out.channels[ch];
        curve.profile=modeIndex==1?"Office":modeIndex==2?"Turbo":"Gaming";
        curve.temperatureSource=ch?"EC GPU":"EC CPU";
        curve.temperatureAddress=ch?0x44f:0x43e;
        curve.temperatureCelsius=temperature(c.temperature[ch]);
        curve.curveSource="GM6 EC 1.12 candidate image; live table-group selector confirmed; rising reference";
        curve.graphCaption="Образ EC 1.12 · базовая кривая";
        curve.requestSource=ch?"EC 0x469 · base input; not final PWM":"EC 0x461 · base input; not final PWM";
        curve.requestPreOverride=true;curve.forced=(c.selectors[3]&0x48)!=0;
        if(!curve.forced&&c.request[ch]<=200)curve.requestedPercent=c.request[ch]/2.;
        curve.table=table.address;curve.sampledAtMs=at;
        out.actualPercent[ch]=c.pwm[ch]/2.;
        // 8E75..8E82 and 8EE1..8EEE use a strict temperature > Up[stage]
        // check. Iterate duplicate thresholds to show the steady rising base;
        // a live EC stage can lag or retain hysteresis and is shown separately.
        for(int t=20;t<=100;++t){
            std::size_t stage=0;
            while(stage+1<table.up.size()&&table.up[stage]!=255&&t>table.up[stage])++stage;
            curve.points.push_back({static_cast<double>(t),table.duty[stage]/2.});
        }
    }
    return {O::Available,std::move(out)};
}

NativeDefaultFanReading readFirmwareDefaultFans(EcAccess& ec){
    using O=NativeDefaultFanObservation;
    if(deviceOperations().passiveEcReadsPaused())return {};
    if(!ec.boardVerified()||deviceProfile(ec.verifyBoard().board).id!="mechrevo-gm6ir0c")
        return {O::Inactive,{}};
    const EcAccess::Transaction transaction(ec);
    const auto first=context(ec);if(!first)return {};
    DefaultFanContext sample;sample.context=*first;
    // Avoid an I2EC selector access in a confirmed Manual or Max state.
    if((*first)[0]!=0x4c||(*first)[1]!=25||(*first)[2]!=0x18||(*first)[3]!=1||
       ((*first)[8]&0x40)||((*first)[12]&&(*first)[13]))return {O::Inactive,{}};
    const auto selector=ec.readNativeFanFirmwareSelectors();if(!selector)return {};
    sample.selectors=*selector;
    sample.temperature={ec.readStable(0x43e),ec.readStable(0x44f)};
    sample.request={ec.readStable(0x461),ec.readStable(0x469)};
    sample.pwm={ec.readStable(0x75b),ec.readStable(0x75c)};
    const auto last=context(ec);const auto endSelector=ec.readNativeFanFirmwareSelectors();
    if(!last||!endSelector||*last!=*first||*endSelector!=*selector)return {};
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return decodeFirmwareDefaultFans("mechrevo-gm6ir0c",sample,static_cast<std::uint64_t>(now));
}
}
