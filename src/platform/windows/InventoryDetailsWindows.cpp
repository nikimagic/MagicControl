#include "InventoryDetailsWindows.h"
#include "WmiQueryWindows.h"
#include "WindowsDisplay.h"
#include "MagicControl/PawnIoInstall.h"
#include "MagicControl/LightingZones.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <windows.h>
#include <winioctl.h>
#include <cctype>
#include <climits>
#include <wbemidl.h>
#include <wrl/client.h>
#include <setupapi.h>
#include <poclass.h>
#include <tbs.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <set>
#include <cwctype>

namespace mc {
namespace {
using Microsoft::WRL::ComPtr;
using Rows = std::vector<InventoryRow>;
using namespace windows::wmi;
std::string unit(std::optional<double> value, const char* suffix, double divisor = 1) {
    return value ? std::format("{:g} {}", *value / divisor, suffix) : kInventoryUnknown;
}
std::string bytes(std::optional<double> n) { return n&&std::isfinite(*n)&&*n>=0 ? std::format("{:.2f} ГиБ", *n / 1073741824.0) : kInventoryUnknown; }
std::vector<DXGI_ADAPTER_DESC1> graphicsMemory(){
    // WMI AdapterRAM is uint32 and can overflow on a discrete GPU. DXGI uses
    // pointer-sized byte counts; no D3D device or vendor utility is created.
    ComPtr<IDXGIFactory1> factory;std::vector<DXGI_ADAPTER_DESC1> values;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return values;
    for(UINT i=0;i<64;++i){ComPtr<IDXGIAdapter1> adapter;const auto result=factory->EnumAdapters1(i,&adapter);
        if(result==DXGI_ERROR_NOT_FOUND)break;if(FAILED(result))break;
        DXGI_ADAPTER_DESC1 description{};
        if(SUCCEEDED(adapter->GetDesc1(&description))&&!(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))values.push_back(description);
    }
    return values;
}
const DXGI_ADAPTER_DESC1* graphicsMemoryFor(const std::vector<DXGI_ADAPTER_DESC1>& adapters,std::string pnp){
    std::transform(pnp.begin(),pnp.end(),pnp.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
    const DXGI_ADAPTER_DESC1* matched=nullptr;
    for(const auto& adapter:adapters){
        if(pnp.find(std::format("VEN_{:04X}",adapter.VendorId))==std::string::npos||
           pnp.find(std::format("DEV_{:04X}",adapter.DeviceId))==std::string::npos||
           pnp.find(std::format("SUBSYS_{:08X}",adapter.SubSysId))==std::string::npos)continue;
        if(matched)return nullptr; // Two identical PCI IDs are ambiguous; do not guess by index/name.
        matched=&adapter;
    }
    return matched;
}
std::string date(const std::string& n) {
    return n.size() >= 8 && n.find_first_not_of("0123456789", 0) >= 8 ?
        n.substr(6, 2) + "." + n.substr(4, 2) + "." + n.substr(0, 4) : kInventoryUnknown;
}
void set(Rows& rows, std::string name, std::string value) {
    auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.name == name; });
    if (it != rows.end()) it->value = std::move(value); else rows.push_back({std::move(name), std::move(value)});
}
template<class Fn> void section(HardwareInventory& inventory, const char* title, Fn&& fn) {
    Rows rows;
    try { rows = fn(); if (rows.empty()) rows.push_back({"Сведения", "Windows не вернула сведения этого раздела."}); }
    catch (const std::exception& error) { rows.push_back({"Сведения недоступны", error.what()}); }
    inventory.sections.push_back({title, std::move(rows)});
}
Rows tpm(Wmi& wmi) {
    try {
        const auto data = wmi.query(L"SELECT ManufacturerIdTxt,ManufacturerVersion,SpecVersion,IsEnabled_InitialValue,IsActivated_InitialValue FROM Win32_Tpm",
            {L"ManufacturerIdTxt", L"ManufacturerVersion", L"SpecVersion", L"IsEnabled_InitialValue", L"IsActivated_InitialValue"},
            L"ROOT\\CIMV2\\Security\\MicrosoftTpm");
        if (!data.empty()) {
            const auto& r = data.front();
            return {{"Производитель", r.get(L"ManufacturerIdTxt")}, {"Версия прошивки TPM", r.get(L"ManufacturerVersion")},
                {"Спецификация", r.get(L"SpecVersion")}, {"Включён / активирован", r.yes(L"IsEnabled_InitialValue") + " / " + r.yes(L"IsActivated_InitialValue")}};
        }
    } catch (...) { /* TBS presence query remains available without TPM WMI permissions. */ }
    TPM_DEVICE_INFO info{};
    const TBS_RESULT result = Tbsi_GetDeviceInfo(sizeof(info), &info);
    if (result == TBS_SUCCESS) return {{"TPM", "Обнаружен службой Windows TBS"},
        {"Спецификация", info.tpmVersion == TPM_VERSION_20 ? "2.0" : info.tpmVersion == TPM_VERSION_12 ? "1.2" : kInventoryUnknown},
        {"Подробности TPM", "WMI не вернула подробности. Для чтения производителя и прошивки могут требоваться права администратора."}};
    if (result == TBS_E_TPM_NOT_FOUND) return {{"TPM", "Служба Windows TBS не обнаружила TPM."}};
    return {{"Состояние TPM", "Не удалось определить: " + failure(static_cast<HRESULT>(result))}};
}
Rows metadataRows(const wchar_t* operation,std::stop_token cancellation,const wchar_t* scope=L"MAGICCONTROL\\READONLY") {
    Rows rows;QueryCompletion completion;
    const auto records=isolatedQuery(operation,{L"Name",L"Value"},scope,std::chrono::seconds(20),cancellation,completion);
    for(const auto& record:records)rows.push_back({record.get(L"Name"),record.get(L"Value")});
    if(!completion.complete)rows.push_back({"Полнота метаданных",completion.status});
    return rows;
}
std::string chipsetPackageVersion(const std::string& vendor,std::stop_token cancellation) {
    // A chipset package contains several independently versioned drivers.
    // Report its registered package version, never an arbitrary PCI component.
    struct Key {HKEY value{};~Key(){if(value)RegCloseKey(value);}};
    const auto text=[](HKEY key,const wchar_t* name){
        wchar_t value[2048]{};DWORD bytes=sizeof(value);
        return RegGetValueW(key,nullptr,name,RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS?
            std::wstring(value):std::wstring{};
    };
    const auto lower=[](std::wstring value){for(auto& ch:value)ch=std::towlower(ch);return value;};
    std::set<std::string> versions;
    for(const auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){
        Key root;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ|view,&root.value)!=ERROR_SUCCESS)continue;
        for(DWORD index=0;index<16384&&!cancellation.stop_requested();++index){
            wchar_t name[256]{};DWORD count=256;const auto status=RegEnumKeyExW(root.value,index,name,&count,nullptr,nullptr,nullptr,nullptr);
            if(status==ERROR_NO_MORE_ITEMS)break;if(status!=ERROR_SUCCESS)continue;
            Key item;if(RegOpenKeyExW(root.value,name,0,KEY_READ|view,&item.value)!=ERROR_SUCCESS)continue;
            const auto package=lower(text(item.value,L"DisplayName"));
            const bool match=vendor=="AMD"?(package.find(L"amd")!=std::wstring::npos&&package.find(L"chipset")!=std::wstring::npos):
                vendor=="Intel"?(package.find(L"intel")!=std::wstring::npos&&package.find(L"chipset")!=std::wstring::npos):false;
            if(!match)continue;const auto version=text(item.value,L"DisplayVersion");
            if(!version.empty())versions.insert(utf8(version));
        }
    }
    std::string result;for(const auto& version:versions){if(!result.empty())result+=" / ";result+=version;}
    return result.empty()?std::string(kInventoryUnknown):result;
}
Rows compactDrivers(const HardwareInventory& inventory,std::stop_token cancellation) {
    Rows result;std::string vendor;
    for(const auto& section:inventory.sections){
        if(section.title=="Процессор")for(const auto& row:section.rows)if(row.name=="Производитель"){
            if(row.value.find("AMD")!=std::string::npos)vendor="AMD";
            else if(row.value.find("Intel")!=std::string::npos)vendor="Intel";
        }
        if(section.title!="Графика")continue;
        std::string model,version,pci,maker;
        const auto append=[&]{
            if(pci.find("VEN_10DE")!=std::string::npos||pci.find("VEN_1002")!=std::string::npos||pci.find("VEN_8086")!=std::string::npos||
               maker.find("NVIDIA")!=std::string::npos||maker.find("Intel")!=std::string::npos||maker.starts_with("AMD")||maker.starts_with("Advanced Micro Devices"))
                result.push_back({"Видеодрайвер · "+model,version.empty()?kInventoryUnknown:version});
        };
        for(const auto& row:section.rows){
            if(row.name=="Модель"){if(!model.empty())append();model=row.value;version.clear();pci.clear();maker.clear();}
            else if(row.name=="Производитель")maker=row.value;
            else if(row.name=="Драйвер")version=row.value;
            else if(row.name=="Идентификатор устройства"){
                pci=row.value;std::transform(pci.begin(),pci.end(),pci.begin(),[](unsigned char ch){return static_cast<char>(std::toupper(ch));});
            }
        }
        if(!model.empty())append();
    }
    try{const auto status=dependencies::pawnIoDriverStatus(dependencies::PawnIoVersionQuery::LoadedDriver);
        result.push_back({"PawnIO",status.installed?(status.version().empty()?"Установлен · версия недоступна":"Установлен · "+std::string(status.version())):"Не установлен"});
        if(status.installed){
            result.push_back({"PawnIO · источник версии",std::string(status.versionSource())});
            result.push_back({"PawnIO · версия SYS-файла",status.fileVersion.empty()?"Нет VERSIONINFO / "+status.versionStatus:status.fileVersion});
            result.push_back({"PawnIO · версия загруженного драйвера",status.loadedVersion.empty()?status.loadedVersionStatus:status.loadedVersion});
        }
    }
    catch(...){result.push_back({"PawnIO",kInventoryUnknown});}
    result.push_back({vendor.empty()?"Пакет чипсета Intel/AMD":"Пакет чипсета "+vendor,chipsetPackageVersion(vendor,cancellation)});
    return result;
}
Rows battery(std::stop_token cancellation) {
    Rows rows;SYSTEM_POWER_STATUS power{};
    if(GetSystemPowerStatus(&power)){
        rows.push_back({"Питание",power.ACLineStatus==1?"От сети":power.ACLineStatus==0?"От батареи":kInventoryUnknown});
        rows.push_back({"Заряд",power.BatteryLifePercent<=100?std::format("{} %",power.BatteryLifePercent):kInventoryUnknown});
        if(power.BatteryFlag==128)rows.push_back({"Батарея","Windows сообщает отсутствие батареи."});
    }
    // Stream every physical system battery from a bounded read-only helper.
    // A stale aggregate PowerStatus never hides an available interface.
    auto detail=metadataRows(L"SELECT Name,Value FROM MagicControlSystemBatteries",cancellation);
    rows.insert(rows.end(),std::make_move_iterator(detail.begin()),std::make_move_iterator(detail.end()));
    return rows;
}
}

std::optional<std::vector<InventoryRow>> activeDisplayRows(const std::string& panelId) {
    const auto current=windows::readActiveDisplayConfiguration();
    if(!current.complete)return std::nullopt;
    for (const auto& path:current.paths) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId, path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
        std::string name = utf8(target.monitorDevicePath);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        if (name.find("#" + panelId + "#") == std::string::npos) continue;
        Rows rows;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id};
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS && EnumDisplaySettingsExW(source.viewGdiDeviceName, ENUM_CURRENT_SETTINGS, &mode, 0))
            rows.push_back({"Текущий режим Windows", std::format("{} × {} · {} Гц", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency)});
        const auto output = path.targetInfo.outputTechnology;
        rows.push_back({"Подключение", output == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL || output == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
            output == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED ? "Встроенный" : output == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI ? "HDMI" :
            output == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL ? "DisplayPort" : "Внешний"});
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
        color.header = {DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), path.targetInfo.adapterId, path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) {
            rows.push_back({"Глубина цвета Windows", std::format("{} бит на канал", color.bitsPerColorChannel)});
            const char* encoding = color.colorEncoding == DISPLAYCONFIG_COLOR_ENCODING_RGB ? "RGB" :
                color.colorEncoding == DISPLAYCONFIG_COLOR_ENCODING_YCBCR444 ? "YCbCr 4:4:4" :
                color.colorEncoding == DISPLAYCONFIG_COLOR_ENCODING_YCBCR422 ? "YCbCr 4:2:2" :
                color.colorEncoding == DISPLAYCONFIG_COLOR_ENCODING_YCBCR420 ? "YCbCr 4:2:0" : kInventoryUnknown;
            rows.push_back({"Формат цвета Windows", encoding});
            rows.push_back({"Расширенный цвет Windows", color.advancedColorSupported ?
                color.advancedColorEnabled ? "Поддерживается, включён" : "Поддерживается, выключен" : "Не поддерживается в текущей конфигурации"});
        } else rows.push_back({"Цветовой сигнал Windows", "DisplayConfig не вернул сведения."});
        return rows;
    }
    return Rows{};
}

void appendWindowsDetails(HardwareInventory& inventory, std::stop_token cancellation) {
    // COM lifetime surrounds all section queries; one failing provider never erases other sections.
    std::unique_ptr<Wmi> wmi;
    std::string initializationError;
    try { wmi = std::make_unique<Wmi>(cancellation); } catch (const std::exception& e) { initializationError = e.what(); }
    const auto available = [&]() -> Wmi& { if (!wmi) throw std::runtime_error(initializationError); return *wmi; };
    if (wmi && !inventory.sections.empty()) {
        try {
            auto bios = wmi->query(L"SELECT SMBIOSMajorVersion,SMBIOSMinorVersion FROM Win32_BIOS", {L"SMBIOSMajorVersion",L"SMBIOSMinorVersion"});
            if (!bios.empty()) set(inventory.sections[0].rows, "Версия SMBIOS", bios[0].get(L"SMBIOSMajorVersion") + "." + bios[0].get(L"SMBIOSMinorVersion"));
        } catch (...) {}
    }
    section(inventory, "TPM", [&] { return tpm(available()); });
    section(inventory, "Процессор", [&] {
        Rows rows;
        for (const auto& r : available().query(L"SELECT Name,Manufacturer,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,SocketDesignation,L2CacheSize,L3CacheSize,VirtualizationFirmwareEnabled FROM Win32_Processor",
             {L"Name",L"Manufacturer",L"NumberOfCores",L"NumberOfLogicalProcessors",L"MaxClockSpeed",L"SocketDesignation",L"L2CacheSize",L"L3CacheSize",L"VirtualizationFirmwareEnabled"})) {
            rows.insert(rows.end(), {{"Модель",r.get(L"Name")},{"Производитель",r.get(L"Manufacturer")},
                {"Ядра / потоки",r.get(L"NumberOfCores") + " / " + r.get(L"NumberOfLogicalProcessors")},
                {"Частота из SMBIOS",unit(r.number(L"MaxClockSpeed"),"МГц")},{"Сокет",r.get(L"SocketDesignation")},
                {"Кэш L2 / L3",unit(r.number(L"L2CacheSize"),"МиБ",1024) + " / " + unit(r.number(L"L3CacheSize"),"МиБ",1024)},
                {"Виртуализация в прошивке",r.yes(L"VirtualizationFirmwareEnabled")}});
        }
        rows.push_back({"О частоте","Частота из SMBIOS не является текущей частотой или гарантированным пределом буста."}); return rows;
    });
    section(inventory, "Графика", [&] {
        Rows rows;
        const auto adapters=graphicsMemory();
        for (const auto& r : available().query(L"SELECT Name,AdapterCompatibility,VideoProcessor,DriverVersion,DriverDate,PNPDeviceID FROM Win32_VideoController",
            {L"Name",L"AdapterCompatibility",L"VideoProcessor",L"DriverVersion",L"DriverDate",L"PNPDeviceID"})){
            const auto* memory=graphicsMemoryFor(adapters,r.get(L"PNPDeviceID"));
            rows.insert(rows.end(), {{"Модель",r.get(L"Name")},{"Производитель",r.get(L"AdapterCompatibility")},
                {"Графический процессор",r.get(L"VideoProcessor")},{"Драйвер",r.get(L"DriverVersion")},
                {"Дата драйвера",date(r.get(L"DriverDate"))},
                {"Выделенная видеопамять",memory?bytes(static_cast<double>(memory->DedicatedVideoMemory)):kInventoryUnknown},
                {"Общая системная память (лимит)",memory?bytes(static_cast<double>(memory->SharedSystemMemory)):kInventoryUnknown},
                {"Источник данных памяти",memory?"DXGI · точное соответствие PCI VEN/DEV/SUBSYS":"DXGI: адаптер не сопоставлен однозначно"},
                {"Идентификатор устройства",r.get(L"PNPDeviceID")}});
        }
        rows.push_back({"О видеопамяти","DXGI разделяет выделенную видеопамять и лимит общей системной памяти. Это ёмкости, а не текущая свободная память; общая память не прибавляется к VRAM."}); return rows;
    });
    section(inventory, "Оперативная память", [&] {
        auto modules = available().query(L"SELECT Manufacturer,PartNumber,SerialNumber,Capacity,Speed,ConfiguredClockSpeed,SMBIOSMemoryType,FormFactor,DeviceLocator,BankLabel,DataWidth,TotalWidth,ConfiguredVoltage FROM Win32_PhysicalMemory",
            {L"Manufacturer",L"PartNumber",L"SerialNumber",L"Capacity",L"Speed",L"ConfiguredClockSpeed",L"SMBIOSMemoryType",L"FormFactor",L"DeviceLocator",L"BankLabel",L"DataWidth",L"TotalWidth",L"ConfiguredVoltage"});
        if (modules.empty()) return Rows{{"Модули памяти","WMI не вернула сведения о модулях."}};
        double total = 0; bool complete = true;
        for (const auto& r : modules) { if (auto n = r.number(L"Capacity")) total += *n; else complete = false; }
        Rows rows{{"Установлено модулей",std::to_string(modules.size())},{"Всего",complete ? bytes(total) : kInventoryUnknown}};
        int index = 0;
        for (const auto& r : modules) {
            const auto type = r.integer(L"SMBIOSMemoryType"), form = r.integer(L"FormFactor");
            rows.insert(rows.end(), {{"Модуль " + std::to_string(++index) + " · " + r.get(L"DeviceLocator"),r.get(L"PartNumber")},
                {"Серийный номер",r.get(L"SerialNumber")},{"Производитель",r.get(L"Manufacturer")},{"Объём",bytes(r.number(L"Capacity"))},
                {"Банк из SMBIOS",r.get(L"BankLabel")},{"Тип",type == 34 ? "DDR5" : type == 26 ? "DDR4" : type == 35 ? "LPDDR5" : type == 30 ? "LPDDR4" : r.get(L"SMBIOSMemoryType")},
                {"Формат",form == 12 ? "SO-DIMM" : form == 8 ? "DIMM" : r.get(L"FormFactor")},
                {"Номинальная / настроенная скорость",unit(r.number(L"Speed"),"MT/s") + " / " + unit(r.number(L"ConfiguredClockSpeed"),"MT/s")},
                {"Ширина данных / общая",r.get(L"DataWidth") + " / " + r.get(L"TotalWidth") + " бит"},
                {"Настроенное напряжение",unit(r.number(L"ConfiguredVoltage"),"В",1000)}});
        } return rows;
    });
    section(inventory, "Накопители", [&] {
        std::vector<Record> disks;
        try { disks = available().query(L"SELECT DeviceId,FriendlyName,FirmwareVersion,Size,BusType,MediaType,HealthStatus FROM MSFT_PhysicalDisk",
            {L"DeviceId",L"FriendlyName",L"FirmwareVersion",L"Size",L"BusType",L"MediaType",L"HealthStatus"}, L"ROOT\\Microsoft\\Windows\\Storage"); } catch (...) {}
        Rows rows; int i = 0;
        if (!disks.empty()) {
            std::sort(disks.begin(),disks.end(),[](const auto& a,const auto& b){return a.integer(L"DeviceId") < b.integer(L"DeviceId");});
            rows.push_back({"Установлено накопителей",std::to_string(disks.size())});
            for (const auto& r : disks) {
                const int bus = r.integer(L"BusType"), media = r.integer(L"MediaType"), health = r.integer(L"HealthStatus");
                rows.insert(rows.end(),{{"Накопитель " + std::to_string(++i),r.get(L"FriendlyName")},{"Номер диска Windows",r.get(L"DeviceId")},
                    {"Прошивка диска",r.get(L"FirmwareVersion")},{"Объём",bytes(r.number(L"Size")) + " · " + unit(r.number(L"Size"),"ГБ",1e9)},
                    {"Подключение",bus == 17 ? "NVMe" : bus == 11 ? "SATA" : bus == 7 ? "USB" : bus == 10 ? "SAS" : r.get(L"BusType")},
                    {"Тип носителя",media == 4 ? "SSD" : media == 3 ? "HDD" : kInventoryUnknown},
                    {"Состояние Windows",health == 0 ? "Исправен" : health == 1 ? "Предупреждение" : health == 2 ? "Неисправен" : kInventoryUnknown}});
            }
        } else {
            for (const auto& r : available().query(L"SELECT Model,FirmwareRevision,Size,InterfaceType,Status FROM Win32_DiskDrive",{L"Model",L"FirmwareRevision",L"Size",L"InterfaceType",L"Status"}))
                rows.insert(rows.end(),{{"Накопитель " + std::to_string(++i),r.get(L"Model")},{"Прошивка диска",r.get(L"FirmwareRevision")},
                    {"Объём",bytes(r.number(L"Size"))},{"Интерфейс WMI",r.get(L"InterfaceType")},{"Состояние Windows",r.get(L"Status")}});
        } return rows;
    });
    section(inventory, "Батарея", [&] { return battery(cancellation); });
    section(inventory,"Драйверы",[&]{return compactDrivers(inventory,cancellation);});
    section(inventory, "Windows", [&] {
        auto systems = available().query(L"SELECT Caption,Version,BuildNumber,OSArchitecture,LastBootUpTime FROM Win32_OperatingSystem",
            {L"Caption",L"Version",L"BuildNumber",L"OSArchitecture",L"LastBootUpTime"});
        if (systems.empty()) return Rows{};
        const auto& r = systems.front();
        const auto boot = r.get(L"LastBootUpTime");
        wchar_t release[128]{}; DWORD releaseSize = sizeof(release), revision{}, revisionSize = sizeof(revision);
        constexpr auto versionKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        const bool hasRelease = RegGetValueW(HKEY_LOCAL_MACHINE, versionKey, L"DisplayVersion", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
            nullptr, release, &releaseSize) == ERROR_SUCCESS;
        const bool hasRevision = RegGetValueW(HKEY_LOCAL_MACHINE, versionKey, L"UBR", RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY,
            nullptr, &revision, &revisionSize) == ERROR_SUCCESS;
        const std::string build = r.get(L"BuildNumber") + (hasRevision ? "." + std::to_string(revision) : "");

        return Rows{{"Система",r.get(L"Caption")},{"Версия",r.get(L"Version")},{"Выпуск / сборка", (hasRelease ? utf8(release) + " · " : "") + build},
            {"Архитектура",r.get(L"OSArchitecture")},{"Последняя загрузка",date(boot) + (boot.size() >= 14 ? " " + boot.substr(8,2) + ":" + boot.substr(10,2) + ":" + boot.substr(12,2) : "")}};
    });
    if (cancellation.stop_requested()) throw std::runtime_error("Сбор отменён");
}
void appendWindowsDiagnosticDetails(HardwareInventory& inventory,std::stop_token cancellation) {
    if(cancellation.stop_requested())return;
    // Only an explicit dump calls this method. Ordinary INFO refresh remains
    // descriptor/OS metadata only and never requests HID firmware.
    if(hydrocProfile(deviceProfile(readBoardDescriptor()).id)){
        section(inventory,lightingEvidenceSectionTitle,[&]{
            return metadataRows(L"SELECT Name,Value FROM MagicControlIteLightingEvidence",cancellation,L"MAGICCONTROL\\HID_VERSION_QUERY");
        });
    }
    if(cancellation.stop_requested())return;
    section(inventory,"Нативные ACPI/HID интерфейсы и драйверы",[&]{
        return metadataRows(L"SELECT Name,Value FROM MagicControlNativeInterfaces",cancellation);
    });
    if(cancellation.stop_requested())return;
    section(inventory,"Сеть · звук · камера · NPU",[&]{
        Rows rows;
        Wmi wmi(cancellation);
        for(const auto& r:wmi.query(L"SELECT DeviceName,Manufacturer,DriverVersion FROM Win32_PnPSignedDriver WHERE DeviceClass='NET' OR DeviceClass='MEDIA' OR DeviceClass='CAMERA' OR DeviceClass='BLUETOOTH' OR DeviceClass='COMPUTEACCELERATOR' OR DeviceName LIKE '%IPU%' OR DeviceName LIKE '%NPU%'",
            {L"DeviceName",L"Manufacturer",L"DriverVersion"})){
            const auto name=r.get(L"DeviceName");
            if(name==kInventoryUnknown||name.starts_with("WAN Miniport")||name.starts_with("Минипорт WAN"))continue;
            rows.push_back({name,r.get(L"Manufacturer")+" · драйвер "+r.get(L"DriverVersion")});
        }
        return rows;
    });
}
}
