#include "WmiWindows.h"
#include "MagicControl/ActionLog.h"
#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <chrono>
#include <cwchar>
#include <map>
#include <mutex>
#include <limits>
#include "ControlWindows.h"
namespace mc::windows::legion {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("Lenovo WMI HRESULT="+std::to_string(static_cast<unsigned long>(h)));}
std::wstring wide(const char* s){std::wstring r;for(;*s;++s)r+=static_cast<unsigned char>(*s);return r;}
std::string narrow(const wchar_t* s){std::string r;for(;*s;++s){if(*s>127)throw std::runtime_error("Lenovo: non-ASCII WMI schema property");r+=static_cast<char>(*s);}return r;}
struct Bstr{BSTR p;explicit Bstr(const wchar_t* s):p(SysAllocString(s)){if(!p)throw std::bad_alloc();}~Bstr(){SysFreeString(p);}};
struct Value{VARIANT v{};~Value(){VariantClear(&v);}};
struct Apartment {
    HRESULT status{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    Apartment(){if(FAILED(status)&&status!=RPC_E_CHANGED_MODE)check(status);}
    ~Apartment(){if(SUCCEEDED(status))CoUninitialize();}
};
std::uint32_t number(VARIANT& v){
    if(v.vt==VT_UI4)return v.ulVal;if(v.vt==VT_I4)return static_cast<std::uint32_t>(v.lVal);
    if(v.vt==VT_UI2)return v.uiVal;if(v.vt==VT_I2&&v.iVal>=0)return v.iVal;
    if(v.vt==VT_UI1)return v.bVal;if(v.vt==VT_BOOL)return v.boolVal?1:0;
    throw std::runtime_error("Lenovo: unexpected WMI numeric type");
}
std::vector<std::uint32_t> values(VARIANT& v){
    if(v.vt==VT_NULL||v.vt==VT_EMPTY)return {};
    if(!(v.vt&VT_ARRAY))return {number(v)};
    if(!v.parray||SafeArrayGetDim(v.parray)!=1)throw std::runtime_error("Lenovo: invalid WMI array");
    LONG lo{},hi{};check(SafeArrayGetLBound(v.parray,1,&lo));check(SafeArrayGetUBound(v.parray,1,&hi));
    if(hi<lo||hi-lo>=4096)throw std::runtime_error("Lenovo: oversized WMI array");
    std::vector<std::uint32_t> result;const auto type=v.vt&VT_TYPEMASK;
    for(LONG i=lo;i<=hi;++i){Value item;item.v.vt=type;
        switch(type){case VT_UI1:check(SafeArrayGetElement(v.parray,&i,&item.v.bVal));break;
        case VT_UI2:check(SafeArrayGetElement(v.parray,&i,&item.v.uiVal));break;
        case VT_UI4:check(SafeArrayGetElement(v.parray,&i,&item.v.ulVal));break;
        case VT_I4:check(SafeArrayGetElement(v.parray,&i,&item.v.lVal));break;
        default:throw std::runtime_error("Lenovo: unknown WMI array element type");}
        result.push_back(number(item.v));}
    return result;
}
struct ClassGuid{const char* name;const wchar_t* guid;};
constexpr ClassGuid guids[]{
 {"LENOVO_GAMEZONE_DATA",L"887B54E3-DDDC-4B2C-8B88-68A26A8835D0"},
 {"LENOVO_OTHER_METHOD",L"DC2A8805-3A8C-41BA-A6F7-092E0089CD3B"},
 {"LENOVO_FAN_METHOD",L"92549549-4BDE-4F06-AC04-CE8BF898DBAA"},
 {"LENOVO_CAPABILITY_DATA_01",L"7A8F5407-CB67-4D6E-B547-39B3BE018154"},
 {"LENOVO_DISCRETE_DATA",L"91433B17-B7B7-4640-BB40-34C67349FBEC"},
 {"LENOVO_FAN_TABLE_DATA",L"87FB2A6D-D802-48E7-9208-4576C5F5C8D8"}};
bool numericType(VARTYPE t){return t==VT_UI1||t==VT_UI2||t==VT_UI4||t==VT_I2||t==VT_I4||t==VT_BOOL;}
}
struct Session::Impl {
    Apartment apartment;
    ComPtr<IWbemServices> service;
    std::map<std::string,ComPtr<IWbemClassObject>> classes;
    std::map<std::string,std::vector<std::wstring>> instances;
    Impl(){
        static std::once_flag security;std::call_once(security,[]{const auto r=CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);if(FAILED(r)&&r!=RPC_E_TOO_LATE)check(r);});
        ComPtr<IWbemLocator> locator;check(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator)));
        Bstr ns(L"ROOT\\WMI");check(locator->ConnectServer(ns.p,nullptr,nullptr,nullptr,WBEM_FLAG_CONNECT_USE_MAX_WAIT,nullptr,nullptr,&service));
        check(CoSetProxyBlanket(service.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE));
    }
    ~Impl(){classes.clear();service.Reset();}
    IWbemClassObject* cls(const char* name){
        if(auto i=classes.find(name);i!=classes.end())return i->second.Get();
        const auto expected=std::find_if(std::begin(guids),std::end(guids),[&](const auto& g){return std::string_view(g.name)==name;});
        if(expected==std::end(guids))throw std::runtime_error("Lenovo: WMI class is outside protocol allowlist");
        ComPtr<IWbemClassObject> c;const auto w=wide(name);Bstr key(w.c_str());check(service->GetObject(key.p,0,nullptr,&c,nullptr));
        ComPtr<IWbemQualifierSet> q;check(c->GetQualifierSet(&q));Value v;check(q->Get(L"GUID",0,&v.v,nullptr));
        if(v.v.vt!=VT_BSTR)throw std::runtime_error("Lenovo: class GUID is missing");
        std::wstring id=v.v.bstrVal;if(id.starts_with(L"{")&&id.ends_with(L"}"))id=id.substr(1,id.size()-2);
        if(_wcsicmp(id.c_str(),expected->guid))throw std::runtime_error("Lenovo: firmware WMI class GUID mismatch");
        return classes.emplace(name,std::move(c)).first->second.Get();
    }
    lenovo::Row decode(IWbemClassObject* object,bool cap=false){
        lenovo::Row result;check(object->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY));
        try{for(;;){BSTR property{};Value value;CIMTYPE type{};const auto r=object->Next(0,&property,&value.v,&type,nullptr);if(r==WBEM_S_NO_MORE_DATA)break;check(r);
            std::wstring key(property,SysStringLen(property));SysFreeString(property);
            if(numericType(value.v.vt&VT_TYPEMASK)){
                auto data=values(value.v);if(!data.empty())result.emplace(narrow(key.c_str()),data);
                if(cap){ComPtr<IWbemQualifierSet> q;Value id;if(SUCCEEDED(object->GetPropertyQualifierSet(key.c_str(),&q))&&SUCCEEDED(q->Get(L"WmiDataId",0,&id.v,nullptr))&&number(id.v)==2)result["_flags"]=std::move(data);}
            }
        }}catch(...){object->EndEnumeration();throw;}check(object->EndEnumeration());return result;
    }
    std::vector<lenovo::Row> query(const char* name){
        cls(name);const auto sql=L"SELECT * FROM "+wide(name);Bstr lang(L"WQL"),q(sql.c_str());ComPtr<IEnumWbemClassObject> it;
        check(service->ExecQuery(lang.p,q.p,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&it));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);std::vector<lenovo::Row> result;std::vector<std::wstring> paths;
        while(true){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Lenovo: WMI enumeration timed out");
            ComPtr<IWbemClassObject> o;ULONG count{};const auto r=it->Next(200,1,&o,&count);check(r);if(!count){if(r==WBEM_S_TIMEDOUT)continue;break;}
            if(result.size()>=4096)throw std::runtime_error("Lenovo: WMI instance count limit");
            Value active;if(SUCCEEDED(o->Get(L"Active",0,&active.v,nullptr,nullptr))&&active.v.vt==VT_BOOL&&!active.v.boolVal)continue;
            Value path;check(o->Get(L"__PATH",0,&path.v,nullptr,nullptr));if(path.v.vt!=VT_BSTR)throw std::runtime_error("Lenovo: WMI instance path missing");
            paths.emplace_back(path.v.bstrVal);result.push_back(decode(o.Get(),std::string_view(name)=="LENOVO_CAPABILITY_DATA_01"));
        }
        instances[name]=std::move(paths);return result;
    }
    lenovo::Row call(const char* name,const char* method,const lenovo::Row& fields){
        auto c=cls(name);if(!instances.contains(name))query(name);const auto& paths=instances.at(name);
        if(paths.size()!=1)throw std::runtime_error("Lenovo: firmware method instance is not unique");
        const auto m=wide(method);Bstr wm(m.c_str());ComPtr<IWbemClassObject> schema,input;check(c->GetMethod(wm.p,0,&schema,nullptr));
        if(schema)check(schema->SpawnInstance(0,&input));
        for(const auto& [key,data]:fields){if(!input||data.empty()||data.size()>256)throw std::runtime_error("Lenovo: invalid method input");const auto k=wide(key.c_str());Value expected,value;CIMTYPE type{};
            check(schema->Get(k.c_str(),0,&expected.v,&type,nullptr));
            const auto base=type&~CIM_FLAG_ARRAY;
            if(type&CIM_FLAG_ARRAY){VARTYPE vt=base==CIM_UINT8?VT_UI1:base==CIM_UINT16?VT_UI2:base==CIM_UINT32?VT_UI4:VT_EMPTY;
                if(vt==VT_EMPTY)throw std::runtime_error("Lenovo: incompatible method array type");
                value.v.vt=vt|VT_ARRAY;value.v.parray=SafeArrayCreateVector(vt,0,static_cast<ULONG>(data.size()));if(!value.v.parray)throw std::bad_alloc();
                for(LONG i=0;i<static_cast<LONG>(data.size());++i){auto n=data[i];if((vt==VT_UI1&&n>255)||(vt==VT_UI2&&n>65535))throw std::runtime_error("Lenovo: method array overflow");check(SafeArrayPutElement(value.v.parray,&i,&n));}
            }else {if(data.size()!=1||(base!=CIM_UINT8&&base!=CIM_UINT16&&base!=CIM_UINT32))throw std::runtime_error("Lenovo: incompatible scalar method schema");
                if((base==CIM_UINT8&&data[0]>255)||(base==CIM_UINT16&&data[0]>65535))throw std::runtime_error("Lenovo: scalar method overflow");value.v.vt=VT_I4;value.v.lVal=static_cast<LONG>(data[0]);}
            check(input->Put(k.c_str(),0,&value.v,0));
        }
        Bstr path(paths[0].c_str());ComPtr<IWbemClassObject> output;
        if(std::string_view(method).starts_with("Get")){ComPtr<IWbemCallResult> pending;
            check(service->ExecMethod(path.p,wm.p,WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,input.Get(),nullptr,&pending));
            if(!pending)throw std::runtime_error("Lenovo: asynchronous WMI getter result missing");
            const auto status=pending->GetResultObject(8000,&output);if(status==WBEM_S_TIMEDOUT)throw std::runtime_error("Lenovo: WMI getter timed out after8s");check(status);
        }else check(service->ExecMethod(path.p,wm.p,0,nullptr,input.Get(),&output,nullptr));
        if(!output)return {};auto result=decode(output.Get());
        for(const char* key:{"ReturnValue","ret"})if(auto i=result.find(key);i!=result.end()&&!i->second.empty()&&i->second[0])throw std::runtime_error("Lenovo: firmware returned a nonzero method status");
        return result;
    }
};
Session::Session():impl_(std::make_unique<Impl>()){}Session::~Session()=default;
std::vector<lenovo::Row> Session::query(const char* c){return impl_->query(c);}
lenovo::Row Session::call(const char* c,const char* m,const lenovo::Row& in){return impl_->call(c,m,in);}
int Session::mode(){return static_cast<int>(lenovo::scalar(call("LENOVO_GAMEZONE_DATA","GetSmartFanMode"),"Data"));}
void Session::mode(int v){if(v!=1&&v!=2&&v!=3&&v!=255)throw std::runtime_error("Lenovo: mode outside enabled protocol");call("LENOVO_GAMEZONE_DATA","SetSmartFanMode",{{"Data",{static_cast<std::uint32_t>(v)}}});}
int Session::feature(std::uint32_t id){return static_cast<int>(lenovo::scalar(call("LENOVO_OTHER_METHOD","GetFeatureValue",{{"IDs",{id}}}),"Value"));}
void Session::feature(std::uint32_t id,int value){if(value<0)throw std::runtime_error("Lenovo: negative feature value");call("LENOVO_OTHER_METHOD","SetFeatureValue",{{"IDs",{id}},{"value",{static_cast<std::uint32_t>(value)}}});}
lenovo::Catalog Session::catalog(){return lenovo::Catalog::decode(query("LENOVO_CAPABILITY_DATA_01"),query("LENOVO_DISCRETE_DATA"));}
std::string readWmiEvidence(){
    if(!legionHostSupported())throw std::runtime_error("Lenovo: WMI diagnostic identity unavailable");Session s;
    std::string out="{\"schema\":1,\"source\":\"actual ROOT/WMI instances, SKU-selected by firmware\",\"write_commands\":0,\"mode\":"+std::to_string(s.mode());
    for(const auto cls:{"LENOVO_CAPABILITY_DATA_01","LENOVO_DISCRETE_DATA","LENOVO_FAN_TABLE_DATA"}){
        out+=",\""+std::string(cls)+"\":[";bool first=true;for(const auto& row:s.query(cls)){
            if(!first)out+=',';first=false;out+='{';bool fieldFirst=true;
            for(const auto& [key,values]:row){if(!fieldFirst)out+=',';fieldFirst=false;out+=audit::quote(key)+":[";
                for(std::size_t i=0;i<values.size();++i){if(i)out+=',';out+=std::to_string(values[i]);}out+=']';}out+='}';}out+=']';
    }
    out+=",\"current_features\":{";bool first=true;
    const auto add=[&](std::uint32_t id){if(!first)out+=',';first=false;out+=audit::quote(std::to_string(id))+':'+std::to_string(s.feature(id));};
    for(const auto id:lenovo::cpuIds)add(id);for(const auto& option:lenovo::optionIds)add(option.id);
    add(lenovo::fanFullSpeedId);
    return out+"}}\n";
}
}
