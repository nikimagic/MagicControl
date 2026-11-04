#include "MagicControl/PortableSettings.h"
#include "MagicControl/FileStorage.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/KeyboardBacklight.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "MagicControl/FanCurve.h"
#include "MagicControl/FanControlLimits.h"
#include "MagicControl/HardwareInventory.h"

namespace mc {
namespace {
const std::vector<std::string> settingNames{
    "battery-aging-override.state",
    "battery-aging-original.state",
    "app-settings.ini","device-settings.ini","keys-settings.ini","fan-curves.csv","keyboard-cycle.ini","wujie-usb-priority.ini",
    "gm6ir0c-device-settings.ini","gm6ir0c-power.txt","gm6ir0c-fans.txt",
    "gm6ir0c-custom-presets.txt","gm6ir0c-configuration.txt",
    "gm5hg0a-device-settings.ini","gm5hg0a-power.txt","l16-device-settings.ini",
    "l16-power.txt","l16-fan-curves.csv","hydroc16g2-device-settings.ini",
    "hydroc16g2-power.txt","hydroc16g2-fan-curves.csv",
    "hydroc16g2-lighting-keyboard.ini","hydroc16g2-lighting-lightbar.ini",
    "hydroc16g2-lighting-logo.ini","hydroc16g2-lighting-hinge.ini",
    "hlyl-wxx9-device-settings.ini","battery-enforcement-platform.txt",
    "hpbsd-fan-ownership.txt","hpbsd-fan-targets.txt","hpbsd-fan-curves.txt","hpbsd-power-mode.ini",
    "hpbsd-fan-profiles.csv","lenovo-legion-kwcn-power.ini","lenovo-legion-kwcn-fan-lease.txt","lenovo-legion-kwcn-fan-profiles.csv","lenovo-energy-settings.ini"};
bool settingName(std::string_view name){
    if(std::find(settingNames.begin(),settingNames.end(),name)!=settingNames.end())return true;
    for(const auto* p:deviceProfiles())if(name==std::string(p->id)+"-custom-presets.txt")return true;
    return false;
}
std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
bool integer(std::string_view text, int& value) {
    if (text.empty()) return false;
    const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), value);
    return status == std::errc{} && end == text.data() + text.size();
}
bool readText(const std::filesystem::path& path, std::string& text, std::string& error) {
    error.clear();
    const auto source=settingsReadPath(path);
    std::ifstream input(source, std::ios::binary | std::ios::ate);
    if (!input) {
        std::error_code status;
        if (std::filesystem::exists(source, status)) error = "Не удалось открыть файл настроек.";
        return false;
    }
    const auto size = input.tellg();
    if (size < 0 || size > 1024 * 1024) { error = "Размер файла настроек недопустим."; return false; }
    input.seekg(0);
    text.assign(std::istreambuf_iterator<char>(input), {});
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    return true;
}
bool writeText(const std::filesystem::path& path, const std::string& text, std::string& error) {
    return detail::writeStorageTextAtomically(path,text,error);
}

bool validateTable(const FanCurveTable& table, std::string& error) {
    std::vector<FanCurvePoint> points;
    for (const auto& cell : table) {
        if (!fanInput::validPercent(cell.duty)) { error = "PWM должен быть 5–100%, включая неиспользуемые точки."; return false; }
        if (cell.temperature != 255) points.push_back({cell.temperature, cell.duty});
    }
    return validateFanCurve(points, error);
}
}

std::filesystem::path portableSettingsPath(std::string_view filename) {
    const std::filesystem::path name(filename);
    if (name.empty() || name.has_parent_path() || name == "." || name == "..")
        throw std::invalid_argument("Имя настроек должно быть файлом внутри портативной папки.");
    const auto directory = executableDirectory();
    if (directory.empty() || directory == ".") throw std::runtime_error("Не удалось определить портативную папку программы.");
    const auto* first = reinterpret_cast<const char8_t*>(directory.data());
    const auto root=std::filesystem::path(std::u8string(first, first + directory.size()));
    return settingName(filename)?root/"settings"/name:root/name;
}
std::filesystem::path settingsReadPath(const std::filesystem::path& path){
    if(std::filesystem::exists(path)||path.parent_path().filename()!="settings"||!settingName(path.filename().string()))return path;
    const auto legacy=path.parent_path().parent_path()/path.filename();
    return std::filesystem::exists(legacy)?legacy:path;
}
void migratePortableSettings(const std::vector<std::filesystem::path>& legacyDirectories,bool preferLegacy){
    const auto root=portableSettingsPath("app-settings.ini").parent_path();
    const auto marker=root/".portable-settings-v1";
    const bool importPrevious=preferLegacy&&!std::filesystem::exists(marker);
    std::filesystem::create_directories(root);
    auto sources=legacyDirectories;sources.push_back(root.parent_path());
    auto names=settingNames;for(const auto* p:deviceProfiles())if(!p->id.empty())names.push_back(std::string(p->id)+"-custom-presets.txt");
    for(const auto& name:names){
        const auto destination=root/name;
        if(std::filesystem::exists(destination)&&!importPrevious)continue;
        for(const auto& directory:sources){
            const auto source=directory/name;
            if(source==destination||!std::filesystem::is_regular_file(source))continue;
            std::string text,error;
            if(!readText(source,text,error)||!writeText(destination,text,error))
                throw std::runtime_error("Перенос настроек "+name+": "+error);
            break;
        }
    }
    const auto backupRoot=root/"legacy-backup";
    for(const auto& name:names){
        const auto source=root.parent_path()/name;
        if(!std::filesystem::is_regular_file(source)||!std::filesystem::is_regular_file(root/name))continue;
        if(std::filesystem::file_size(source)>1024*1024)throw std::runtime_error("Legacy settings too large");
        std::ifstream original(source,std::ios::binary);
        if(!original)throw std::runtime_error("Cannot open legacy settings backup");
        const std::string bytes(std::istreambuf_iterator<char>(original),{});
        if(original.bad()||bytes.size()!=std::filesystem::file_size(source))throw std::runtime_error("Cannot read legacy settings backup");
        original.close();
        std::filesystem::create_directories(backupRoot);
        auto backup=backupRoot/name;
        for(unsigned n=1;std::filesystem::exists(backup);++n){
            if(n>1000)throw std::runtime_error("Too many legacy settings backups");
            backup=backupRoot/(name+"."+std::to_string(n)+".bak");
        }
        // Exact known source under this executable's directory; same-tree
        // rename preserves the original file, never recursively deletes data.
        std::filesystem::rename(source,backup);
        std::ifstream check(backup,std::ios::binary);
        if(!check)throw std::runtime_error("Cannot open migrated settings backup");
        const std::string copied(std::istreambuf_iterator<char>(check),{});
        if(check.bad()||copied!=bytes)throw std::runtime_error("Legacy settings changed during migration; backup retained");
    }
    std::string error;if(!writeText(marker,"Version=1\n",error))throw std::runtime_error(error);
}
bool savePortableText(const std::filesystem::path& path,std::string_view text,std::string& error) {
    if(text.size()>1024*1024){error="Размер файла настроек недопустим.";return false;}
    return writeText(path,std::string(text),error);
}
bool loadApplicationPreferences(const std::filesystem::path& path, ApplicationPreferences& preferences, std::string& error) {
    std::string text;
    if (!readText(path, text, error)) return false;
    std::istringstream input(text);
    ApplicationPreferences next;
    std::array<bool, 7> seen{};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto equal = line.find('=');
        int value = -1;
        const std::string_view key(line.data(), equal == std::string::npos ? line.size() : equal);
        const int index = key == "Version" ? 0 : key == "OsdDisabled" ? 1 :
            key == "CopilotBlocked" ? 2 : key == "EmbeddedFont" ? 3 : key == "AllLogs" ? 4 : key == "Language" ? 5 : key == "BatteryLog" ? 6 : -1;
        if (equal == std::string::npos || index < 0 || seen[static_cast<std::size_t>(index)] ||
            !integer(std::string_view(line).substr(equal + 1), value) || value < 0 || value > (index==5?3:1) || (index == 0 && value != 1)) {
            error = "Неверный формат app-settings.ini."; return false;
        }
        seen[static_cast<std::size_t>(index)] = true;
        if (index == 1) next.osdDisabled = value != 0;
        // Legacy CopilotBlocked is accepted here, but migrated into KEYs only
        // by migrateKeysPreferences; it is no longer application-owned state.
        if (index == 3) next.embeddedFont = true; // Accept the legacy key; system fonts are no longer a UI mode.
        if (index == 4) next.allLogs = value != 0;
        if (index == 5) next.language=static_cast<unsigned>(value);
        if (index == 6) next.batteryLog=value!=0;
    }
    if (!seen[0]) { error = "Версия app-settings.ini не указана."; return false; }
    preferences = next;
    return true;
}
bool saveApplicationPreferences(const std::filesystem::path& path, const ApplicationPreferences& preferences, std::string& error) {
    // KEYs switches have their own schema/file. Legacy fields are read above
    // only for compatibility; new application saves must not duplicate them.
    return writeText(path, "Version=1\r\nEmbeddedFont=1\r\nAllLogs=" + std::to_string(preferences.allLogs) + "\r\nBatteryLog=" + std::to_string(preferences.batteryLog) + "\r\nLanguage=" + std::to_string(preferences.language) + "\r\n", error);
}
namespace {
constexpr std::array<std::string_view, static_cast<std::size_t>(DeviceSetting::Count)> deviceKeys{
    "ChargeLimit", "Performance", "KeyboardLevel", "KeyboardEffect", "PulseColor",
    "Red", "Green", "Blue", "WindowsLocked", "FnLocked", "UsbCharging", "AcRecovery",
    "Wifi", "Bluetooth", "Camera", "Touchpad",
    "ManualPowerEnabled", "ManualStapm", "ManualSlow", "ManualFast", "ApuMode", "ChargeAuto", "BatteryProtection", "ChargeRestart", "KeyboardCycleEnabled", "BatteryPercentageEnabled", "BatteryCardView", "NativeCpuPresets", "ApuAutoPower", "CustomBase"};
bool validDeviceValue(std::size_t key, int value) {
    if(key==static_cast<std::size_t>(DeviceSetting::CustomBase))return value==0||value==1;
    if(key==static_cast<std::size_t>(DeviceSetting::ApuAutoPower))return value==0||value==1;
    if(key==static_cast<std::size_t>(DeviceSetting::NativeCpuPresets))return value==0||value==1;
    if(key==static_cast<std::size_t>(DeviceSetting::BatteryCardView))return value==0||value==1;
    if(key==static_cast<std::size_t>(DeviceSetting::KeyboardCycleEnabled))return value==0||value==1;
    if(key==static_cast<std::size_t>(DeviceSetting::BatteryPercentageEnabled))return value==0||value==1;
    if(key==static_cast<std::size_t>(DeviceSetting::ChargeRestart))return value>=1&&value<=99;
    if (key == static_cast<std::size_t>(DeviceSetting::BatteryProtection)) return value >= 0 && value <= 2;
    if (key == static_cast<std::size_t>(DeviceSetting::ChargeAuto)) return value >= 0 && value <= 2;
    if (key == static_cast<std::size_t>(DeviceSetting::ApuMode)) return value >= 0 && value <= 4;
    if (key >= static_cast<std::size_t>(DeviceSetting::ManualStapm)) return value >= 5 && value <= 255;
    // Preserve legacy 10% preferences for restore; 60% is now a UI preset.
    if (key == 0) return value == 100 || value == 95 || value == 85 || value == 60 || value == 10;
    const int maximum = key == 1 || key == 3 ? 2 : key == 2 ? 4 : key == 4 ? 3 : key >= 5 && key <= 7 ? 50 : 1;
    return value >= 0 && value <= maximum;
}
bool validDeviceSelection(const DevicePreferences& settings, std::string& error) {
    // ChargeLimit remembers the last explicit preset even while ChargeAuto=1;
    // actual upper=100% in Auto is selected by the battery API/restore caller.
    if ((settings[DeviceSetting::ApuMode] == 4 || settings[DeviceSetting::ManualPowerEnabled] == 1) &&
        (!settings[DeviceSetting::ManualStapm] || !settings[DeviceSetting::ManualSlow] || !settings[DeviceSetting::ManualFast])) {
        error = "Для Manual мощности должны быть сохранены три лимита."; return false;
    }
    if (settings[DeviceSetting::KeyboardEffect] == 1 &&
        !settings[DeviceSetting::PulseColor]) {
        error = "Для пульсации должен быть сохранён цвет."; return false;
    }
    return true;
}
}
bool loadDevicePreferences(const std::filesystem::path& path, DevicePreferences& settings, std::string& error) {
    std::string text;
    if (!readText(path, text, error)) return false;
    std::istringstream input(text);
    DevicePreferences next;
    int version = 0;
    bool legacyBreathing = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const auto equal = line.find('='); int value{};
        if (equal == std::string::npos || !integer(std::string_view(line).substr(equal + 1), value)) {
            error = "Неверный формат device-settings.ini."; return false;
        }
        const auto name = std::string_view(line).substr(0, equal);
        if (name == "Version") {
            if (version || (value != 1 && value != 2)) { error = "Неверная версия device-settings.ini."; return false; }
            version = value; continue;
        }
        if (name == "Breathing") {
            if (legacyBreathing || next[DeviceSetting::PulseColor] || value < 0 || value > 4) {
                error = "Недопустимый старый пресет пульсации."; return false;
            }
            legacyBreathing = true; next[DeviceSetting::PulseColor] = value; continue;
        }
        const auto found = std::find(deviceKeys.begin(), deviceKeys.end(), name);
        if (found == deviceKeys.end()) { error = "Неизвестный параметр device-settings.ini."; return false; }
        const auto key = static_cast<std::size_t>(found - deviceKeys.begin());
        if (next.values[key] || !validDeviceValue(key, value)) { error = "Недопустимое значение device-settings.ini."; return false; }
        next.values[key] = value;
    }
    if (!version) { error = "Версия device-settings.ini не указана."; return false; }
    if (legacyBreathing) {
        if (version != 1) { error = "Breathing поддерживается только в старой версии конфигурации."; return false; }
        // Preserve the colour the user selected in the old UI, rather than its
        // incorrect native preset: old 1=Red, 2=Green, 3=Blue, 4=White.
        auto& color = next[DeviceSetting::PulseColor];
        if (*color == 0) color.reset();
        else if (*color == 4) color = 0;
    }
    if (!validDeviceSelection(next, error)) return false;
    settings = next; return true;
}
std::filesystem::path batteryPreferencesPath(std::string_view id){
    if(gmRamFanProfile(id))return portableSettingsPath(std::string(gmSettings(id)));
    if(id=="eluktronics-hydroc16g2")return portableSettingsPath("hydroc16g2-device-settings.ini");
    if(id=="huawei-hlyl-wxx9")return portableSettingsPath("hlyl-wxx9-device-settings.ini");
    if(id=="wu-jie-15-xpro")return portableSettingsPath("device-settings.ini");
    throw std::runtime_error("Профиль не поддерживает настройки нижнего порога");
}
int selectedChargeRestart(std::string_view id,int fallback){
    DevicePreferences preferences;std::string error;
    if(!loadDevicePreferences(batteryPreferencesPath(id),preferences,error)&&!error.empty())throw std::runtime_error(error);
    return preferences[DeviceSetting::ChargeRestart].value_or(fallback);
}
void rememberChargeRestart(std::string_view id,int percent){
    if(percent<1||percent>99)throw std::invalid_argument("Нижний порог должен быть1–99%");
    DevicePreferences patch;patch[DeviceSetting::ChargeRestart]=percent;std::string error;
    if(!rememberDevicePreferences(patch,error,batteryPreferencesPath(id)))throw std::runtime_error("Нижний порог применён, но не сохранён: "+error);
}
bool customBaseSupported(std::string_view id) noexcept {
    // Identity and write admission remain with the exact hardware owner.
    return id=="wu-jie-15-xpro"||id=="mechrevo-gm5hg0a"||id=="mechrevo-gm6ir0c";
}
int selectedCustomBase(std::string_view id){
    if(!customBaseSupported(id))throw std::runtime_error("Custom Base: неподтверждённый профиль");
    DevicePreferences saved;std::string error;
    if(!loadDevicePreferences(batteryPreferencesPath(id),saved,error)&&!error.empty())throw std::runtime_error(error);
    return saved[DeviceSetting::CustomBase].value_or(id=="mechrevo-gm6ir0c"?0:1);
}
void rememberCustomBase(std::string_view id,int base){
    if(!customBaseSupported(id)||(base!=0&&base!=1))throw std::invalid_argument("Custom Base: недопустимое значение или профиль");
    DevicePreferences patch;patch[DeviceSetting::CustomBase]=base;std::string error;
    if(!rememberDevicePreferences(patch,error,batteryPreferencesPath(id)))throw std::runtime_error("Custom Base не сохранён: "+error);
}
bool rememberDevicePreferences(const DevicePreferences& patch, std::string& error,const std::filesystem::path& target) {
    static std::mutex gate;
    const std::lock_guard lock(gate);
    try {
        const auto path = target.empty()?portableSettingsPath("device-settings.ini"):target;
        DevicePreferences saved;
        if (!loadDevicePreferences(path, saved, error) && !error.empty()) return false;
        for (std::size_t key = 0; key < patch.values.size(); ++key) {
            if (!patch.values[key]) continue;
            if (!validDeviceValue(key, *patch.values[key])) { error = "Недопустимое сохраняемое значение."; return false; }
            saved.values[key] = patch.values[key];
        }
        if (!validDeviceSelection(saved, error)) return false;
        std::ostringstream output; output << "Version=2\r\n";
        for (std::size_t key = 0; key < saved.values.size(); ++key)
            if (saved.values[key]) output << deviceKeys[key] << '=' << *saved.values[key] << "\r\n";
        return writeText(path, output.str(), error);
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}
bool loadFanCurveSettings(const std::filesystem::path& path, FanCurveTable& cpu, FanCurveTable& gpu, std::string& error) {
    std::string text;
    if (!readText(path, text, error)) return false;
    std::istringstream input(text);
    FanCurveTable nextCpu{}, nextGpu{};
    std::array<std::array<bool, ec::kTablePointCount>, 2> seen{};
    bool versionSeen = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        if (line.starts_with("Version,")) {
            if (versionSeen || line != "Version,1") { error = "Неподдерживаемая версия fan-curves.csv."; return false; }
            versionSeen = true; continue;
        }
        std::array<std::string_view, 4> fields{};
        std::size_t start = 0;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto comma = line.find(',', start);
            if ((i + 1 == fields.size()) != (comma == std::string::npos)) { error = "Неверное число полей fan-curves.csv."; return false; }
            fields[i] = std::string_view(line).substr(start, comma == std::string::npos ? comma : comma - start);
            start = comma == std::string::npos ? line.size() : comma + 1;
        }
        const int channel = fields[0] == "CPU" ? 0 : fields[0] == "GPU" ? 1 : -1;
        int index{}, temperature{}, duty{};
        if (channel < 0 || !integer(fields[1], index) || !integer(fields[2], temperature) || !integer(fields[3], duty) ||
            index < 1 || index > ec::kTablePointCount || !fanInput::validPercent(duty) || seen[static_cast<std::size_t>(channel)][static_cast<std::size_t>(index - 1)]) {
            error = "Неверная точка fan-curves.csv."; return false;
        }
        seen[static_cast<std::size_t>(channel)][static_cast<std::size_t>(index - 1)] = true;
        (channel == 0 ? nextCpu : nextGpu)[static_cast<std::size_t>(index - 1)] = {temperature, duty};
    }
    if (!versionSeen) { error = "Версия fan-curves.csv не указана."; return false; }
    if (!validateTable(nextCpu, error) || !validateTable(nextGpu, error)) return false;
    cpu = nextCpu; gpu = nextGpu;
    return true;
}
bool saveFanCurveSettings(const std::filesystem::path& path, const FanCurveTable& cpu, const FanCurveTable& gpu, std::string& error) {
    if (!validateTable(cpu, error) || !validateTable(gpu, error)) return false;
    std::ostringstream output;
    output << "Version,1\r\n";
    for (int channel = 0; channel < 2; ++channel) {
        const auto& table = channel == 0 ? cpu : gpu;
        for (std::size_t i = 0; i < table.size(); ++i)
            output << (channel == 0 ? "CPU" : "GPU") << ',' << i + 1 << ',' << table[i].temperature << ',' << table[i].duty << "\r\n";
    }
    return writeText(path, output.str(), error);
}

FanCurveProfiles::FanCurveProfiles() {
    constexpr std::array<FanCurveCell, 8> minimum{{
        {30, 7}, {33, 10}, {38, 20}, {44, 30}, {45, 40}, {50, 50}, {60, 70}, {70, 100}}};
    for (std::size_t channel = 0; channel < 2; ++channel) {
        auto& min = curves[static_cast<std::size_t>(FanCurvePreset::Min)][channel];
        std::copy(minimum.begin(), minimum.end(), min.begin());
        curves[static_cast<std::size_t>(FanCurvePreset::Manual)][channel] = min;
    }
    ecManualCurve = curves[static_cast<std::size_t>(FanCurvePreset::Manual)][0];
    for (auto& cell : ecManualCurve) cell.duty = fanInput::percent(cell.duty);
    for (auto& table : curves[static_cast<std::size_t>(FanCurvePreset::Max)])
        table[0] = {20, 100};
}

void FanCurveProfiles::edit(std::size_t channel, const FanCurveTable& table) {
    if (table == current(channel)) return;
    if (ecManual) { ecManualCurve = table; for (auto& cell : ecManualCurve) cell.duty = fanInput::percent(cell.duty); return; }
    if (linked) channel = 0;
    curves[static_cast<std::size_t>(FanCurvePreset::Manual)][channel] = table;
    selected[channel] = FanCurvePreset::Manual;
}

namespace {
void migrateMin(FanCurveProfiles& profiles) {
    for (std::size_t channel = 0; channel < 2; ++channel) {
        std::string ignored;
        const auto& manual = profiles.curves[static_cast<std::size_t>(FanCurvePreset::Manual)][channel];
        if (validateTable(manual, ignored))
            profiles.curves[static_cast<std::size_t>(FanCurvePreset::Min)][channel] = manual;
        if (profiles.selected[channel] == FanCurvePreset::Manual) profiles.selected[channel] = FanCurvePreset::Min;
    }
}
bool validateProfiles(const FanCurveProfiles& profiles, std::string& error) {
    if (!validateTable(profiles.ecManualCurve, error)) return false;
    for (const auto& cell : profiles.ecManualCurve) if (!fanInput::validPercent(cell.duty)) { error = "EC_manual: PWM 5–100%."; return false; }
    const FanCurveProfiles builtins;
    for (std::size_t preset = 0; preset < kFanCurvePresetNames.size(); ++preset) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const auto& table = profiles.curves[preset][channel];
            if (preset == static_cast<std::size_t>(FanCurvePreset::Default) ||
                preset == static_cast<std::size_t>(FanCurvePreset::Max)) {
                if (table != builtins.curves[preset][channel]) {
                    error = "Default должен означать штатный EC, Max — постоянные 100%.";
                    return false;
                }
            } else if ((preset == static_cast<std::size_t>(FanCurvePreset::Min) || table != FanCurveTable{}) &&
                       [&]{auto active=table;for(int i=profiles.activePointCount(channel);i<16;++i)active[i]={255,100};return !validateTable(active,error);}()) return false;
        }
    }
    for (const auto preset : profiles.selected) {
        if (static_cast<std::size_t>(preset) >= kFanCurvePresetNames.size()) {
            error = "Неизвестный выбранный профиль вентилятора."; return false;
        }
    }
    return true;
}
}

bool loadKeyboardCycleSettings(KeyboardCycleSettings& value,std::string& error){
    std::string text;if(!readText(portableSettingsPath("keyboard-cycle.ini"),text,error))return false;
    KeyboardCycleSettings next;std::array<bool,6> seen{};bool version=false,interval=false;
    std::istringstream input(text);std::string line;
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        if(line=="Version=1"&&!version){version=true;continue;}
        if(line.starts_with("Interval=")&&!interval){interval=integer(std::string_view(line).substr(9),next.intervalSeconds)&&next.intervalSeconds>=1&&next.intervalSeconds<=3600;if(interval)continue;}
        if(line.starts_with("Color=")){auto fields=line.substr(6);std::replace(fields.begin(),fields.end(),',',' ');std::istringstream row(fields);int index,r,g,b;std::string extra;
            if(row>>index>>r>>g>>b&&!(row>>extra)&&index>=1&&index<=6&&!seen[index-1]&&r>=0&&r<=255&&g>=0&&g<=255&&b>=0&&b<=255){seen[index-1]=true;next.colors[index-1]={static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(g),static_cast<std::uint8_t>(b)};continue;}}
        error="Неверный формат keyboard-cycle.ini";return false;
    }
    if(!version||!interval||!std::all_of(seen.begin(),seen.end(),[](bool s){return s;})){error="Неполный keyboard-cycle.ini";return false;}
    value=next;return true;
}
bool saveKeyboardCycleSettings(const KeyboardCycleSettings& value,std::string& error){
    if(value.intervalSeconds<1||value.intervalSeconds>3600){error="Интервал цикла1–3600сек";return false;}
    std::ostringstream out;out<<"Version=1\r\nInterval="<<value.intervalSeconds<<"\r\n";
    for(int i=0;i<6;++i)out<<"Color="<<i+1<<','<<int(value.colors[i][0])<<','<<int(value.colors[i][1])<<','<<int(value.colors[i][2])<<"\r\n";
    return writeText(portableSettingsPath("keyboard-cycle.ini"),out.str(),error);
}
bool loadFanCurveProfiles(const std::filesystem::path& path, FanCurveProfiles& profiles, std::string& error) {
    std::string text;
    if (!readText(path, text, error)) return false;
    // Preserve both original Manual tables, including unused slots.
    std::istringstream versionInput(text);
    std::string line;
    while (std::getline(versionInput, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        if (line == "Version,1") {
            FanCurveProfiles next;
            for(int ch=0;ch<2;++ch)next.setActivePointCount(ch,profiles.activePointCount(ch));
            if (!loadFanCurveSettings(path, next.curves[0][0], next.curves[0][1], error)) return false;
            migrateMin(next);
            profiles = next; return true;
        }
        break;
    }
    FanCurveProfiles next;
    for(int ch=0;ch<2;++ch)next.setActivePointCount(ch,profiles.activePointCount(ch));
    std::array<std::array<std::array<bool, ec::kTablePointCount>, 2>, 4> seen{};
    std::array<bool, 2> selectionSeen{};
    std::array<bool, ec::kTablePointCount> ecCurveSeen{};
    bool versionSeen{}, nativeSeen{}, linkedSeen{}, ecManualSeen{}, legacy{},extended{},restoreSeen{};
    std::array<bool,2> pointCountsSeen{};
    std::istringstream input(text);
    const auto bad = [&error] { error = "Неверный формат профилей fan-curves.csv."; return false; };
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        std::vector<std::string_view> fields;
        std::size_t start{};
        do {
            const auto comma = line.find(',', start);
            fields.push_back(std::string_view(line).substr(start, comma == std::string::npos ? comma : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        } while (true);
        if (fields[0] == "Version") {
            if (versionSeen || (line != "Version,2" && line != "Version,3"&&line!="Version,4")) return bad();
            legacy = line == "Version,2";
            extended=line=="Version,4";
            versionSeen = true; continue;
        }
        if (fields[0] == "Mode") {
            if (nativeSeen || line != "Mode,Default,Native") return bad();
            nativeSeen = true; continue;
        }
        if(fields[0]=="ActivePoints"){
            const int ch=fields.size()==3?(fields[1]=="CPU"?0:fields[1]=="GPU"?1:-1):-1;int count{};
            if(!extended||ch<0||pointCountsSeen[ch]||!integer(fields[2],count)||count<1||count>16)return bad();pointCountsSeen[ch]=true;next.setActivePointCount(ch,count);continue;
        }
        if(fields[0]=="RestoreSelected"){int value{};if(!extended||restoreSeen||fields.size()!=2||!integer(fields[1],value)||(value!=0&&value!=1))return bad();restoreSeen=true;next.restoreSelected=value!=0;continue;}
        if (fields[0] == "Linked") {
            int linked{};
            if (linkedSeen || fields.size() != 2 || !integer(fields[1], linked) || linked < 0 || linked > 1) return bad();
            linkedSeen = true;
            next.linked = linked != 0;
            continue;
        }
        if (fields[0] == "EcManual") {
            int value{};
            if (ecManualSeen || fields.size() != 2 || !integer(fields[1], value) || value < 0 || value > 1) return bad();
            ecManualSeen = true; next.ecManual = value != 0; continue;
        }
        if (fields[0] == "EcCurve") {
            int index{}, temperature{}, duty{};
            if (fields.size() != 4 || !integer(fields[1], index) || !integer(fields[2], temperature) ||
                !integer(fields[3], duty) || index < 1 || index > ec::kTablePointCount || ecCurveSeen[index-1]) return bad();
            ecCurveSeen[index-1] = true; next.ecManualCurve[index-1] = {temperature,duty}; continue;
        }
        if (fields[0] == "Selected") {
            if (fields.size() != 3) return bad();
            const int channel = fields[1] == "CPU" ? 0 : fields[1] == "GPU" ? 1 : -1;
            const auto preset = std::find(kFanCurvePresetNames.begin(), kFanCurvePresetNames.end(), fields[2]);
            if (channel < 0 || selectionSeen[channel] || preset == kFanCurvePresetNames.end()) return bad();
            selectionSeen[channel] = true;
            next.selected[channel] = static_cast<FanCurvePreset>(preset - kFanCurvePresetNames.begin());
            continue;
        }
        if (fields.size() != 5) return bad();
        const auto preset = std::find(kFanCurvePresetNames.begin(), kFanCurvePresetNames.end(), fields[0]);
        const int channel = fields[1] == "CPU" ? 0 : fields[1] == "GPU" ? 1 : -1;
        int index{}, temperature{}, duty{};
        if (preset == kFanCurvePresetNames.end() || channel < 0 || !integer(fields[2], index) ||
            !integer(fields[3], temperature) || !integer(fields[4], duty) || index < 1 || index > ec::kTablePointCount) return bad();
        const auto p = static_cast<std::size_t>(preset - kFanCurvePresetNames.begin());
        if (legacy && p == static_cast<std::size_t>(FanCurvePreset::Min)) return bad();
        if (seen[p][channel][index - 1]) return bad();
        seen[p][channel][index - 1] = true;
        next.curves[p][channel][index - 1] = {temperature, duty};
    }
    if (!versionSeen || !nativeSeen || !selectionSeen[0] || !selectionSeen[1]) return bad();
    if(extended&&(!pointCountsSeen[0]||!pointCountsSeen[1]||!restoreSeen))return bad();
    for (std::size_t preset = 0; preset < (legacy ? 3u : seen.size()); ++preset)
        for (const auto& channel : seen[preset])
            if (std::find(channel.begin(), channel.end(), false) != channel.end()) return bad();
    const auto ecCurveCount = std::count(ecCurveSeen.begin(), ecCurveSeen.end(), true);
    if (ecCurveCount == 0) next.ecManualCurve = next.curves[static_cast<std::size_t>(FanCurvePreset::Manual)][0];
    else if (ecCurveCount != ec::kTablePointCount) return bad();
    for (auto& cell : next.ecManualCurve) cell.duty = fanInput::percent(cell.duty);
    if (!validateProfiles(next, error)) return false;
    if (legacy) migrateMin(next);
    if (next.ecManual) { next.linked = true; next.selected.fill(FanCurvePreset::Manual); }
    profiles = next;
    return true;
}

bool saveFanCurveProfiles(const std::filesystem::path& path, const FanCurveProfiles& profiles, std::string& error) {
    error.clear();
    if (!validateProfiles(profiles, error)) return false;
    std::ostringstream output;
    const bool extended=profiles.restoreSelected||profiles.activePointCount(0)!=16||profiles.activePointCount(1)!=16;
    if(extended&&std::filesystem::exists(path))try{
        std::ifstream previous(path);std::string first;std::getline(previous,first);
        if(!first.starts_with("Version,4")){const auto backup=path.parent_path()/"legacy-backup"/(path.filename().string()+".before-shared-curves");std::filesystem::create_directories(backup.parent_path());if(!std::filesystem::exists(backup))std::filesystem::copy_file(path,backup);}
    }catch(const std::exception& e){error=e.what();return false;}
    output << (extended?"Version,4\r\n":"Version,3\r\n")<<"Mode,Default,Native\r\n";
    if(extended){for(int ch=0;ch<2;++ch)output<<"ActivePoints,"<<(ch?"GPU":"CPU")<<','<<profiles.activePointCount(ch)<<"\r\n";output<<"RestoreSelected,"<<profiles.restoreSelected<<"\r\n";}
    output << "Linked," << profiles.linked << "\r\n";
    output << "EcManual," << profiles.ecManual << "\r\n";
    for (std::size_t i=0; i<ec::kTablePointCount; ++i)
        output << "EcCurve," << i+1 << ',' << profiles.ecManualCurve[i].temperature << ',' << profiles.ecManualCurve[i].duty << "\r\n";
    for (std::size_t channel = 0; channel < 2; ++channel)
        output << "Selected," << (channel == 0 ? "CPU" : "GPU") << ','
               << kFanCurvePresetNames[static_cast<std::size_t>(profiles.selected[channel])] << "\r\n";
    for (std::size_t preset = 0; preset < kFanCurvePresetNames.size(); ++preset)
        for (std::size_t channel = 0; channel < 2; ++channel)
            for (std::size_t i = 0; i < ec::kTablePointCount; ++i) {
                const auto& cell = profiles.curves[preset][channel][i];
                output << kFanCurvePresetNames[preset] << ',' << (channel == 0 ? "CPU" : "GPU") << ','
                       << i + 1 << ',' << cell.temperature << ',' << cell.duty << "\r\n";
            }
    return writeText(path, output.str(), error);
}
}
