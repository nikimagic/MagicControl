#include "MagicControl/GpuDevice.h"
#include "GpuDisplayRoutesWindows.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <dxcore.h>
#include <wrl/client.h>
#include <format>
#include <setupapi.h>
#include <devguid.h>
#include <devpropdef.h>
#include <charconv>
#include <algorithm>
#include <cwctype>
#include <string_view>
#include <set>
namespace mc {
namespace {
std::string utf8(const wchar_t* value){const int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,value,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;}
struct PciNode {std::string instance;std::optional<LUID> luid;std::optional<unsigned> vendor,device,subsystem,revision;GpuPciLocation location;};
struct PciNodes {std::vector<PciNode> values;bool complete{};};
std::optional<unsigned> pciPart(std::string_view text,std::string_view token,std::size_t digits){
    const auto at=text.find(token);if(at==std::string_view::npos||at+token.size()+digits>text.size())return {};
    unsigned value{};auto part=text.substr(at+token.size(),digits);const auto parsed=std::from_chars(part.data(),part.data()+part.size(),value,16);
    return parsed.ec==std::errc{}&&parsed.ptr==part.data()+part.size()?std::optional<unsigned>(value):std::nullopt;
}
PciNodes pciNodes(){
    PciNodes out;bool metadataComplete=true;const auto devices=SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY,nullptr,nullptr,DIGCF_PRESENT);if(devices==INVALID_HANDLE_VALUE)return out;
    struct Owner{HDEVINFO value;~Owner(){SetupDiDestroyDeviceInfoList(value);}} owner{devices};
    // Exact Windows SDK ntddvdeo.h DEVPKEY_Device_AdapterLuid (binary LUID).
    constexpr DEVPROPKEY adapterLuid{{0xc50a3f10,0xaa5c,0x4247,{0xb8,0x30,0xd6,0xa6,0xf8,0xea,0xa3,0x10}},3};
    for(DWORD i=0;i<256;++i){SP_DEVINFO_DATA info{sizeof(info)};if(!SetupDiEnumDeviceInfo(devices,i,&info)){out.complete=GetLastError()==ERROR_NO_MORE_ITEMS&&metadataComplete;break;}
        wchar_t instance[1024]{};if(!SetupDiGetDeviceInstanceIdW(devices,&info,instance,1024,nullptr)){metadataComplete=false;continue;}
        std::wstring wide(instance);std::transform(wide.begin(),wide.end(),wide.begin(),[](wchar_t c){return std::towupper(c);});
        if(!wide.starts_with(L"PCI\\"))continue;
        PciNode row;row.instance=utf8(wide.c_str());row.vendor=pciPart(row.instance,"VEN_",4);row.device=pciPart(row.instance,"DEV_",4);row.subsystem=pciPart(row.instance,"SUBSYS_",8);row.revision=pciPart(row.instance,"REV_",2);
        if(!row.vendor||!row.device||!row.subsystem||!row.revision)metadataComplete=false;
        DEVPROPTYPE propertyType{};DWORD bytes{};LUID luid{};
        if(SetupDiGetDevicePropertyW(devices,&info,&adapterLuid,&propertyType,reinterpret_cast<PBYTE>(&luid),sizeof(luid),&bytes,0)&&propertyType==DEVPROP_TYPE_BINARY&&bytes==sizeof(luid))row.luid=luid;
        const auto number=[&](DWORD property)->std::optional<DWORD>{DWORD value{},type{},count{};
            return SetupDiGetDeviceRegistryPropertyW(devices,&info,property,&type,reinterpret_cast<PBYTE>(&value),sizeof(value),&count)&&type==REG_DWORD&&count==sizeof(value)?std::optional<DWORD>(value):std::nullopt;};
        const auto bus=number(SPDRP_BUSNUMBER),address=number(SPDRP_ADDRESS);
        if(bus&&address&&*bus<=255&&(*address>>16)<=31&&(*address&65535)<=7){row.location.bus=*bus;row.location.device=*address>>16;row.location.function=*address&65535;row.location.source="Windows PCI devnode SPDRP_BUSNUMBER/SPDRP_ADDRESS; segment not reported";}
        out.values.push_back(std::move(row));
    }return out;
}
void bindPhysical(GraphicsAdapter& row,const LUID& luid,const PciNodes& nodes){
    if(!nodes.complete){row.identityStatus="Present PCI devnode enumeration incomplete/unavailable; physical binding refused";return;}
    const PciNode* match{};unsigned matches{};
    for(const auto& node:nodes.values)if(node.luid&&node.luid->HighPart==luid.HighPart&&node.luid->LowPart==luid.LowPart){match=&node;++matches;}
    bool exactLuid=matches==1;
    if(!matches){for(const auto& node:nodes.values)if(node.vendor==row.vendorId&&node.device==row.deviceId&&node.subsystem==row.subsystemId&&node.revision==row.revision){match=&node;++matches;}}
    if(matches!=1){row.identityStatus="Physical PCI binding unavailable/ambiguous; no name matching";return;}
    if(match->vendor!=row.vendorId||match->device!=row.deviceId||match->subsystem!=row.subsystemId||match->revision!=row.revision){row.identityStatus="PCI devnode hardware IDs contradict the DXGI descriptor; physical binding refused";return;}
    row.physicalId="windows-pnp:"+match->instance;row.location=match->location;
    row.identitySource=exactLuid?"DXGI AdapterLuid = present PCI devnode DEVPKEY_Device_AdapterLuid":"Unique present PCI VEN/DEV/SUBSYS/REV tuple; multiple identical devices refused";
    row.identityStatus="Physical PCI devnode matched; LUID is session-scoped";
}
}
std::vector<GraphicsAdapter> graphicsAdapters(){
    using Microsoft::WRL::ComPtr;std::vector<GraphicsAdapter> rows;const auto nodes=pciNodes();
    ComPtr<IDXGIFactory1> factory;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return rows;
    HMODULE module=LoadLibraryExW(L"dxcore.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    ComPtr<IDXCoreAdapterFactory> core;
    if(module){const auto create=reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(GetProcAddress(module,"DXCoreCreateAdapterFactory"));if(create)create(IID_PPV_ARGS(&core));}
    bool enumerationComplete=false,descriptionsComplete=true;
    for(UINT index=0;index<64;++index){ComPtr<IDXGIAdapter1> adapter;const auto enumerated=factory->EnumAdapters1(index,&adapter);
        if(enumerated==DXGI_ERROR_NOT_FOUND){enumerationComplete=descriptionsComplete;break;}if(FAILED(enumerated)||!adapter)break;
        DXGI_ADAPTER_DESC1 d{};if(FAILED(adapter->GetDesc1(&d))){descriptionsComplete=false;continue;}
        GraphicsAdapter r;r.name=utf8(d.Description);r.vendorId=d.VendorId;r.deviceId=d.DeviceId;r.subsystemId=d.SubSysId;r.revision=d.Revision;
        r.id=std::format("luid:{:08x}:{:08x}",static_cast<unsigned>(d.AdapterLuid.HighPart),d.AdapterLuid.LowPart);
        r.pci=std::format("{:04X}:{:04X} subsystem={:08X} rev={:02X}",d.VendorId,d.DeviceId,d.SubSysId,d.Revision);
        // RDP can enumerate a second Microsoft WARP handle without SOFTWARE.
        // Its documented PCI pair is not a physical GPU and must never win
        // the FANs card selection when all hardware load values are unknown.
        r.software=(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0||(d.VendorId==0x1414&&d.DeviceId==0x008c);
        if(!r.software)bindPhysical(r,d.AdapterLuid,nodes);
        r.primarySource="Windows · active display source LUID unavailable";
        r.dedicatedBytes=d.DedicatedVideoMemory;r.sharedBytes=d.SharedSystemMemory;
        if(core&&!r.software){ComPtr<IDXCoreAdapter> a;if(SUCCEEDED(core->GetAdapterByLuid(d.AdapterLuid,IID_PPV_ARGS(&a)))&&a->IsPropertySupported(DXCoreAdapterProperty::IsIntegrated)){
            bool integrated{};if(SUCCEEDED(a->GetProperty(DXCoreAdapterProperty::IsIntegrated,sizeof(integrated),&integrated)))r.type=integrated?GpuType::Integrated:GpuType::Discrete;}}
        LARGE_INTEGER version{};if(SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&version)))r.driver=std::format("{}.{}.{}.{}",(version.HighPart>>16)&65535,version.HighPart&65535,(version.LowPart>>16)&65535,version.LowPart&65535);
        ComPtr<IDXGIAdapter3> memory;if(SUCCEEDED(adapter.As(&memory))){DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if(SUCCEEDED(memory->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))){r.processLocalUsageBytes=info.CurrentUsage;r.processLocalBudgetBytes=info.Budget;}}
        r.status=r.software?"Windows software render adapter; not a physical GPU":r.type==GpuType::Unknown?"DXGI: адаптер есть; DXCore не подтвердил integrated/discrete":"DXGI/DXCore: тип подтверждён; рабочая нагрузка не создавалась";
        rows.push_back(std::move(r));
    }
    if(!enumerationComplete)for(auto& row:rows){row.physicalId.clear();row.identityStatus="DXGI adapter enumeration incomplete; physical grouping refused";}
    // Hybrid DXGI can expose a shared HMONITOR on the rendering dGPU. Bind the
    // primary flag to the existing active source LUID/GDI map instead.
    const auto routes=windows::gpuDisplayRoutes(rows);std::set<std::string> primaryIds;
    for(const auto& route:routes)if(route.primary)primaryIds.insert(route.adapterId);
    if(primaryIds.size()==1)for(auto& row:rows){row.primary=row.id==*primaryIds.begin();row.primaryKnown=true;row.primarySource="Windows · active primary display source LUID";}
    core.Reset();if(module)FreeLibrary(module);return rows;
}
}
