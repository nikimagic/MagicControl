#include "MagicControl/LaptopLightingResource.h"
#include "../../../src/protocols/uniwill/hydroc16g2/Lighting.h"
#include <iostream>
#include <vector>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Native final:mc::LaptopControl {
    std::vector<std::string> calls;bool failLevel{},failTimeout{};mc::LaptopControlSnapshot state;
    mc::LaptopControlSnapshot read()override{return state;}
    void setChargeThresholds(int,int)override{throw std::logic_error("unexpected charge write");}
    void setFnLock(bool)override{throw std::logic_error("unexpected key write");}
    void setKeyboardLevel(int level)override{
        calls.push_back("level:"+std::to_string(level));
        if(failLevel||level<0||level>2)throw std::runtime_error("level/readback failed");
        state.keyboardLevel=level;
    }
    void setKeyboardTimeout(int seconds)override{
        calls.push_back("timeout:"+std::to_string(seconds));
        if(failTimeout||seconds<0||seconds>3600)throw std::runtime_error("timeout/readback failed");
        state.keyboardTimeoutSeconds=seconds;
    }
};
}
int main(){try{
    using namespace mc;Native control;
    auto request=LightingWriteRequest::nativeUpdate(2,30);
    auto result=executeLightingResource(control,request);
    check(result.success&&control.calls==std::vector<std::string>({"level:2","timeout:30"}),"native pair changed setter order");
    check(lightingReadbackMatches(control.read(),request),"successful native pair does not match its observation");
    control.calls.clear();result=executeLightingResource(control,{LightingWrite::Level,0});
    check(result.success&&control.calls==std::vector<std::string>{"level:0"},"OFF must not change timeout");
    check(lightingReadbackMatches(control.read(),{LightingWrite::Level,0}),"real zero level must remain known");
    control.state.keyboardLevel.reset();check(!lightingReadbackMatches(control.read(),{LightingWrite::Level,0}),"missing level confirmed OFF");
    control.calls.clear();result=executeLightingResource(control,{LightingWrite::Timeout,15});
    check(result.success&&control.calls==std::vector<std::string>{"timeout:15"},"timeout-only changed brightness");
    control.failLevel=true;control.calls.clear();result=executeLightingResource(control,request);
    check(!result.success&&result.message=="level/readback failed"&&control.calls.size()==2,"successful second field hid the first error or was not attempted");
    control.failTimeout=true;control.calls.clear();result=executeLightingResource(control,request);
    check(!result.success&&result.message=="level/readback failed; timeout/readback failed"&&control.calls.size()==2,"paired errors were duplicated or lost");
    control.failLevel=false;control.failTimeout=false;control.calls.clear();result=executeLightingResource(control,{LightingWrite::Level,4});
    check(!result.success&&control.state.keyboardLevel!=4,"generic entry overrode native model bounds");
    control.calls.clear();
    for(auto kind:{LightingWrite::Effect,LightingWrite::Palette,LightingWrite::CustomColor,LightingWrite::Native,LightingWrite::Cycle,LightingWrite::Zone,LightingWrite::Update,LightingWrite::Pulse,static_cast<LightingWrite>(42)}){
        bool refused{};try{executeLightingResource(control,{kind,1});}catch(const std::invalid_argument&){refused=true;}
        check(refused&&control.calls.empty(),"native transport borrowed a SingleZone/HID command");
    }
    for(const auto empty:{LightingWriteRequest{},LightingWriteRequest{LightingWrite::Level,-1},LightingWriteRequest{LightingWrite::Timeout,-1},LightingWriteRequest::nativeUpdate(-1,-1)}){
        bool refused{};try{executeLightingResource(control,empty);}catch(const std::invalid_argument&){refused=true;}
        check(refused&&control.calls.empty(),"empty native request reached a setter");
    }
    KeyboardBacklightChange change;change.level=3;change.effect=1;change.pulse=4;change.powerDisabled=true;change.preserveEffectsWhenBlanked=true;
    change.expected=KeyboardBacklightState{};change.expected->pulseRaw=0x84;
    const auto restore=LightingWriteRequest::singleZone(change,LightingWriteOrigin::Restore);
    check(restore.kind==LightingWrite::Update&&restore.origin==LightingWriteOrigin::Restore&&restore.change.expected==change.expected&&
        restore.change.level==3&&restore.change.preserveEffectsWhenBlanked&&restore.change.powerDisabled==true,"request factory lost restore ownership/blank semantics");
    const std::array zones{LightingZone::Keyboard,LightingZone::Lightbar,LightingZone::Logo,LightingZone::Hinge};
    const std::array controls{2,34,35,33};
    for(std::size_t i=0;i<zones.size();++i){
        LightingConfiguration config;config.level=0;config.rgb={7,8,9};
        const auto request=LightingWriteRequest::forZone(config);
        check(request.configuration==config,"zone request replaced independent configuration");
        const auto packet=hydroc16g2::lightingEffect(zones[i],request.configuration);
        check(packet[1]==8&&packet[2]==controls[i]&&packet[5]==0&&packet[8]==0,"zone OFF became global OFF or firmware Save");
    }
    LightingConfiguration invalid;invalid.level=5;check(!hydroc16g2::validLightingConfiguration(invalid),"invalid HID level became writable");
    const auto candidate=hydroc16g2::keyboardHidCandidate({0x048d,0x6000,1,0xff12,1,65});
    check(candidate.candidate&&!candidate.permission,"HID descriptor granted write permission");
    std::cout<<"PASS: native lighting order, independent failures, typed bounds, absence/readback predicate and exact restore request\n";
    std::cout<<"PASS: four zone requests, zone-specific OFF, Save=0 and descriptor-only admission\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
