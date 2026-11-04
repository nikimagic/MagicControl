#include "MagicControl/ActionLog.h"
#include "../../../protocols/huawei/Hwmi.h"
#include "MagicControl/Logging.h"
#include "HuaweiWmiGate.h"
#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <string>
#include <mutex>

namespace mc::huawei {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT result){if(FAILED(result))throw std::runtime_error("Huawei WMI недоступен, HRESULT="+std::to_string(static_cast<unsigned long>(result)));}
struct Apartment {
    HRESULT result{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    Apartment(){if(FAILED(result)&&result!=RPC_E_CHANGED_MODE)checked(result);}
    ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
};
struct Variant {VARIANT value;Variant(){VariantInit(&value);}~Variant(){VariantClear(&value);}};
struct Bstr {BSTR value;explicit Bstr(const wchar_t* text):value(SysAllocString(text)){if(!value)throw std::bad_alloc();}~Bstr(){SysFreeString(value);}};
class WindowsSession final:public Session {
    Apartment apartment;
    WmiGate gate;
    ComPtr<IWbemLocator> locator;
    ComPtr<IWbemServices> service;
    ComPtr<IWbemClassObject> inputDefinition;
    std::wstring objectPath;
    bool byteInput{};
public:
    WindowsSession(){
        checked(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator)));
        Bstr ns(L"ROOT\\WMI");checked(locator->ConnectServer(ns.value,nullptr,nullptr,nullptr,0,nullptr,nullptr,&service));
        checked(CoSetProxyBlanket(service.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE));
        Bstr clsName(L"OemWMIMethod"),method(L"OemWMIfun");ComPtr<IWbemClassObject> cls;
        checked(service->GetObject(clsName.value,0,nullptr,&cls,nullptr));
        ComPtr<IWbemQualifierSet> qualifiers;
        if(SUCCEEDED(cls->GetQualifierSet(&qualifiers))){Variant guid;
            if(SUCCEEDED(qualifiers->Get(L"GUID",0,&guid.value,nullptr))&&guid.value.vt==VT_BSTR){
                std::wstring value(guid.value.bstrVal);if(value.starts_with(L"{"))value=value.substr(1,value.size()-2);
                if(_wcsicmp(value.c_str(),L"ABBC0F5B-8EA1-11D1-A000-C90629100000")!=0)throw std::runtime_error("Huawei: GUID класса WMI не совпадает");}}
        checked(cls->GetMethod(method.value,0,&inputDefinition,nullptr));
        Variant value;CIMTYPE type{};const auto bytes=inputDefinition->Get(L"u8Input",0,&value.value,&type,nullptr);
        if(SUCCEEDED(bytes)){
            if(type!=(CIM_UINT8|CIM_FLAG_ARRAY))throw std::runtime_error("Huawei: неизвестный тип u8Input");byteInput=true;
        }else {checked(inputDefinition->Get(L"u64Input",0,&value.value,&type,nullptr));if(type!=CIM_UINT64)throw std::runtime_error("Huawei: неизвестный тип u64Input");}
        ComPtr<IEnumWbemClassObject> instances;Bstr wql(L"WQL"),query(L"SELECT * FROM OemWMIMethod");
        checked(service->ExecQuery(wql.value,query.value,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&instances));
        std::vector<std::wstring> all,hwmi;
        for(int i=0;i<8;++i){ComPtr<IWbemClassObject> item;ULONG count{};const auto result=instances->Next(3000,1,&item,&count);
            checked(result);if(result==WBEM_S_TIMEDOUT)throw std::runtime_error("Huawei: время перечисления WMI истекло");if(!count)break;
            Variant path,name,active;checked(item->Get(L"__PATH",0,&path.value,nullptr,nullptr));if(path.value.vt!=VT_BSTR)continue;
            if(SUCCEEDED(item->Get(L"Active",0,&active.value,nullptr,nullptr))&&active.value.vt==VT_BOOL&&!active.value.boolVal)continue;
            std::wstring candidate(path.value.bstrVal);all.push_back(candidate);
            if(SUCCEEDED(item->Get(L"InstanceName",0,&name.value,nullptr,nullptr))&&name.value.vt==VT_BSTR&&std::wstring(name.value.bstrVal).find(L"HWMI")!=std::wstring::npos)hwmi.push_back(candidate);
        }
        if(hwmi.size()==1)objectPath=hwmi.front();else if(hwmi.empty()&&all.size()==1)objectPath=all.front();
        else throw std::runtime_error("Huawei: не найден однозначный активный экземпляр HWMI");
        static std::once_flag logged;
        std::call_once(logged,[this]{logMessage("huawei_hwmi",byteInput?"Transport: OemWMIMethod.OemWMIfun u8Input[64], u8Output status at 0":"Transport: OemWMIMethod.OemWMIfun u64Input, u8Output status at 0");});
    }
    std::vector<std::uint8_t> call(const Command& command) override {
        audit::Scope trace("huawei.hwmi_call","\"request_hex\":"+audit::quote(audit::hex(command)),0,audit::current()!=0);
        ComPtr<IWbemClassObject> input;checked(inputDefinition->SpawnInstance(0,&input));Variant value;
        if(byteInput){
            value.value.vt=VT_ARRAY|VT_UI1;value.value.parray=SafeArrayCreateVector(VT_UI1,0,static_cast<ULONG>(command.size()));
            if(!value.value.parray)throw std::bad_alloc();void* buffer{};checked(SafeArrayAccessData(value.value.parray,&buffer));
            std::memcpy(buffer,command.data(),command.size());checked(SafeArrayUnaccessData(value.value.parray));
            checked(input->Put(L"u8Input",0,&value.value,0));
        }else {
            std::uint64_t packed{};for(unsigned i=0;i<8;++i)packed|=std::uint64_t(command[i])<<(8*i);
            value.value.vt=VT_BSTR;value.value.bstrVal=SysAllocString(std::to_wstring(packed).c_str());if(!value.value.bstrVal)throw std::bad_alloc();
            checked(input->Put(L"u64Input",0,&value.value,0));
        }
        Bstr path(objectPath.c_str()),method(L"OemWMIfun");ComPtr<IWbemCallResult> pending;
        checked(service->ExecMethod(path.value,method.value,WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,input.Get(),nullptr,&pending));
        LONG callStatus{};const auto status=pending->GetCallStatus(5000,&callStatus);
        if(status==WBEM_S_TIMEDOUT)throw std::runtime_error("Huawei: время WMI-вызова истекло; состояние неизвестно");checked(status);checked(callStatus);
        ComPtr<IWbemClassObject> output;checked(pending->GetResultObject(0,&output));Variant bytes;
        checked(output->Get(L"u8Output",0,&bytes.value,nullptr,nullptr));
        if(bytes.value.vt!=(VT_ARRAY|VT_UI1)||!bytes.value.parray||SafeArrayGetDim(bytes.value.parray)!=1)throw std::runtime_error("Huawei: неизвестный формат u8Output");
        LONG lo{},hi{};checked(SafeArrayGetLBound(bytes.value.parray,1,&lo));checked(SafeArrayGetUBound(bytes.value.parray,1,&hi));
        if(hi<lo||hi-lo+1>256)throw std::runtime_error("Huawei: неверная длина ответа");
        void* data{};checked(SafeArrayAccessData(bytes.value.parray,&data));
        std::vector<std::uint8_t> result(static_cast<std::uint8_t*>(data),static_cast<std::uint8_t*>(data)+(hi-lo+1));
        checked(SafeArrayUnaccessData(bytes.value.parray));
        trace.finish("transport_returned","\"response_hex\":"+audit::quote(audit::hex(result))+",\"response_bytes\":"+std::to_string(result.size()));return result;
    }
};
}
std::unique_ptr<Session> openSession(){return std::make_unique<WindowsSession>();}
void withWmiGate(const std::function<void()>& action){WmiGate gate;action();}
}
