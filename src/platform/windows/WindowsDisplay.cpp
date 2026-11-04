#include "WindowsDisplay.h"
#include "../common/DisplayEvidence.h"
#include <shellscalingapi.h>

#include <windows.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <vector>
#include <cwchar>
#include <string_view>
#include <iterator>
#include <utility>

namespace mc::windows {
ActiveDisplayConfiguration readActiveDisplayConfiguration(std::stop_token token){
    ActiveDisplayConfiguration out;
    for(int attempt=0;attempt<3;++attempt){
        if(token.stop_requested()){out.error=ERROR_CANCELLED;break;}
        out.paths.clear();out.modes.clear();
        UINT32 paths{},modes{};out.error=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths,&modes);
        if(out.error!=ERROR_SUCCESS)break;
        if(paths>128||modes>1024){out.error=ERROR_BUFFER_OVERFLOW;out.status="Display path/mode safety limit exceeded";return out;}
        if(!paths){out.complete=true;out.status="No active desktop display paths";return out;}
        out.paths.resize(paths);out.modes.resize(modes);
        out.error=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths,out.paths.data(),&modes,out.modes.data(),nullptr);
        if(out.error==ERROR_INSUFFICIENT_BUFFER)continue;
        if(out.error!=ERROR_SUCCESS)break;
        out.paths.resize(paths);out.modes.resize(modes);out.complete=true;out.status="Active current desktop paths captured";return out;
    }
    out.paths.clear();out.modes.clear();if(out.status.empty())out.status="Current display configuration unavailable (Win32="+std::to_string(out.error)+")";return out;
}
namespace {
std::wstring builtInDisplayName() {
        const auto current=readActiveDisplayConfiguration();
        if(!current.complete)throw std::runtime_error(std::format("Не удалось прочитать подключения экранов (код {}).",current.error));
        for (const auto& path:current.paths) {
            const auto technology = path.targetInfo.outputTechnology;
            if (technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS &&
                technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED &&
                technology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL) continue;
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            name.header.size = sizeof(name);
            name.header.adapterId = path.sourceInfo.adapterId; name.header.id = path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS && name.viewGdiDeviceName[0])
                return name.viewGdiDeviceName;
        }
    throw std::runtime_error("Встроенный экран не активен или Windows не сообщила его имя.");
}
DEVMODEW readMode(const std::wstring& device) {
    DEVMODEW mode{}; mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &mode,0))
        throw std::runtime_error("Windows не вернула текущую ориентацию встроенного экрана.");
    return mode;
}
std::string displayUtf8(std::wstring_view value){
    const int count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string out(count,0);if(count)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),count,nullptr,nullptr);return out;
}
struct DesktopMonitor {HMONITOR handle{};MONITORINFOEXW info{};};
struct DesktopMonitors {std::vector<DesktopMonitor> values;DWORD error{};};
BOOL CALLBACK collectDesktopMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM context){
    auto& out=*reinterpret_cast<DesktopMonitors*>(context);DesktopMonitor entry;entry.handle=monitor;entry.info.cbSize=sizeof(entry.info);
    if(!GetMonitorInfoW(monitor,&entry.info)){out.error=GetLastError();return TRUE;}
    if(out.values.size()>=128){out.error=ERROR_BUFFER_OVERFLOW;return FALSE;}
    try{out.values.push_back(entry);}catch(...){out.error=ERROR_NOT_ENOUGH_MEMORY;return FALSE;}return TRUE;
}
const DesktopMonitor* monitorFor(const DesktopMonitors& monitors,std::wstring_view device){
    const DesktopMonitor* found{};for(const auto& monitor:monitors.values)if(_wcsicmp(monitor.info.szDevice,std::wstring(device).c_str())==0){if(found)return nullptr;found=&monitor;}return found;
}
}
std::string displayDiagnosticJson(std::stop_token token){
    using namespace diagnostics;DisplayEvidence out;out.source="Windows QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS) + EnumDisplaySettingsEx(ENUM_CURRENT_SETTINGS)";
    const auto current=readActiveDisplayConfiguration(token);out.enumerationComplete=current.complete;out.cancelled=token.stop_requested();out.status=current.status;
    if(!current.complete)out.issues.push_back({"enumeration","QueryDisplayConfig",current.status,"win32",static_cast<std::uint32_t>(current.error)});
    if(out.cancelled)return displayEvidenceJson(out);
    DesktopMonitors monitors;SetLastError(ERROR_SUCCESS);
    const bool monitorEnumeration=EnumDisplayMonitors(nullptr,nullptr,collectDesktopMonitor,reinterpret_cast<LPARAM>(&monitors))!=FALSE;
    if(!monitorEnumeration&&!monitors.error)monitors.error=GetLastError();
    if(!monitorEnumeration||monitors.error)out.issues.push_back({"monitor_mapping","EnumDisplayMonitors/GetMonitorInfo", "Monitor list incomplete or unavailable; zero error means no extended error reported","win32",monitors.error});
    struct Library{HMODULE handle{};~Library(){if(handle)FreeLibrary(handle);}} shcore{LoadLibraryExW(L"Shcore.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)};
    const auto shcoreError=shcore.handle?ERROR_SUCCESS:GetLastError();
    using ScaleFn=HRESULT(WINAPI*)(HMONITOR,DEVICE_SCALE_FACTOR*);
    using ProcessAwarenessFn=HRESULT(WINAPI*)(HANDLE,PROCESS_DPI_AWARENESS*);
    const auto scale=shcore.handle?reinterpret_cast<ScaleFn>(GetProcAddress(shcore.handle,"GetScaleFactorForMonitor")):nullptr;
    const auto awareness=shcore.handle?reinterpret_cast<ProcessAwarenessFn>(GetProcAddress(shcore.handle,"GetProcessDpiAwareness")):nullptr;
    PROCESS_DPI_AWARENESS processAwareness=PROCESS_DPI_UNAWARE;const auto awarenessResult=awareness?awareness(nullptr,&processAwareness):E_NOTIMPL;
    using ThreadContextFn=HANDLE(WINAPI*)();using ThreadAwarenessFn=int(WINAPI*)(HANDLE);
    const auto user32=GetModuleHandleW(L"user32.dll");
    const auto context=reinterpret_cast<ThreadContextFn>(GetProcAddress(user32,"GetThreadDpiAwarenessContext"));
    const auto contextAwareness=reinterpret_cast<ThreadAwarenessFn>(GetProcAddress(user32,"GetAwarenessFromDpiAwarenessContext"));
    const auto threadAwareness=context&&contextAwareness?contextAwareness(context()):-1;
    const auto add=[&](std::string id,std::wstring device,const DISPLAYCONFIG_PATH_INFO* path){
        DisplayReading row;row.id=std::move(id);row.name=displayUtf8(device);row.active=true;row.source=out.source;
        row.status="Current OS evidence; fields with issues remain unavailable";
        if(path){DISPLAYCONFIG_TARGET_DEVICE_NAME target{};target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),path->targetInfo.adapterId,path->targetInfo.id};
            const auto result=DisplayConfigGetDeviceInfo(&target.header);
            if(result==ERROR_SUCCESS){const auto friendly=displayUtf8(std::wstring_view(target.monitorFriendlyDeviceName,wcsnlen_s(target.monitorFriendlyDeviceName,std::size(target.monitorFriendlyDeviceName))));
                if(!friendly.empty())row.name=friendly;
                row.connected=path->targetInfo.targetAvailable!=FALSE;
                row.metadataJson="{\"targetDevicePath\":"+firmwareQuote(displayUtf8(std::wstring_view(target.monitorDevicePath,wcsnlen_s(target.monitorDevicePath,std::size(target.monitorDevicePath)))))+",\"outputTechnology\":"+std::to_string(path->targetInfo.outputTechnology)+",";}
            else{row.metadataJson="{";row.issues.push_back({"name","DisplayConfigGetDeviceInfo(GET_TARGET_NAME)","Friendly name unavailable","win32",static_cast<std::uint32_t>(result)});}
            const auto rate=path->targetInfo.refreshRate;
            if(rate.Numerator&&rate.Denominator){row.refreshNumerator=rate.Numerator;row.refreshDenominator=rate.Denominator;row.refreshHz=double(rate.Numerator)/rate.Denominator;row.refreshSource="QueryDisplayConfig current path targetInfo.refreshRate (rational; not frame rate)";}
            else row.issues.push_back({"refreshHz","QueryDisplayConfig", "Current target refresh rational is zero/invalid",{}, {}});
        }else row.metadataJson="{";
        row.metadataJson+="\"gdiDeviceName\":"+firmwareQuote(displayUtf8(device))+",\"primaryScope\":\"GDI desktop source; clone targets may share primary\",\"dpiAwarenessProcess\":"+(SUCCEEDED(awarenessResult)?std::to_string(processAwareness):"null")+",\"dpiAwarenessThread\":"+(threadAwareness>=0?std::to_string(threadAwareness):"null")+"}";
        DEVMODEW mode{};mode.dmSize=sizeof(mode);
        if(!device.empty()&&EnumDisplaySettingsExW(device.c_str(),ENUM_CURRENT_SETTINGS,&mode,0)){
            if((mode.dmFields&(DM_PELSWIDTH|DM_PELSHEIGHT))==(DM_PELSWIDTH|DM_PELSHEIGHT)&&mode.dmPelsWidth&&mode.dmPelsHeight){row.pixelWidth=mode.dmPelsWidth;row.pixelHeight=mode.dmPelsHeight;row.modeSource="EnumDisplaySettingsEx(ENUM_CURRENT_SETTINGS); physical pixels, not DPI virtualized";}
            if(!path&&(mode.dmFields&DM_DISPLAYFREQUENCY)&&mode.dmDisplayFrequency>1){row.refreshHz=mode.dmDisplayFrequency;row.refreshNumerator=mode.dmDisplayFrequency;row.refreshDenominator=1;row.refreshSource="EnumDisplaySettingsEx current integer frequency; precision limited";}
        }else row.issues.push_back({"pixelWidth/pixelHeight","EnumDisplaySettingsEx(ENUM_CURRENT_SETTINGS)",device.empty()?"Source device name unavailable":"Getter failed; this API does not document an extended error",{}, {}});
        if(!row.pixelWidth&&path&&path->sourceInfo.modeInfoIdx<current.modes.size()){
            const auto& value=current.modes[path->sourceInfo.modeInfoIdx];
            if(value.infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE&&value.id==path->sourceInfo.id&&value.adapterId.HighPart==path->sourceInfo.adapterId.HighPart&&value.adapterId.LowPart==path->sourceInfo.adapterId.LowPart&&value.sourceMode.width&&value.sourceMode.height){row.pixelWidth=value.sourceMode.width;row.pixelHeight=value.sourceMode.height;row.modeSource="QueryDisplayConfig current SOURCE_MODE; source desktop pixels";}}
        if(!row.pixelWidth)row.issues.push_back({"pixelWidth/pixelHeight","current display getters","No validated current physical/source dimensions",{}, {}});
        if(!row.refreshHz)row.issues.push_back({"refreshHz","current display getters","No validated current refresh rate; EDID preferred not substituted",{}, {}});
        if(const auto* monitor=monitorFor(monitors,device)){
            row.primary=(monitor->info.dwFlags&MONITORINFOF_PRIMARY)!=0;
            if(scale){DEVICE_SCALE_FACTOR factor{};const auto result=scale(monitor->handle,&factor);
                if(result==S_OK&&int(factor)>0&&int(factor)<=1000){row.scalePercent=int(factor);row.scaleSource="Shcore.GetScaleFactorForMonitor; successful reported DEVICE_SCALE_FACTOR";
                    row.dpiX=row.dpiY=double(int(factor))*96.0/100.0;row.dpiSource="Derived from reported scale factor * 96/100; not raw panel DPI or independent custom-override measurement";}
                else row.issues.push_back({"scalePercent/effectiveDpi","GetScaleFactorForMonitor","Failed/invalid scale; API fallback output discarded","hresult",static_cast<std::uint32_t>(result)});
            }else row.issues.push_back({"scalePercent/effectiveDpi","Shcore.GetScaleFactorForMonitor","API unavailable","win32",static_cast<std::uint32_t>(shcore.handle?ERROR_PROC_NOT_FOUND:shcoreError)});
        }else{row.issues.push_back({"primary/scalePercent/effectiveDpi","EnumDisplayMonitors + GetMonitorInfo","No unique HMONITOR matched the exact GDI source device",{}, {}});}
        out.displays.push_back(std::move(row));
    };
    for(const auto& path:current.paths){if(token.stop_requested()){out.cancelled=true;out.enumerationComplete=false;break;}
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
        const auto result=DisplayConfigGetDeviceInfo(&source.header);
        const auto id=std::format("{:08X}:{:08X}:{}",static_cast<unsigned>(path.targetInfo.adapterId.HighPart),path.targetInfo.adapterId.LowPart,path.targetInfo.id);
        add(id,result==ERROR_SUCCESS?std::wstring(source.viewGdiDeviceName):std::wstring{},&path);
        if(result!=ERROR_SUCCESS)out.displays.back().issues.push_back({"gdiDeviceName","DisplayConfigGetDeviceInfo(GET_SOURCE_NAME)","Source name unavailable","win32",static_cast<std::uint32_t>(result)});
    }
    if(!current.complete&&!token.stop_requested())for(const auto& monitor:monitors.values){
        if(token.stop_requested()){out.cancelled=true;break;}add(displayUtf8(monitor.info.szDevice),monitor.info.szDevice,nullptr);}
    out.cancelled=out.cancelled||token.stop_requested();
    if(out.cancelled){out.enumerationComplete=false;out.status="Display capture cancelled; retained partial OS evidence";}
    out.metadataJson="{\"edidPreferredUsedAsCurrent\":false,\"modeQueriesUsePhysicalPixels\":true,\"scaleCaveat\":\"GetScaleFactorForMonitor reports an enum factor; exact custom override not independently verified. GetDpiForMonitor is not used because its result depends on process/thread awareness. EffectiveDpi here is explicitly derived.\",\"refreshCaveat\":\"Configured OS path frequency, not instantaneous VRR/frame presentation; snapshot non-atomic\"}";
    return displayEvidenceJson(out);
}
std::string toggleBuiltInDisplayRotation() {
    const auto device = builtInDisplayName();
    auto mode = readMode(device);
    const DWORD before = mode.dmDisplayOrientation;
    if (before > DMDO_270) throw std::runtime_error("Windows сообщила неизвестную ориентацию экрана.");
    const DWORD wanted = (before + 2) % 4;
    mode.dmDisplayOrientation = wanted; mode.dmFields |= DM_DISPLAYORIENTATION;
    LONG result = ChangeDisplaySettingsExW(device.c_str(),&mode,nullptr,CDS_TEST,nullptr);
    if (result == DISP_CHANGE_SUCCESSFUL)
        result = ChangeDisplaySettingsExW(device.c_str(),&mode,nullptr,0,nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL)
        throw std::runtime_error(std::format("Драйвер отклонил поворот экрана (код {}).",result));
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (readMode(device).dmDisplayOrientation == wanted)
            return wanted == DMDO_DEFAULT ? "Обычная ориентация · подтверждено Windows" :
                std::format("Ориентация {}° · подтверждено Windows",wanted*90);
        Sleep(50);
    }
    throw std::runtime_error("Windows не подтвердила поворот встроенного экрана чтением состояния.");
}

}
