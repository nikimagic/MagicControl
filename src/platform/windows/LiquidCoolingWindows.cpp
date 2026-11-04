#include "MagicControl/ActionLog.h"
#include <windows.h>
#include <roapi.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#include "MagicControl/LiquidCooling.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include "../../devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "../../protocols/uniwill/hydroc16g2/LiquidCooling.h"
#include "LightingOwnershipWindows.h"
#include "LiquidCoolingWindows.h"
#include "DiagnosticWmiWindows.h"
#include "BackendWindows.h"
#include "../common/FirmwareEvidence.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <format>
#include <stdexcept>
namespace mc {
namespace {
namespace bt=winrt::Windows::Devices::Bluetooth;
namespace adv=winrt::Windows::Devices::Bluetooth::Advertisement;
namespace gatt=winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
namespace lpp=hydroc16g2::lpp;
using Clock=std::chrono::steady_clock;
struct Apartment {
    HRESULT result{RoInitialize(RO_INIT_MULTITHREADED)};
    explicit Apartment(bool allowSta=false){if(FAILED(result)&&!(allowSta&&result==RPC_E_CHANGED_MODE))throw std::runtime_error(std::format("LPP requires a background MTA worker; RoInitialize=0x{:08X}",static_cast<unsigned>(result)));}
    ~Apartment(){if(SUCCEEDED(result))RoUninitialize();}
};
std::uint64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
template<class F>auto checked(F&& action){try{return action();}catch(const winrt::hresult_error& e){throw std::runtime_error("LPP WinRT: "+winrt::to_string(e.message())+std::format(" HRESULT=0x{:08X}",static_cast<unsigned>(e.code().value)));}}
template<class T>auto wait(T operation,std::stop_token stop,std::chrono::seconds timeout=std::chrono::seconds(8)){
    const auto deadline=Clock::now()+timeout;
    while(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started){
        if(stop.stop_requested()||Clock::now()>=deadline){operation.Cancel();throw std::runtime_error("LPP BLE timeout/cancel; late completion is possible, command must not be automatically retried");}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return operation.GetResults();
}
void guard(bool driver=false){
    windows::verifyHydrocControlIdentity();if(wcsstr(GetCommandLineW(),L"--no-ec"))throw std::runtime_error("LPP disabled by --no-ec");
    const auto owner=windows::inspectLightingOwnership();if(!owner.writable)throw std::runtime_error("LPP OEM ownership blocked: "+owner.status);
    if(driver){std::string evidence;windows::verifyHydrocDriver(evidence);}
}
void status(gatt::GattCommunicationStatus value,const char* stage){if(value!=gatt::GattCommunicationStatus::Success)throw std::runtime_error(std::string("LPP ")+stage+": GATT status="+std::to_string(static_cast<int>(value)));}
// Exact OEM WMILC_Condition: the bool parameter is unused. Firmware reads its
// current EC presence state; there is one constant request for both transitions.
unsigned notifyNativePolicyDirect(){
    using Microsoft::WRL::ComPtr;
    const auto check=[](HRESULT code){if(FAILED(code))throw std::runtime_error(std::format("LPP native WMI HRESULT=0x{:08X}",static_cast<unsigned>(code)));};
    struct Bstr{BSTR value;explicit Bstr(const wchar_t* s):value(SysAllocString(s)){if(!value)throw std::bad_alloc();}~Bstr(){SysFreeString(value);}};
    Apartment apartment;
    ComPtr<IWbemLocator> locator;check(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator)));
    Bstr ns(L"ROOT\\WMI");ComPtr<IWbemServices> service;
    check(locator->ConnectServer(ns.value,nullptr,nullptr,nullptr,WBEM_FLAG_CONNECT_USE_MAX_WAIT,nullptr,nullptr,&service));
    check(CoSetProxyBlanket(service.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE));
    Bstr className(L"AcpiTest_MULong"),method(L"GetSetULong"),path(L"AcpiTest_MULong.InstanceName=\"ACPI\\\\PNP0C14\\\\1_1\"");
    ComPtr<IWbemClassObject> type,inputDefinition,input,output;check(service->GetObject(className.value,0,nullptr,&type,nullptr));
    check(type->GetMethod(method.value,0,&inputDefinition,nullptr));check(inputDefinition->SpawnInstance(0,&input));
    VARIANT data{};VariantInit(&data);data.vt=VT_UI8;data.ullVal=5497558138892ULL;check(input->Put(L"Data",0,&data,0));
    ComPtr<IWbemCallResult> call;check(service->ExecMethod(path.value,method.value,WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,input.Get(),nullptr,&call));
    if(!call)throw std::runtime_error("LPP native WMI returned no call-result object");
    LONG result{};auto code=call->GetCallStatus(3000,&result);
    if(code==WBEM_S_TIMEDOUT)throw std::runtime_error("LPP native WMI notification timed out; no retry");check(code);check(result);
    code=call->GetResultObject(1000,&output);if(code==WBEM_S_TIMEDOUT||!output)throw std::runtime_error("LPP native WMI result unavailable; no retry");check(code);
    VARIANT value{},byte{};VariantInit(&value);VariantInit(&byte);
    struct Clear{VARIANT& a;VARIANT& b;~Clear(){VariantClear(&a);VariantClear(&b);}} clear{value,byte};
    check(output->Get(L"Return",0,&value,nullptr,nullptr));check(VariantChangeType(&byte,&value,0,VT_UI1));
    return byte.bVal;
}
void notifyNativePolicy(bool expectedPresence){
    windows::wmi::QueryCompletion completion;
    const auto rows=windows::wmi::isolatedQuery(expectedPresence?L"SELECT Name,Value FROM MagicControlLppConditionOn":L"SELECT Name,Value FROM MagicControlLppConditionOff",
        {L"Name",L"Value"},L"MAGICCONTROL\\LPP_NATIVE_POLICY_NOTIFY",std::chrono::seconds(5),{},completion);
    if(!completion.complete||rows.size()!=1||rows.front().get(L"Name")!="native_lpp_condition")throw std::runtime_error("LPP native-policy notification failed/uncertain: "+completion.status);
    logMessage("liquid_cooling",rows.front().get(L"Value"));
}
struct Shared {
    std::mutex gate;LiquidCoolingLink link;std::uint64_t epoch{};
};
class Backend final:public ILiquidCoolingBackend {
    struct Peer{LiquidCoolingDevice device;std::uint64_t address{};bt::BluetoothAddressType type{};Clock::time_point seen;};
    std::map<std::string,Peer> peers_;
    std::shared_ptr<Shared> shared_{std::make_shared<Shared>()};
    bt::BluetoothLEDevice device_{nullptr};gatt::GattDeviceService service_{nullptr};gatt::GattCharacteristic tx_{nullptr},rx_{nullptr};
    winrt::event_token valueToken_{},connectionToken_{};bool valuesRegistered_{},connectionRegistered_{};
    std::shared_ptr<windows::Gm6Controller> policy_;
    windows::Gm6Controller& policy(){if(!policy_)policy_=windows::gm6Controller();if(!policy_)throw std::runtime_error("LPP: exact HYDROC native policy controller unavailable");return *policy_;}
    void send(std::span<const std::uint8_t> bytes,std::stop_token stop){
        auto buffer=winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(winrt::array_view<const std::uint8_t>(bytes.data(),bytes.data()+bytes.size()));
        const auto result=wait(tx_.WriteValueWithResultAsync(buffer,gatt::GattWriteOption::WriteWithResponse),stop);
        status(result.Status(),"write");
    }
public:
    ~Backend()override{close();}
    std::vector<LiquidCoolingDevice> discover(std::stop_token stop)override{return checked([&]{
        guard();Apartment apartment;
        struct Scan{std::mutex gate;std::map<std::string,Peer> peers;bool ended{};std::optional<int> error;};auto scan=std::make_shared<Scan>();
        adv::BluetoothLEAdvertisementWatcher watcher;watcher.ScanningMode(adv::BluetoothLEScanningMode::Active);
        const auto received=watcher.Received([scan](const auto&,const adv::BluetoothLEAdvertisementReceivedEventArgs& args){try{
            const auto name=winrt::to_string(args.Advertisement().LocalName());if(!lpp::knownDevice(name))return;
            Peer peer;peer.device={std::format("lpp:{:012x}",args.BluetoothAddress()),name,{}};peer.address=args.BluetoothAddress();peer.type=args.BluetoothAddressType();peer.seen=Clock::now();
            std::lock_guard lock(scan->gate);if(!scan->ended)scan->peers[peer.device.id]=std::move(peer);
        }catch(...){}});
        const auto stopped=watcher.Stopped([scan](const auto&,const adv::BluetoothLEAdvertisementWatcherStoppedEventArgs& args){std::lock_guard lock(scan->gate);if(args.Error()!=bt::BluetoothError::Success)scan->error=static_cast<int>(args.Error());});
        struct Revoke{adv::BluetoothLEAdvertisementWatcher watcher;winrt::event_token received,stopped;~Revoke(){try{watcher.Stop();watcher.Received(received);watcher.Stopped(stopped);}catch(...){}}} revoke{watcher,received,stopped};
        watcher.Start();const auto deadline=Clock::now()+std::chrono::seconds(4);
        while(Clock::now()<deadline&&!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(20));
        watcher.Stop();std::lock_guard lock(scan->gate);scan->ended=true;
        if(stop.stop_requested())throw std::runtime_error("LPP discovery cancelled");if(scan->error)throw std::runtime_error("LPP discovery Bluetooth error="+std::to_string(*scan->error));
        peers_=scan->peers;std::vector<LiquidCoolingDevice> result;for(const auto& [id,peer]:peers_)result.push_back(peer.device);return result;
    });}
    void connect(const std::string& id,std::stop_token stop)override{checked([&]{
        guard();Apartment apartment;const auto found=peers_.find(id);
        if(found==peers_.end()||Clock::now()-found->second.seen>std::chrono::seconds(60))throw std::runtime_error("LPP: select an exact device from a fresh explicit discovery");
        const auto peer=found->second;close();std::uint64_t epoch;
        {std::lock_guard lock(shared_->gate);shared_->link={};shared_->link.deviceId=id;shared_->link.deviceName=peer.device.name;epoch=shared_->epoch;}
        try{
            device_=wait(bt::BluetoothLEDevice::FromBluetoothAddressAsync(peer.address,peer.type),stop);
            if(!device_||winrt::to_string(device_.Name())!=peer.device.name)throw std::runtime_error("LPP: BLE device name/identity changed or access unavailable");
            const auto services=wait(device_.GetGattServicesForUuidAsync(winrt::guid(lpp::service),bt::BluetoothCacheMode::Uncached),stop);status(services.Status(),"service discovery");
            if(services.Services().Size()!=1)throw std::runtime_error("LPP: native UART service missing or ambiguous");service_=services.Services().GetAt(0);
            const auto chars=wait(service_.GetCharacteristicsAsync(bt::BluetoothCacheMode::Uncached),stop);status(chars.Status(),"characteristic discovery");
            int txCount{},rxCount{};for(const auto& c:chars.Characteristics()){
                if(c.Uuid()==winrt::guid(lpp::tx)){tx_=c;++txCount;}if(c.Uuid()==winrt::guid(lpp::rx)){rx_=c;++rxCount;}}
            if(txCount!=1||rxCount!=1||!bool(tx_.CharacteristicProperties()&gatt::GattCharacteristicProperties::Write)||!bool(rx_.CharacteristicProperties()&gatt::GattCharacteristicProperties::Notify))
                throw std::runtime_error("LPP: native TX Write / RX Notify contract not satisfied");
            std::weak_ptr<Shared> weak=shared_;
            valueToken_=rx_.ValueChanged([weak,epoch](const auto&,const gatt::GattValueChangedEventArgs& args){try{
                const auto state=weak.lock();if(!state||args.CharacteristicValue().Length()>512)return;winrt::com_array<std::uint8_t> bytes;winrt::Windows::Security::Cryptography::CryptographicBuffer::CopyToByteArray(args.CharacteristicValue(),bytes);
                const std::span<const std::uint8_t> data(bytes.data(),bytes.size());std::lock_guard lock(state->gate);if(state->epoch!=epoch)return;
                audit::event("ble_notification","\"connection_epoch\":"+std::to_string(epoch)+",\"response_hex\":"+audit::quote(audit::hex(data)));
                state->link.lastNotificationHex.clear();for(const auto b:data)state->link.lastNotificationHex+=std::format("{:02x}",b);
                if(const auto meter=lpp::meterState(data)){state->link.meterState=*meter;state->link.meterAtMs=nowMs();}
                else if(const auto version=lpp::firmwareVersion(data)){state->link.firmware=*version;state->link.firmwareReply.assign(data.begin(),data.end());state->link.connected=state->link.transportConnected&&state->link.notificationsReady;}
            }catch(...){}});valuesRegistered_=true;
            connectionToken_=device_.ConnectionStatusChanged([weak,epoch](const bt::BluetoothLEDevice& sender,const auto&){try{
                const auto state=weak.lock();if(!state)return;std::lock_guard lock(state->gate);if(state->epoch!=epoch)return;
                state->link.transportConnected=sender.ConnectionStatus()==bt::BluetoothConnectionStatus::Connected;
                state->link.connected=state->link.transportConnected&&state->link.notificationsReady&&!state->link.firmware.empty();
                if(!state->link.transportConnected)state->link.status="BLE disconnected; physical pump/fan state unknown";
            }catch(...){}});connectionRegistered_=true;
            status(wait(rx_.WriteClientCharacteristicConfigurationDescriptorAsync(gatt::GattClientCharacteristicConfigurationDescriptorValue::Notify),stop),"subscribe Notify");
            {std::lock_guard lock(shared_->gate);shared_->link.notificationsReady=true;shared_->link.transportConnected=device_.ConnectionStatus()==bt::BluetoothConnectionStatus::Connected;}
            constexpr std::array<std::uint8_t,2> versionQuery{'s','w'};send(versionQuery,stop);
            const auto deadline=Clock::now()+std::chrono::seconds(3);
            while(Clock::now()<deadline&&!stop.stop_requested()){if(link().connected)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            if(!link().connected)throw std::runtime_error("LPP: firmware handshake incomplete/cancelled; no cooling commands sent");
            std::lock_guard lock(shared_->gate);shared_->link.status="GATT/Notify/sw firmware ready; transport acceptance is not physical cooling feedback";
        }catch(...){close();throw;}
    });}
    LiquidCoolingLink link()override{std::lock_guard lock(shared_->gate);return shared_->link;}
    void write(std::span<const std::uint8_t> bytes,std::stop_token stop)override{audit::Scope trace("lpp.ble_write","\"request_hex\":"+audit::quote(audit::hex(bytes)));checked([&]{
        if(!lpp::allowedPacket(bytes))throw std::invalid_argument("LPP: command outside fixed native allowlist");guard();Apartment apartment;
        if(!link().connected||!tx_)throw std::runtime_error("LPP: command refused without current firmware/Notify connection");
        if(bytes[1]!=0x32){
            const auto raw=policy().readLiquidCoolingPolicy();if((raw[0]&lpp::presenceMask)&&!nativePolicyOwned())throw std::runtime_error("LPP: foreign native LC policy owns cooling; actuation refused");
            if(nativePolicyOwned()&&!(raw[0]&lpp::presenceMask))throw std::runtime_error("LPP: native LC policy changed externally; automatic actuation refused");
        }
        try{send(bytes,stop);trace.finish("gatt_accepted","\"device_ack_received\":false");}catch(...){close();throw;}
    });}
    void close()noexcept override{
        {std::lock_guard lock(shared_->gate);++shared_->epoch;shared_->link.connected=false;shared_->link.transportConnected=false;shared_->link.notificationsReady=false;}
        try{Apartment apartment(true);if(valuesRegistered_&&rx_)rx_.ValueChanged(valueToken_);if(connectionRegistered_&&device_)device_.ConnectionStatusChanged(connectionToken_);
            if(service_)service_.Close();if(device_)device_.Close();}catch(...){}
        valuesRegistered_=connectionRegistered_=false;tx_=nullptr;rx_=nullptr;service_=nullptr;device_=nullptr;
    }
    LiquidCoolingTargets automaticTargets()override{
        guard(true);if(!nativePolicyOwned())throw std::runtime_error("LPP: no owned native LC policy");const auto raw=policy().readLiquidCoolingPolicy();
        if(!(raw[0]&lpp::presenceMask))throw std::runtime_error("LPP: native presence lost");return lpp::targets(raw[1],raw[2]);
    }
    void prepareAutomatic()override{guard(true);policy().verifyLiquidCoolingPolicyAdmission();}
    void acquireNativePolicy()override{
        guard(true);if(nativePolicyOwned())return;
        try{policy().setLiquidCoolingPresence(true);notifyNativePolicy(true);}
        catch(const std::exception& e){std::string reason=e.what();
            if(nativePolicyOwned())try{policy().setLiquidCoolingPresence(false);notifyNativePolicy(false);}
            catch(const std::exception& cleanup){reason+="; owned native policy cleanup: "+std::string(cleanup.what());}
            throw std::runtime_error(reason);
        }
    }
    void releaseNativePolicy()override{
        if(!nativePolicyOwned())return;guard(true);policy().setLiquidCoolingPresence(false);notifyNativePolicy(false);
    }
    bool nativePolicyOwned()const noexcept override{return policy_&&policy_->liquidCoolingPresenceOwned();}
};
std::mutex sessionGate;std::weak_ptr<LiquidCoolingController> session;
}
bool windows::invokeHydrocLppCondition(bool expectedPresence,const std::function<void(const InventoryRow&)>& emit){
    guard(true);auto ec=createWindowsAcpiDiagnosticBackend();ec->lockTransaction();
    try{
        const auto stable=[&](std::uint16_t address){const auto before=ec->read(address);if(ec->read(address)!=before)throw std::runtime_error("LPP helper: unstable native context");return before;};
        if(stable(0x740)!=26||stable(0x7c3)!=13||!(stable(0x78e)&64)||!(stable(lpp::capability)&lpp::capabilityMask)||
            bool(stable(lpp::presence)&lpp::presenceMask)!=expectedPresence)throw std::runtime_error("LPP helper: actual native LC context/presence predicate mismatch");
    }catch(...){ec->unlockTransaction();throw;}ec->unlockTransaction();
    const auto result=notifyNativePolicyDirect();
    emit({"native_lpp_condition",std::format("WMI native LC condition returned{} after expected7C7.1={}; no EC/BLE writes by helper",result,expectedPresence)});return true;
}
std::shared_ptr<LiquidCoolingController> createLiquidCoolingController(){
    if(wcsstr(GetCommandLineW(),L"--no-ec")||!hydrocProfile(deviceProfile(readBoardDescriptor()).id))return {};
    std::lock_guard lock(sessionGate);if(auto existing=session.lock())return existing;
    auto controller=std::make_shared<LiquidCoolingController>(std::make_unique<Backend>());session=controller;return controller;
}
void suspendLiquidCooling(){std::shared_ptr<LiquidCoolingController> active;{std::lock_guard lock(sessionGate);active=session.lock();}if(active)active->suspend();}
std::string liquidCoolingDiagnosticsJson(){
    using diagnostics::firmwareQuote;std::shared_ptr<LiquidCoolingController> active;{std::lock_guard lock(sessionGate);active=session.lock();}
    if(!active)return "{\"schema\":1,\"backend\":\"windows-lpp-ble\",\"session_created\":false,\"status\":\"No LPP session; diagnostics does not scan/connect/start cooling\"}";
    const auto snapshot=active->tryState();if(!snapshot)return "{\"schema\":1,\"backend\":\"windows-lpp-ble\",\"session_created\":true,\"status\":\"Command pending; cached snapshot temporarily busy, no extra IO performed\"}";
    const auto& s=*snapshot;const auto boolean=[](bool v){return v?"true":"false";};
    const auto pump=s.acceptedPump?"{\"enabled\":"+std::string(boolean(s.acceptedPump->enabled))+",\"duty\":"+std::to_string(s.acceptedPump->duty)+",\"voltage\":"+std::to_string(static_cast<int>(s.acceptedPump->voltage))+"}":"null";
    const auto fan=s.acceptedFan?"{\"enabled\":"+std::string(boolean(s.acceptedFan->enabled))+",\"duty\":"+std::to_string(s.acceptedFan->duty)+"}":"null";
    const bool fresh=s.link.meterState&&s.link.meterAtMs&&nowMs()>=s.link.meterAtMs&&nowMs()-s.link.meterAtMs<=6000;
    return "{\"schema\":1,\"backend\":\"windows-lpp-ble\",\"session_created\":true,\"connected\":"+std::string(boolean(s.link.connected))+
        ",\"transport_connected\":"+boolean(s.link.transportConnected)+",\"notifications_ready\":"+boolean(s.link.notificationsReady)+",\"device_id\":"+firmwareQuote(s.link.deviceId)+
        ",\"device_name\":"+firmwareQuote(s.link.deviceName)+",\"firmware\":"+firmwareQuote(s.link.firmware)+",\"firmware_reply\":"+firmwareQuote(s.link.firmwareReply)+
        ",\"last_notification_hex\":"+firmwareQuote(s.link.lastNotificationHex)+
        ",\"meter_state\":"+(s.link.meterState?std::to_string(*s.link.meterState):"null")+",\"meter_at_monotonic_ms\":"+std::to_string(s.link.meterAtMs)+
        ",\"flow_normal\":"+(fresh?boolean(s.link.meterState==2):"null")+",\"accepted_pump_command\":"+pump+",\"accepted_fan_command\":"+fan+
        ",\"automatic\":"+boolean(s.automatic)+",\"native_policy_owned\":"+boolean(s.nativePolicyOwned)+",\"suspended\":"+boolean(s.suspended)+
        ",\"command_uncertain\":"+boolean(s.commandUncertain)+",\"physical_rpm_duty_readback\":false,\"link_status\":"+firmwareQuote(s.link.status)+",\"status\":"+firmwareQuote(s.status)+"}";
}
}
