#include "MagicControl/ControlAudit.h"
#include <windows.h>
#include "IteLightingWindows.h"
#include "BackendWindows.h"
#include "LightingOwnershipWindows.h"
#include "../../devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.h"
#include "../../protocols/uniwill/hydroc16g2/Lighting.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Logging.h"
#include "../common/FirmwareEvidence.h"
#include <span>
#include <iomanip>
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <mutex>
#include <map>
#include <stdexcept>

namespace mc::windows {
namespace {
[[noreturn]] void failure(const char* stage){throw std::runtime_error(std::string("ITE HID: ")+stage+"; Win32="+std::to_string(GetLastError()));}
struct HidApi {
    HMODULE dll{LoadLibraryExW(L"hid.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)};
    template<class T>T proc(const char* name){auto p=reinterpret_cast<T>(dll?GetProcAddress(dll,name):nullptr);if(!p)failure(name);return p;}
    decltype(&HidD_GetHidGuid) guid{proc<decltype(guid)>("HidD_GetHidGuid")};
    decltype(&HidD_GetAttributes) attributes{proc<decltype(attributes)>("HidD_GetAttributes")};
    decltype(&HidD_GetPreparsedData) parsed{proc<decltype(parsed)>("HidD_GetPreparsedData")};
    decltype(&HidD_FreePreparsedData) freeParsed{proc<decltype(freeParsed)>("HidD_FreePreparsedData")};
    decltype(&HidP_GetCaps) caps{proc<decltype(caps)>("HidP_GetCaps")};
    decltype(&HidD_SetFeature) set{proc<decltype(set)>("HidD_SetFeature")};
    decltype(&HidD_GetFeature) get{proc<decltype(get)>("HidD_GetFeature")};
    ~HidApi(){if(dll)FreeLibrary(dll);}
};
struct Handle {HANDLE value{INVALID_HANDLE_VALUE};~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Interface {std::wstring path;hydroc16g2::KeyboardHidDescriptor descriptor;};
std::vector<Interface> interfaces(HidApi& api,const std::function<void(const Interface&)>& observe={}){
    GUID guid{};api.guid(&guid);const auto devices=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(devices==INVALID_HANDLE_VALUE)failure("enumeration");
    struct Cleanup{HDEVINFO value;~Cleanup(){SetupDiDestroyDeviceInfoList(value);}} cleanup{devices};
    std::vector<Interface> result;
    for(DWORD i=0;i<4096;++i){
        SP_DEVICE_INTERFACE_DATA item{sizeof(item)};
        if(!SetupDiEnumDeviceInterfaces(devices,nullptr,&guid,i,&item)){
            if(GetLastError()!=ERROR_NO_MORE_ITEMS)failure("interface enumeration");return result;}
        DWORD bytes{};SetupDiGetDeviceInterfaceDetailW(devices,&item,nullptr,0,&bytes,nullptr);
        if(bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||bytes>65536)throw std::runtime_error("ITE HID: invalid interface metadata size");
        std::vector<BYTE> buffer(bytes);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(devices,&item,detail,bytes,nullptr,nullptr))failure("interface detail");
        Handle handle{CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr)};
        if(handle.value==INVALID_HANDLE_VALUE)continue;
        HIDD_ATTRIBUTES attr{sizeof(attr)};if(!api.attributes(handle.value,&attr))continue;
        if(attr.VendorID!=hydroc16g2::keyboardHidVendor)continue;
        PHIDP_PREPARSED_DATA parsed{};if(!api.parsed(handle.value,&parsed))failure("preparsed metadata");
        HIDP_CAPS caps{};const auto status=api.caps(parsed,&caps);api.freeParsed(parsed);
        if(status!=HIDP_STATUS_SUCCESS)throw std::runtime_error("ITE HID: invalid capabilities");
        if(caps.FeatureReportByteLength<9||caps.FeatureReportByteLength>4096){
            if(observe)observe({detail->DevicePath,{attr.VendorID,attr.ProductID,attr.VersionNumber,caps.UsagePage,caps.Usage,caps.FeatureReportByteLength}});
            continue;
        }
        result.push_back({detail->DevicePath,{attr.VendorID,attr.ProductID,attr.VersionNumber,caps.UsagePage,caps.Usage,caps.FeatureReportByteLength}});
        if(observe)observe(result.back());
    }
    throw std::runtime_error("ITE HID: incomplete interface enumeration");
}
class HidFeatureTransaction {
    HANDLE mutex_{};bool locked_{};
public:
    explicit HidFeatureTransaction(DWORD timeout=1500){
        mutex_=CreateMutexW(nullptr,FALSE,L"Local\\MagicControl.Hydroc.IteFeature");
        if(!mutex_)failure("feature mutex");const auto result=WaitForSingleObject(mutex_,timeout);
        locked_=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;
        if(!locked_){CloseHandle(mutex_);mutex_=nullptr;throw std::runtime_error("ITE HID: feature channel busy or unavailable");}
    }
    ~HidFeatureTransaction(){if(locked_)ReleaseMutex(mutex_);if(mutex_)CloseHandle(mutex_);}
    HidFeatureTransaction(const HidFeatureTransaction&)=delete;
};
struct OemFlagValue {LSTATUS status{};DWORD value{},type{},bytes{sizeof(DWORD)};};
OemFlagValue oemFlag(const wchar_t* name,DWORD view=RRF_SUBKEY_WOW6464KEY){
    OemFlagValue result;
    result.status=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\OEM\\GamingCenter2\\RGBKeyboard",name,
        RRF_RT_REG_DWORD|view,&result.type,&result.value,&result.bytes);return result;
}
bool oemZone(const wchar_t* name){
    const auto flag=oemFlag(name);return flag.status==ERROR_SUCCESS&&flag.value==1;
}
Handle openInterface(HidApi& api,const Interface& device){
        Handle handle{CreateFileW(device.path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr)};
        if(handle.value==INVALID_HANDLE_VALUE)failure("open feature interface");
        HIDD_ATTRIBUTES attr{sizeof(attr)};if(!api.attributes(handle.value,&attr)||attr.VendorID!=device.descriptor.vid||attr.ProductID!=device.descriptor.pid)
            throw std::runtime_error("ITE HID: endpoint identity changed");
        PHIDP_PREPARSED_DATA parsed{};if(!api.parsed(handle.value,&parsed))failure("reopened endpoint metadata");
        HIDP_CAPS caps{};const auto status=api.caps(parsed,&caps);api.freeParsed(parsed);
        if(status!=HIDP_STATUS_SUCCESS||caps.UsagePage!=device.descriptor.usagePage||caps.Usage!=device.descriptor.usage||
            caps.FeatureReportByteLength!=device.descriptor.featureBytes||caps.FeatureReportByteLength<9||caps.FeatureReportByteLength>4096)
            throw std::runtime_error("ITE HID: endpoint descriptor changed or feature size is unreviewed");
        // Return by guaranteed elision; Handle is never copied/moved.
        const auto raw=handle.value;handle.value=INVALID_HANDLE_VALUE;return Handle{raw};
    }
void sendReport(HidApi& api,HANDLE handle,const Interface& device,const hydroc16g2::LightingPacket& packet){
        std::vector<std::uint8_t> full(device.descriptor.featureBytes,0);std::copy(packet.begin(),packet.end(),full.begin());
        audit::Scope trace("hid.set_feature","\"vid\":"+std::to_string(device.descriptor.vid)+",\"pid\":"+std::to_string(device.descriptor.pid)+",\"request_hex\":"+audit::quote(audit::hex(full)));
        if(!api.set(handle,full.data(),static_cast<ULONG>(full.size()))){const auto error=GetLastError();trace.fail("SetFeature Win32="+std::to_string(error));SetLastError(error);failure("SetFeature");}
        trace.finish("transport_accepted","\"device_ack_received\":false");
    }
std::vector<std::uint8_t> firmwareReport(HidApi& api,HANDLE handle,const Interface& device){
    audit::Scope trace("hid.get_feature","\"vid\":"+std::to_string(device.descriptor.vid)+",\"pid\":"+std::to_string(device.descriptor.pid));
    sendReport(api,handle,device,{0,0x80,0,0,0,0,0,0,0});Sleep(1);
    std::vector<std::uint8_t> data(device.descriptor.featureBytes,0);
    if(!api.get(handle,data.data(),static_cast<ULONG>(data.size()))){const auto error=GetLastError();trace.fail("GetFeature Win32="+std::to_string(error));SetLastError(error);failure("GetFeature80");}
    trace.finish("transport_returned","\"response_hex\":"+audit::quote(audit::hex(data)));return data;
}
std::vector<std::uint8_t> queryFirmware(HidApi& api,HANDLE handle,const Interface& device){
    auto data=firmwareReport(api,handle,device);
    if(data[0]!=0||data[1]!=0x80)throw std::runtime_error("ITE HID: unexpected reply command/report ID");return data;
}

std::filesystem::path settingsFile(LightingZone zone){return portableSettingsPath("hydroc16g2-lighting-"+std::string(lightingZoneKey(zone))+".ini");}
std::optional<LightingConfiguration> load(LightingZone zone){
    const auto file=settingsFile(zone);std::error_code error;if(!std::filesystem::exists(file,error)){if(error)throw std::runtime_error(error.message());return {};}
    if(std::filesystem::file_size(file)>4096)throw std::runtime_error("ITE: lighting settings exceed4KiB");
    std::ifstream input(file);LightingConfiguration c;int version{},r{},g{},b{};std::string end;
    if(!(input>>version>>c.level>>c.effect>>c.speed>>c.direction>>r>>g>>b)||version!=1||
        !hydroc16g2::validLightingConfiguration(c)||r<0||r>255||g<0||g>255||b<0||b>255||(input>>end))
        throw std::runtime_error("ITE: invalid per-zone lighting settings; file preserved");
    c.rgb={static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(g),static_cast<std::uint8_t>(b)};return c;
}
}
struct IteLightingController::Impl {
    struct Endpoint{Interface device;std::array<std::uint8_t,4> firmware{};};
    std::mutex gate;HidApi api;std::map<LightingZone,Endpoint> zones;
    std::chrono::steady_clock::time_point nextDiscovery{};std::string discoveryStatus;
    void guard(){verifyHydrocControlIdentity();if(wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("ITE: --no-ec disables hardware control");}
    Handle open(const Endpoint& endpoint){return openInterface(api,endpoint.device);}
    void send(HANDLE handle,const Endpoint& endpoint,const hydroc16g2::LightingPacket& packet){sendReport(api,handle,endpoint.device,packet);}
    std::vector<std::uint8_t> query(HANDLE handle,const Endpoint& endpoint){return queryFirmware(api,handle,endpoint.device);}
    void discover(){
        if(std::chrono::steady_clock::now()<nextDiscovery)return;
        nextDiscovery=std::chrono::steady_clock::now()+std::chrono::seconds(10);zones.clear();
        guard();ensureLightingWritable();HidFeatureTransaction feature;std::string evidence;verifyHydrocDriver(evidence);
        auto ec=createWindowsAcpiDiagnosticBackend();ec->lockTransaction();
        std::array<std::uint8_t,4> context{};
        try{constexpr std::array<std::uint16_t,4> addresses{0x740,0x73c,0x7c3,0x78e};
            for(std::size_t i=0;i<4;++i){context[i]=ec->read(addresses[i]);if(ec->read(addresses[i])!=context[i])throw std::runtime_error("ITE: unstable EC identity");}
        }catch(...){ec->unlockTransaction();throw;}ec->unlockTransaction();
        if(context[0]!=26||context[2]!=13||!(context[3]&64))throw std::runtime_error("ITE: HYDROC project/platform identity mismatch");
        std::vector<Endpoint> keyboard,lightbar;
        for(const auto& device:interfaces(api)){
            const auto& d=device.descriptor;
            const bool kb=hydroc16g2::keyboardHidCandidate(d).candidate;
            const bool bar=(d.pid==0x7000||d.pid==0x7001)&&d.usagePage==0xff03&&d.usage==2;
            if(!kb&&!bar)continue;
            Endpoint endpoint{device};auto handle=open(endpoint);const auto fw=query(handle.value,endpoint);
            std::copy_n(fw.begin()+2,4,endpoint.firmware.begin());
            // Exact reviewed FW branch for EC73C=24. Other types require their
            // own colour calibration/static-map codec; never fall back to EC.
            if(kb&&d.usagePage==0xff03&&context[1]==24&&fw[2]==34)keyboard.push_back(endpoint);
            if(bar&&fw[2]&&fw[2]!=255)lightbar.push_back(endpoint);
        }
        if(keyboard.size()==1)zones.emplace(LightingZone::Keyboard,keyboard.front());
        if(lightbar.size()==1){
            // LM_ITE_RGB.GetDeviceSupport: OEM Force* explicitly selects the
            // fitted boards. PID/type alone does not prove that a logo exists.
            if(oemZone(L"ForceBase"))zones.emplace(LightingZone::Lightbar,lightbar.front());
            if(oemZone(L"ForceLogo"))zones.emplace(LightingZone::Logo,lightbar.front());
            if(oemZone(L"ForceHinge"))zones.emplace(LightingZone::Hinge,lightbar.front());
        }
        discoveryStatus=zones.empty()?"HYDROC: supported ITE type/zones not detected; no EC lighting fallback":"ITE HID: supported zones detected; RGB/zone readback is unavailable";
        if(keyboard.size()>1||lightbar.size()>1)discoveryStatus+="; ambiguous endpoints refused";
    }
};
IteLightingController::IteLightingController():impl_(std::make_unique<Impl>()){}
IteLightingController::~IteLightingController()=default;
LightingZonesSnapshot IteLightingController::read(){
    std::lock_guard lock(impl_->gate);LightingZonesSnapshot result;
    try{impl_->guard();impl_->discover();const auto owner=inspectLightingOwnership();
        for(const auto& [zone,endpoint]:impl_->zones){
            LightingZoneInfo info;info.zone=zone;info.writable=owner.writable;info.configured=load(zone);
            info.status=owner.writable?"HID: saved configuration; physical lighting is not measured":owner.status;
            result.zones.push_back(std::move(info));
        }
        result.status=impl_->discoveryStatus;
    }catch(const std::exception& e){impl_->zones.clear();result.zones.clear();result.status=e.what();}
    std::string zones="[";for(const auto& zone:result.zones){if(zones.size()>1)zones+=',';zones+="{\"zone\":"+audit::quote(lightingZoneKey(zone.zone))+",\"writable\":"+(zone.writable?"true":"false")+",\"configured\":"+(zone.configured?audit::lighting(*zone.configured):"null")+",\"observed_rgb\":null,\"status\":"+audit::quote(zone.status)+"}";}zones+=']';
    audit::state("hid_lighting_zones","{\"zones\":"+zones+",\"status\":"+audit::quote(result.status)+"}");return result;
}
void IteLightingController::apply(LightingZone zone,const LightingConfiguration& c){
    audit::Scope trace("lighting.zone_apply","\"zone\":"+audit::quote(lightingZoneKey(zone))+",\"requested\":"+audit::lighting(c));
    if(!hydroc16g2::validLightingConfiguration(c))throw std::invalid_argument("ITE: invalid lighting configuration");
    std::lock_guard lock(impl_->gate);impl_->guard();ensureLightingWritable();HidFeatureTransaction feature;
    // Fresh enumeration/admission for each explicit write; no stale USB path.
    impl_->nextDiscovery={};impl_->discover();const auto found=impl_->zones.find(zone);
    if(found==impl_->zones.end())throw std::runtime_error("ITE: selected zone is not supported by the current device");
    const auto& endpoint=found->second;auto handle=impl_->open(endpoint);
    const auto fw=impl_->query(handle.value,endpoint);
    if(!std::equal(endpoint.firmware.begin(),endpoint.firmware.end(),fw.begin()+2))throw std::runtime_error("ITE: firmware changed before apply");
    // A zone-specific08 with brightness0 blanks just that zone. Never the OEM
    // global off command01 (which can extinguish the other lightbar boards).
    impl_->send(handle.value,endpoint,hydroc16g2::lightingColor(zone,c));
    impl_->send(handle.value,endpoint,hydroc16g2::lightingEffect(zone,c));
    std::string error;const auto text="1 "+std::to_string(c.level)+" "+std::to_string(c.effect)+" "+std::to_string(c.speed)+" "+std::to_string(c.direction)+" "+
        std::to_string(c.rgb[0])+" "+std::to_string(c.rgb[1])+" "+std::to_string(c.rgb[2])+"\n";
    if(!savePortableText(settingsFile(zone),text,error))throw std::runtime_error("HID command accepted, but zone configuration was not saved: "+error);
    trace.finish("transport_accepted_configuration_saved","\"zone\":"+audit::quote(lightingZoneKey(zone))+",\"configured\":"+audit::lighting(c)+",\"observed_rgb\":null");
    logMessage("lighting",std::string(lightingZoneKey(zone))+": HID08/14 accepted; per-zone configuration saved; physical response unmeasured; EC writes=0; Save=0");
}
std::shared_ptr<IteLightingController> iteLightingController(){
    if(wcsstr(GetCommandLineW(),L"--no-ec")||!hydrocProfile(deviceProfile(readBoardDescriptor()).id))return {};
    static auto controller=std::make_shared<IteLightingController>();return controller;
}
bool captureIteLightingEvidence(const std::function<void(const InventoryRow&)>& emit){
    using diagnostics::firmwareQuote;
    const auto record=[&](const char* kind,std::string json){emit({kind,std::move(json)});};
    const auto text=[](const std::wstring& value){
        const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        if(!size&&!value.empty())throw std::runtime_error("ITE diagnostic: invalid interface UTF16");
        std::string out(size,0);if(size)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),size,nullptr,nullptr);return out;
    };
    const auto hex=[](std::span<const std::uint8_t> data){std::ostringstream out;out<<std::hex<<std::setfill('0');
        for(const auto b:data)out<<std::setw(2)<<static_cast<unsigned>(b);return out.str();};
    record("contract","{\"schema\":1,\"scope\":\"explicit HYDROC dump only\",\"ec_access\":false,\"registry_writes\":false,\"lighting_effect_writes\":false,\"only_feature_command\":128,\"feature_query_is_SetFeature_then_GetFeature\":true,\"flash_save\":false,\"physical_zones_confirmed\":false,\"non_atomic\":true}");
    if(!hydrocProfile(deviceProfile(readBoardDescriptor()).id)){
        record("status","{\"status\":\"not_applicable\",\"reason\":\"Actual board is not HYDROC-16 G2; HID firmware not queried\"}");return true;
    }
    // Native Force overrides are evidence, not measurements of installed LEDs.
    // Preserve missing/access-denied/wrong-type distinctly, including both views.
    for(const auto view:{RRF_SUBKEY_WOW6464KEY,RRF_SUBKEY_WOW6432KEY})for(const auto* name:{L"ForceBase",L"ForceLogo",L"ForceHinge",L"Forcenewlogosupport",L"MBlogoSupport"}){
        const auto flag=oemFlag(name,view);const bool valid=flag.status==ERROR_SUCCESS;
        const auto status=valid?"ok":flag.status==ERROR_FILE_NOT_FOUND||flag.status==ERROR_PATH_NOT_FOUND?"missing":flag.status==ERROR_UNSUPPORTED_TYPE?"wrong_type":"error";
        record("oem_flag","{\"source\":\"HKLM\\\\SOFTWARE\\\\OEM\\\\GamingCenter2\\\\RGBKeyboard\",\"registry_view\":"+
            std::to_string(view==RRF_SUBKEY_WOW6464KEY?64:32)+",\"name\":"+firmwareQuote(text(name))+",\"status\":"+firmwareQuote(status)+
            ",\"win32\":"+std::to_string(flag.status)+",\"registry_type\":"+std::to_string(flag.type)+",\"value\":"+(valid?std::to_string(flag.value):"null")+
            ",\"boolean\":"+(valid&&flag.value<=1?(flag.value?"true":"false"):"null")+",\"sample_ms\":"+std::to_string(GetTickCount64())+"}");
    }
    try{verifyHydrocControlIdentity();
        if(wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("--no-ec: HID feature query disabled; registry metadata retained");
    }catch(const std::exception& e){record("firmware_query_admission","{\"allowed\":false,\"reason\":"+firmwareQuote(e.what())+"}");return false;}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);bool complete=true;unsigned candidates{};
    try{HidApi api;
        interfaces(api,[&](const Interface& device){
            if(std::chrono::steady_clock::now()>=deadline||candidates>=64)throw std::runtime_error("ITE diagnostic: time/device budget reached; previous records retained");
            const auto& d=device.descriptor;
            const bool keyboard=hydroc16g2::keyboardHidCandidate(d).candidate;
            const bool keyboardPid=hydroc16g2::keyboardHidContains(hydroc16g2::keyboardHidPids,d.pid);
            const bool lightbar=hydroc16g2::keyboardHidContains(hydroc16g2::lightbarHidPids,d.pid);
            if(!keyboardPid&&!lightbar)return;
            const auto path=firmwareQuote(text(device.path));++candidates;
            record("hid_descriptor","{\"path\":"+path+",\"vid\":"+std::to_string(d.vid)+",\"pid\":"+std::to_string(d.pid)+
                ",\"usb_bcd_revision_not_firmware\":"+std::to_string(d.revision)+",\"usage_page\":"+std::to_string(d.usagePage)+",\"usage\":"+std::to_string(d.usage)+
                ",\"feature_buffer_bytes\":"+std::to_string(d.featureBytes)+",\"keyboard_candidate\":"+(keyboard?"true":"false")+",\"lightbar_pid\":"+(lightbar?"true":"false")+"}");
            // Metadata is emitted before the bounded child makes a request.
            // Each endpoint fails independently; no colour/brightness function
            // or live IteLightingController is constructed by this helper.
            if(d.featureBytes<9||d.featureBytes>4096||(!keyboard&&!lightbar)||
                (lightbar&&(d.usagePage!=0xff03||(d.usage!=1&&d.usage!=2)))){
                complete=false;record("hid_firmware","{\"path\":"+path+",\"status\":\"not_queried\",\"reason\":\"Unreviewed Usage/Page/FeatureReportByteLength\",\"firmware_bytes\":null}");return;
            }
            record("firmware_query_begin","{\"path\":"+path+",\"command\":128,\"sample_ms\":"+std::to_string(GetTickCount64())+"}");
            try{HidFeatureTransaction channel(1000);ensureLightingWritable();
                auto handle=openInterface(api,device);const auto reply=firmwareReport(api,handle.value,device);
                const bool valid=reply[0]==0&&reply[1]==0x80&&reply[2]!=0&&reply[2]!=255;
                const auto version=[&](bool hexadecimal){std::ostringstream out;if(hexadecimal)out<<std::hex<<std::setfill('0');
                    for(int i=2;i<6;++i){if(i!=2)out<<'.';if(hexadecimal)out<<std::setw(2);out<<static_cast<unsigned>(reply[i]);}return out.str();};
                record("hid_firmware","{\"path\":"+path+",\"status\":"+firmwareQuote(valid?"ok":"invalid_reply")+",\"reply_command\":"+std::to_string(reply[1])+
                    ",\"report_id\":"+std::to_string(reply[0])+",\"raw_feature_reply_hex\":"+firmwareQuote(hex(reply))+",\"firmware_bytes\":"+
                    (valid?"["+std::to_string(reply[2])+","+std::to_string(reply[3])+","+std::to_string(reply[4])+","+std::to_string(reply[5])+"]":"null")+
                    ",\"firmware_version_hex\":"+(valid?firmwareQuote(version(true)):"null")+",\"firmware_version_decimal\":"+(valid?firmwareQuote(version(false)):"null")+
                    ",\"sample_ms\":"+std::to_string(GetTickCount64())+"}");
                if(!valid)complete=false;
            }catch(const std::exception& e){complete=false;record("hid_firmware","{\"path\":"+path+",\"status\":\"error\",\"firmware_bytes\":null,\"reason\":"+firmwareQuote(e.what())+"}");}
        });
    }catch(const std::exception& e){complete=false;record("enumeration_status","{\"complete\":false,\"reason\":"+firmwareQuote(e.what())+"}");}
    record("summary","{\"complete\":"+std::string(complete?"true":"false")+",\"candidate_interfaces\":"+std::to_string(candidates)+
        ",\"status\":"+firmwareQuote(candidates?"See per-interface firmware and OEM flags; no physical LED response measured":"No reviewed ITE descriptor found; absence of a zone is not inferred")+"}");
    return complete;
}
}
