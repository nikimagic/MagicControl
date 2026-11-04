#include "../../../src/platform/windows/LightingControlWindows.h"
#include "../../../src/platform/windows/SettingsActionWindows.h"
#include "../../../src/protocols/uniwill/SingleZoneBacklight.h"
#include "../../../tests/TestBoardFixture.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/KeyboardPulsePolicy.h"
#include "MagicControl/Logging.h"
#include "MagicControl/PortableSettings.h"
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Memory {std::array<std::uint8_t,65536> bytes{};std::recursive_mutex gate;int writes{};bool failGreen{};};
struct Backend final:mc::IEcBackend {
    std::shared_ptr<Memory> memory;mc::BackendInfo descriptor;
    Backend(std::shared_ptr<Memory> data,const mc::DeviceProfile& profile):memory(std::move(data)){
        descriptor.id=std::string(profile.ecBackendId);descriptor.transport=mc::EcTransport::WindowsAcpiDriver;
        descriptor.writable=true;descriptor.maxAddress=0xffff;
    }
    const mc::BackendInfo& info()const noexcept override{return descriptor;}
    std::uint8_t read(std::uint16_t address)override{std::lock_guard lock(memory->gate);return memory->bytes[address];}
    void write(std::uint16_t address,std::uint8_t value)override{
        std::lock_guard lock(memory->gate);++memory->writes;
        if(address==mc::singlezone::green&&memory->failGreen){memory->failGreen=false;throw std::runtime_error("fixture green write failed");}
        memory->bytes[address]=value;
    }
    void lockTransaction()override{memory->gate.lock();}void unlockTransaction()noexcept override{memory->gate.unlock();}
};
void runBoard(const mc::DeviceProfile& profile,const std::filesystem::path& preferences){
    using namespace mc;using namespace mc::gui;using namespace mc::singlezone;
    BoardDescriptor board;board.manufacturer=profile.manufacturer;board.baseBoard=profile.baseBoard;board.productName=profile.productName;
    tests::setTestBoard(board);
    auto data=std::make_shared<Memory>();for(const auto& probe:profile.identity)data->bytes[probe.address]=probe.expected;
    data->bytes[support]=rgbSupport;data->bytes[level]=0x80;data->bytes[effect]=0x12;data->bytes[pulse]=0x80;
    data->bytes[red]=10;data->bytes[green]=20;data->bytes[blue]=30;
    data->bytes[defaultRed]=20;data->bytes[defaultRed+1]=30;data->bytes[defaultRed+2]=40;
    EcAccess ec(std::make_unique<Backend>(data,profile));
    if(!profile.capabilities.supports(DeviceFeature::Backlight)){
        bool refused{};try{executeLightingResource(LightingControlContext{.ec=&ec},{LightingWrite::Level,1});}catch(const std::exception&){refused=true;}
        check(refused&&data->writes==0,"catalog-only profile gained a lighting writer");return;
    }
    check(ec.verifyBoard().verified,"fixture identity was not admitted");
    LightingControlContext context;context.ec=&ec;context.profile=std::string(profile.id);context.preferences=preferences;
    context.model=gmRamFanProfile(profile.id)?SingleZoneModel::Gm6:SingleZoneModel::Wujie;
    KeyboardColorCycle cycle([&]{return readLighting(context);},[&](const KeyboardBacklightChange& change){
        return executeLightingResource(context,LightingWriteRequest::singleZone(change,LightingWriteOrigin::Cycle)).keyboard.value();});
    context.cycle=&cycle;
    DevicePreferences saved;saved[DeviceSetting::KeyboardEffect]=1;saved[DeviceSetting::PulseColor]=2;
    saved[DeviceSetting::Red]=10;saved[DeviceSetting::Green]=20;saved[DeviceSetting::Blue]=30;
    std::string error;check(rememberDevicePreferences(saved,error,preferences),"fixture preferences not saved");
    // GUI and CLI now send the same Level request. Waking OFF must restore the
    // saved pulse, not silently switch to a raw brightness-only write.
    executeLightingResource(context,{LightingWrite::Level,0});auto state=readLighting(context);
    check((state.keyboard&2)&&state.breathing==0&&!(state.effect&0x80),"OFF did not stop native effects");
    executeLightingResource(context,{LightingWrite::Level,2});state=readLighting(context);
    check(!(state.keyboard&2)&&((state.keyboard>>5)&7)==2&&state.breathing==3,"wake lost saved pulse/brightness policy");
    windows::SettingsActionServices services;services.lighting=[&]{return context;};
    windows::executeSettingsAction({windows::SettingsActionKind::LightingLevel,0},services);
    windows::executeSettingsAction({windows::SettingsActionKind::LightingLevel,2},services);
    const auto guiState=readLighting(context);
    check(((guiState.keyboard^state.keyboard)&0xe2)==0&&guiState.breathing==state.breathing&&guiState.color==state.color,"GUI adapter diverged from the common CLI request");
    for(int index=0;index<8;++index){
        const auto preset=keyboardPulsePalette(context.model,context.profile).presets[index];const int writes=data->writes;
        bool refused{};try{if(index%2)windows::executeSettingsAction({windows::SettingsActionKind::LightingColor,index},services);else executeLightingResource(context,{LightingWrite::Palette,index});}catch(const std::exception&){refused=true;}
        check(preset?(!refused&&readLighting(context).breathing==preset):(refused&&data->writes==writes),"firmware pulse palette mapping/admission changed");
    }
    check((data->bytes[pulse]&0x80)&&((data->bytes[effect]&0x12)==0x12),"pulse changed FAN/USB/lightbar neighbour bits");
    for(int invalid:{-1,5}){const int writes=data->writes;bool refused{};try{executeLightingResource(context,{LightingWrite::Level,invalid});}catch(const std::exception&){refused=true;}
        check(refused&&data->writes==writes,"invalid brightness reached a write");}
    const int beforeCustom=data->writes;LightingWriteRequest custom{LightingWrite::CustomColor};custom.color={255,0,0};
    bool refused{};try{executeLightingResource(context,custom);}catch(const std::exception&){refused=true;}
    check(refused&&data->writes==beforeCustom,"SingleZone pulse accepted custom RGB");
    executeLightingResource(context,{LightingWrite::Effect,0});executeLightingResource(context,custom);state=readLighting(context);
    check(state.color==keyboardColorLevels(context.model,custom.color),"RGB used the wrong model codec");
    rememberLightingChoice(context,LightingChoice::CustomColor,0,{state.supported,false,false,state.keyboard,state.effect,state.breathing,state.color});
    DevicePreferences actual;check(loadDevicePreferences(preferences,actual,error)&&actual[DeviceSetting::Red]==state.color[0]&&actual[DeviceSetting::Green]==state.color[1],"explicit completion did not save observed RGB");
    // Exact temporary restore keeps idle blank/effects and expected ownership.
    const auto baseline=readLighting(context);KeyboardBacklightChange temporary;temporary.effect=0;temporary.color=KeyboardColor{1,2,3};temporary.expected=baseline;
    const auto applied=executeLightingResource(context,LightingWriteRequest::singleZone(temporary,LightingWriteOrigin::Temporary)).keyboard.value();
    KeyboardBacklightChange restore;restore.effect=0;restore.color=baseline.color;restore.expected=applied;restore.powerDisabled=true;restore.preserveEffectsWhenBlanked=true;
    state=executeLightingResource(context,LightingWriteRequest::singleZone(restore,LightingWriteOrigin::Restore)).keyboard.value();
    check(state.color==baseline.color&&(state.keyboard&2),"temporary restore lost RGB/idle power state");
    data->bytes[red]=data->bytes[red]?data->bytes[red]-1:1; // An external owner's valid RGB change.
    KeyboardBacklightChange foreign;foreign.color=KeyboardColor{4,5,6};foreign.expected=baseline;const int writes=data->writes;
    refused=false;try{executeLightingResource(context,LightingWriteRequest::singleZone(foreign,LightingWriteOrigin::Restore));}catch(const std::exception&){refused=true;}
    check(refused&&data->writes==writes,"foreign RGB was overwritten by restore");
    const auto rollback=readLighting(context);data->failGreen=true;LightingWriteRequest failed{LightingWrite::CustomColor};failed.color={0,255,0};
    refused=false;try{executeLightingResource(context,failed);}catch(const std::exception&){refused=true;}
    const auto rolled=readLighting(context);
    check(refused&&rolled.color==rollback.color&&rolled.breathing==rollback.breathing&&((rolled.effect^rollback.effect)&0x80)==0,"owned rollback failed after a fake write error");
    const int zoneWrites=data->writes;context.zone=LightingZone::Logo;
    refused=false;try{executeLightingResource(context,LightingWriteRequest::forZone({}));}catch(const std::exception&){refused=true;}
    check(refused&&data->writes==zoneWrites,"missing HID zone fell back to keyboard EC");context.zone=LightingZone::Keyboard;
    KeyboardCycleSettings settings;settings.intervalSeconds=3600;
    executeLightingResource(context,LightingWriteRequest::forCycle(settings));check(cycle.status().active,"fake host cycle did not start through the shared entry");cycle.stop();
    setKeyboardLightingSuspended(true);const int sleepWrites=data->writes;
    refused=false;try{executeLightingResource(context,{LightingWrite::Level,1});}catch(const std::exception&){refused=true;}
    check(refused&&data->writes==sleepWrites,"suspended keyboard accepted a write");setKeyboardLightingSuspended(false);
    std::cout<<"PASS: "<<profile.id<<" simulated SingleZone policy/masks/restore/rollback\n";
}
}
int main(int argc,char** argv){std::filesystem::path preferences;bool ownFile{};try{
    check(argc==2,"fixture preferences path required");preferences=argv[1];check(!std::filesystem::exists(preferences),"fixture file must not pre-exist");
    ownFile=true;
    mc::setLoggingEnabled(false);int boards{},absent{};
    for(const auto* profile:mc::deviceProfiles()){
        if(profile->id=="wu-jie-15-xpro"||mc::gmRamFanProfile(profile->id)){runBoard(*profile,preferences);++boards;}
        mc::gui::LightingControlContext missing;missing.profile=std::string(profile->id);bool refused{};
        try{mc::gui::executeLightingResource(missing,{mc::LightingWrite::Level,1});}catch(const std::exception&){refused=true;}
        check(refused,"model name granted access without a lighting backend");++absent;
    }
    mc::tests::resetTestBoard();std::filesystem::remove(preferences);
    std::cout<<"PASS: "<<boards<<" SingleZone family fixtures, "<<absent<<" missing-transport declarations; no hardware\n";
}catch(const std::exception& e){mc::setKeyboardLightingSuspended(false);mc::tests::resetTestBoard();if(ownFile){std::error_code error;std::filesystem::remove(preferences,error);}std::cerr<<e.what()<<'\n';return 1;}}
