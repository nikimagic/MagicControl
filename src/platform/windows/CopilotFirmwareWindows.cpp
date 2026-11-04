#include "CopilotFirmwareWindows.h"
#include "KeysHardwareWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/QuickSwitch.h"
#include "MagicControl/Logging.h"
#include "UniwillControlOwnerWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <stdexcept>
namespace mc::windows {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT result,const char* operation){if(FAILED(result))throw std::runtime_error(std::string(operation)+" HRESULT="+std::to_string(static_cast<unsigned long>(result)));}
struct Apartment {
    bool release{};
    Apartment(){const auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);release=SUCCEEDED(result);if(result!=RPC_E_CHANGED_MODE)checked(result,"Copilot COM");}
    ~Apartment(){if(release)CoUninitialize();}
};
struct Bstr {BSTR value;explicit Bstr(const wchar_t* text):value(SysAllocString(text)){if(!value)throw std::bad_alloc();}~Bstr(){SysFreeString(value);}operator BSTR()const{return value;}Bstr(const Bstr&)=delete;};
struct Variant {VARIANT value;Variant(){VariantInit(&value);}~Variant(){VariantClear(&value);}};
// Exact OEM5.57 UEFI_Firmware.exe B90A4416... exports GetCopilotKey RVA20F0,
// SetCopilotKey RVA2310: 64-byte u8Input [03,05,0...] / [03,06,bool,0...].
// Discover the firmware interface, not a displayed laptop model. No OEM EXE is run.
std::array<std::uint8_t,64> oemKeyboardCall(std::uint8_t command,bool locked=false){
    Apartment apartment;
    const auto security=CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);
    if(security!=RPC_E_TOO_LATE)checked(security,"Copilot COM security");
    ComPtr<IWbemLocator> locator;checked(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator)),"Copilot locator");
    ComPtr<IWbemServices> service;Bstr space(L"ROOT\\WMI");
    checked(locator->ConnectServer(space,nullptr,nullptr,nullptr,0,nullptr,nullptr,&service),"Copilot WMI connect");
    checked(CoSetProxyBlanket(service.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE),"Copilot WMI proxy");
    ComPtr<IWbemClassObject> definition,inputType,outputType,input,output;Bstr className(L"OemWMIMethod"),method(L"OemWMIfun");
    checked(service->GetObject(className,0,nullptr,&definition,nullptr),"Copilot OEM class");
    checked(definition->GetMethod(method,0,&inputType,&outputType),"Copilot OEM method");
    if(!inputType||!outputType)throw std::runtime_error("Copilot OEM method schema unavailable");
    for(auto item:{std::pair{inputType.Get(),L"u8Input"},std::pair{outputType.Get(),L"u8Output"}}){
        Variant value;CIMTYPE type{};checked(item.first->Get(item.second,0,&value.value,&type,nullptr),"Copilot OEM parameter");
        if(type!=(CIM_UINT8|CIM_FLAG_ARRAY))throw std::runtime_error("Copilot OEM keyboard ABI mismatch");
    }
    checked(inputType->SpawnInstance(0,&input),"Copilot OEM input");
    Variant packet;packet.value.vt=VT_ARRAY|VT_UI1;packet.value.parray=SafeArrayCreateVector(VT_UI1,0,64);
    if(!packet.value.parray)throw std::bad_alloc();
    void* bytes{};checked(SafeArrayAccessData(packet.value.parray,&bytes),"Copilot input bytes");
    std::array<std::uint8_t,64> data{};data[0]=3;data[1]=command;if(command==6)data[2]=locked?1:0;
    std::copy(data.begin(),data.end(),static_cast<std::uint8_t*>(bytes));SafeArrayUnaccessData(packet.value.parray);
    checked(input->Put(L"u8Input",0,&packet.value,CIM_UINT8|CIM_FLAG_ARRAY),"Copilot OEM input put");
    Bstr instance(L"OemWMIMethod.InstanceName='ACPI\\PNP0C14\\HWMI_0'");ComPtr<IWbemCallResult> result;
    checked(service->ExecMethod(instance,method,WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,input.Get(),&output,&result),"Copilot OEM execute");
    if(!output){if(!result)throw std::runtime_error("Copilot OEM result unavailable");const auto status=result->GetResultObject(1500,&output);if(status==WBEM_S_TIMEDOUT)throw std::runtime_error("Copilot OEM timeout");checked(status,"Copilot OEM result");}
    if(!output)throw std::runtime_error("Copilot OEM empty result");
    Variant response;checked(output->Get(L"u8Output",0,&response.value,nullptr,nullptr),"Copilot OEM output");
    if(response.value.vt!=(VT_ARRAY|VT_UI1)||!response.value.parray||SafeArrayGetDim(response.value.parray)!=1)throw std::runtime_error("Copilot OEM output type");
    LONG lower{},upper{};checked(SafeArrayGetLBound(response.value.parray,1,&lower),"Copilot lower bound");checked(SafeArrayGetUBound(response.value.parray,1,&upper),"Copilot upper bound");
    if(upper-lower+1!=64)throw std::runtime_error("Copilot OEM output length");
    checked(SafeArrayAccessData(response.value.parray,&bytes),"Copilot output bytes");
    std::copy_n(static_cast<const std::uint8_t*>(bytes),64,data.begin());SafeArrayUnaccessData(response.value.parray);return data;
}
std::optional<bool> readOemCopilot(){const auto data=oemKeyboardCall(5);if(data[0]||data[1]>1)return {};return data[1]!=0;}
void requireOwner(){const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("Copilot firmware is owned by OEM: "+owner);}
}
namespace {
enum class Transport {Unavailable,Ec,TypedEc,OemWmi};
Transport transport(const KeysHardwareContext& c){
    if(c.preview||wcsstr(GetCommandLineW(),L"--no-ec"))return Transport::Unavailable;
    if(uniwillCopilotEcProfile(c.profile)){
        if(c.ec&&c.ec->boardVerified())return Transport::Ec;
        if(c.native)return Transport::TypedEc;
        return Transport::Unavailable;
    }
    // Other machines may expose the source-verified OEM keyboard ABI. Its
    // schema/status is checked by the getter; no foreign EC map is inferred.
    return Transport::OemWmi;
}
const char* source(Transport t){return t==Transport::Ec?"Uniwill EC · Copilot":
    t==Transport::TypedEc?"Uniwill typed EC · Copilot":t==Transport::OemWmi?"OEM keyboard WMI · Copilot":"Нет подтверждённого аппаратного обработчика Copilot";}
std::optional<bool> read(Transport t,const KeysHardwareContext& c){
    switch(t){case Transport::Ec:return mc::readCopilotKey(*c.ec);
    case Transport::TypedEc:return c.native->readCopilotKey();
    case Transport::OemWmi:return readOemCopilot();default:return {};}
}
}
KeyObservation observeCopilotFirmware(const KeysHardwareContext& c){
    const auto t=transport(c);
    try{const auto value=read(t,c);const bool writable=value.has_value()&&(t!=Transport::Ec||c.ec->info().writable);
        // Effective Off can mean APP presence is absent while728.2 is still
        // latched. Reconcile once on restore so saved Off clears that latent bit.
        return {value,t!=Transport::Unavailable&&value.has_value(),writable,source(t),
            (t==Transport::Ec||t==Transport::TypedEc)&&value==std::optional<bool>(false)};}
    catch(const std::exception& error){return {{},t==Transport::Ec||t==Transport::TypedEc,false,error.what()};}
}
void applyCopilotFirmware(const KeysHardwareContext& c,bool locked){
    const auto t=transport(c);if(t==Transport::Unavailable)throw std::runtime_error(source(t));
    requireOwner();const auto before=read(t,c);
    if(!before)throw std::runtime_error("Copilot: аппаратное состояние недоступно");
    switch(t){
    case Transport::Ec:mc::setCopilotKey(*c.ec,locked);break;
    case Transport::TypedEc:c.native->setCopilotKey(locked);break;
    case Transport::OemWmi:if(*before!=locked&&oemKeyboardCall(6,locked)[0])throw std::runtime_error("OEM Copilot rejected setter");break;
    default:throw std::runtime_error(source(t));
    }
    if(read(t,c)!=std::optional<bool>(locked))throw std::runtime_error("Copilot: итоговое состояние не подтверждено прошивкой");
}
}
