// MagicControl - read-only Linux hardware inventory.

#include "MagicControl/HardwareInventory.h"
#include "MagicControl/EcIdentityInventory.h"
#include "MagicControl/GpuDevice.h"
#include "KeyboardLedInventoryLinux.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <sys/utsname.h>
#include <system_error>
#include <utility>

namespace mc {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return std::nullopt;
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string result = trim(contents.str());
    if (result.empty()) return std::nullopt;
    return result;
}

std::string dmiValue(const char* name) {
    return readText(std::filesystem::path("/sys/class/dmi/id") / name)
        .value_or(kInventoryUnknown);
}

std::string osName() {
    std::ifstream input("/etc/os-release");
    std::string line;
    while (std::getline(input, line)) {
        constexpr std::string_view key = "PRETTY_NAME=";
        if (!line.starts_with(key)) continue;
        std::string value = line.substr(key.size());
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        return value.empty() ? kInventoryUnknown : value;
    }
    return kInventoryUnknown;
}

std::string cpuModel() {
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = trim(line.substr(0, colon));
        if (key == "model name" || key == "Hardware") {
            return trim(line.substr(colon + 1));
        }
    }
    return kInventoryUnknown;
}

std::string secureBootState() {
    const std::filesystem::path directory("/sys/firmware/efi/efivars");
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return kInventoryUnknown;

    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!name.starts_with("SecureBoot-")) continue;
        std::ifstream input(it->path(), std::ios::binary);
        std::array<unsigned char, 5> bytes{};
        if (!input.read(reinterpret_cast<char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()))) {
            continue;
        }
        return bytes[4] == 0 ? "Выключен" : bytes[4] == 1 ? "Включён" : kInventoryUnknown;
    }
    return kInventoryUnknown;
}

std::string readTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    if (localtime_r(&now, &local) == nullptr) return {};
    std::ostringstream result;
    result << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return result.str();
}

class LinuxHardwareProvider final : public IHardwareProvider {
public:
    [[nodiscard]] const char* id() const noexcept override { return "linux-sysfs"; }

    [[nodiscard]] const char* description() const noexcept override {
        return "DMI, sysfs, /proc и данные ядра Linux";
    }

    [[nodiscard]] HardwareInventory collect(std::stop_token cancellation = {}) const override {
        HardwareInventory inventory;
        inventory.readAt = readTimestamp();

        InventorySection& machine = inventory.addSection("Ноутбук · BIOS · UEFI · EC");
        machine.addRow("Производитель", dmiValue("sys_vendor"));
        machine.addRow("Модель", dmiValue("product_name"));
        machine.addRow("Версия модели", dmiValue("product_version"));
        machine.addRow("Системная плата", dmiValue("board_name"));
        machine.addRow("Производитель платы", dmiValue("board_vendor"));
        machine.addRow("BIOS", dmiValue("bios_vendor"));
        machine.addRow("Версия BIOS", dmiValue("bios_version"));
        machine.addRow("Дата BIOS", dmiValue("bios_date"));
        machine.addRow("Режим загрузки", std::filesystem::exists("/sys/firmware/efi")
            ? "UEFI" : "Legacy BIOS или виртуальная среда без EFI");
        machine.addRow("Secure Boot", secureBootState());
        machine.addRow("EC", "Чтение и запись Linux-транспортом пока не реализованы");

        if (cancellation.stop_requested()) return inventory;

        InventorySection& operatingSystem = inventory.addSection("Операционная система");
        operatingSystem.addRow("Дистрибутив", osName());
        struct utsname system{};
        if (uname(&system) == 0) {
            operatingSystem.addRow("Ядро", system.release);
            operatingSystem.addRow("Архитектура", system.machine);
        } else {
            operatingSystem.addRow("Ядро", kInventoryUnknown);
            operatingSystem.addRow("Архитектура", kInventoryUnknown);
        }
        operatingSystem.addRow("Процессор", cpuModel());
        operatingSystem.addRow("Среда", readText("/proc/sys/kernel/osrelease")
            .value_or(kInventoryUnknown));

        if(!cancellation.stop_requested()){
            auto& drivers=inventory.addSection("Драйверы");
            for(const auto& adapter:graphicsAdapters()){
                if(adapter.vendorId!=0x10de&&adapter.vendorId!=0x1002&&adapter.vendorId!=0x8086)continue;
                std::string version=adapter.driver.empty()?kInventoryUnknown:adapter.driver;
                if(!adapter.driver.empty()){
                    const auto moduleVersion=readText(std::filesystem::path("/sys/module")/adapter.driver/"version");
                    if(moduleVersion)version+=" · "+*moduleVersion;
                    else if(system.release[0])version+=" · ядро "+std::string(system.release);
                }
                drivers.addRow("Видеодрайвер · "+adapter.name,std::move(version));
            }
            drivers.addRow("PawnIO","Не применим в Linux");
            const auto cpu=cpuModel();
            const auto vendor=cpu.find("AMD")!=std::string::npos?"AMD":cpu.find("Intel")!=std::string::npos?"Intel":"Intel/AMD";
            drivers.addRow(std::string("Чипсет ")+vendor,system.release[0]?std::string("Драйверы ядра Linux · ")+system.release:kInventoryUnknown);
        }

        appendInventoryEcIdentity(inventory,cancellation);
        return inventory;
    }
};

}  // namespace

const IHardwareProvider& defaultHardwareProvider() {
    static const LinuxHardwareProvider provider;
    return provider;
}

HardwareInventory collectDiagnosticInventory(std::stop_token cancellation) {
    auto inventory=defaultHardwareProvider().collect(cancellation);
    if(!cancellation.stop_requested()){
        auto& lighting=inventory.addSection("Подсветка клавиатуры · Linux LED metadata");
        lighting.rows=linuxinventory::keyboardLedRows(cancellation);
    }
    return inventory;
}

std::string executableDirectory() {
    // /proc/self/exe resolves the running binary, independent of the shell's CWD.
    std::error_code error;
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error || path.empty()) return ".";
    return path.parent_path().string();
}

}  // namespace mc
