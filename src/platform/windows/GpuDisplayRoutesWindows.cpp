#include "GpuDisplayRoutesWindows.h"
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <format>
#include <map>
#include <set>
namespace mc::windows {
namespace {
std::string utf8(const wchar_t* text){const auto n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,text,-1,out.data(),n,nullptr,nullptr);out.pop_back();return out;}
std::string key(LUID id){return std::format("luid:{:08x}:{:08x}",static_cast<unsigned>(id.HighPart),id.LowPart);}
}
std::vector<GpuDisplayRoute> gpuDisplayRoutes(const std::vector<GraphicsAdapter>& adapters){
    std::wstring primary;
    for(DWORD i=0;i<64;++i){DISPLAY_DEVICEW display{sizeof(display)};if(!EnumDisplayDevicesW(nullptr,i,&display,0))break;
        if(display.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE){primary=display.DeviceName;break;}}
    std::map<std::string,GpuDisplayRoute> routes;std::set<std::string> ambiguous;
    const auto add=[&](const wchar_t* device,LUID source,LUID target,const std::string& api,const std::string& status){
        const auto id=key(source);if(std::none_of(adapters.begin(),adapters.end(),[&](const auto& a){return !a.software&&a.id==id;}))return;
        const auto name=utf8(device);if(name.empty())return;
        GpuDisplayRoute row{name,id,key(target),api,status,!primary.empty()&&!_wcsicmp(device,primary.c_str())};
        const auto old=routes.find(name);
        if(old!=routes.end()){
            if(old->second.adapterId!=row.adapterId){ambiguous.insert(name);return;}
            // Clone targets can share a single GDI source. It remains one route.
            if(old->second.targetAdapterId!=row.targetAdapterId)old->second.targetAdapterId.clear();
            return;
        }
        routes.emplace(name,std::move(row));
    };
    LONG error=ERROR_GEN_FAILURE;
    for(int attempt=0;attempt<3;++attempt){UINT32 pathCount{},modeCount{};
        error=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount);if(error!=ERROR_SUCCESS)break;
        if(pathCount>128||modeCount>512){error=ERROR_NOT_ENOUGH_MEMORY;break;}
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        error=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(error==ERROR_INSUFFICIENT_BUFFER)continue;if(error!=ERROR_SUCCESS)break;
        for(UINT32 i=0;i<pathCount;++i){const auto& path=paths[i];DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(name),path.sourceInfo.adapterId,path.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&name.header)!=ERROR_SUCCESS)continue;
            add(name.viewGdiDeviceName,path.sourceInfo.adapterId,path.targetInfo.adapterId,"Windows · QueryDisplayConfig source LUID/GDI",path.sourceInfo.adapterId.HighPart==path.targetInfo.adapterId.HighPart&&path.sourceInfo.adapterId.LowPart==path.targetInfo.adapterId.LowPart?"":"Source/target adapter differ; firmware MUX mode unknown");
        }
        break;
    }
    if(routes.empty()){
        // Read-only fallback reports attached DXGI outputs, never render-device
        // preference or adapter0. No synthetic workload is created to wake dGPU.
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))for(UINT i=0;i<64;++i){Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if(FAILED(factory->EnumAdapters1(i,&adapter)))break;DXGI_ADAPTER_DESC1 desc{};if(FAILED(adapter->GetDesc1(&desc))||(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            for(UINT j=0;j<32;++j){Microsoft::WRL::ComPtr<IDXGIOutput> output;if(FAILED(adapter->EnumOutputs(j,&output)))break;DXGI_OUTPUT_DESC d{};
                if(SUCCEEDED(output->GetDesc(&d))&&d.AttachedToDesktop)add(d.DeviceName,desc.AdapterLuid,desc.AdapterLuid,"Windows · DXGI attached output",std::format("QueryDisplayConfig unavailable/empty ({})",error));}
        }
    }
    std::vector<GpuDisplayRoute> result;
    for(auto& [device,route]:routes)if(!ambiguous.contains(device))result.push_back(std::move(route));
    return result;
}
}
