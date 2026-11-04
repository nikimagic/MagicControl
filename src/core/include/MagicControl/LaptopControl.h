#pragma once
#include "SensorReading.h"
#include "PowerSettings.h"
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace mc {
struct CpuPowerChoice {
    int baseProfile{};bool custom{};std::array<int,3> values{};PowerOptions options{};
    friend bool operator==(const CpuPowerChoice&,const CpuPowerChoice&)=default;
};
// Typed OEM functions are not a byte-addressed EC window. Never adapt Huawei
// commands to MECHREVO registers just to reuse EcAccess or boardVerified().
struct LaptopControlSnapshot {
    bool connected{},chargeWritable{},fnWritable{},turboWritable{};
    std::optional<int> chargeStart,chargeEnd;
    // Named firmware modes are not percentage thresholds: 0 normal, 1 conservation, 2 rapid.
    std::optional<int> chargeMode;
    bool chargeModeWritable{};
    std::optional<bool> fnLock;
    std::optional<bool> nativeTurbo;
    std::optional<int> stockCpuProfile; // Backend-native index, never a watt preset.
    bool stockCpuProfileWritable{};
    // Native white backlight: 0 off, 1 low, 2 high. Not RGB/PWM percentages.
    std::optional<int> keyboardLevel, keyboardTimeoutSeconds;
    // Selected level survives native idle fade; observed duty is independent.
    std::optional<int> keyboardActualDuty, keyboardPwmPeriod;
    std::string keyboardStatus;
    std::string keyboardPhase;
    bool keyboardWritable{}, keyboardTimeoutWritable{};
    std::array<std::optional<int>,2> fanRpm;
    std::optional<double> cpuCelsius,batteryCelsius;
    std::vector<SensorReading> temperatureSensors;
    std::string source,status;
    bool retainedForDisplay{};
    bool customCpuPowerWritable{};
    std::optional<bool> customCpuPowerActive;
    std::optional<std::array<int,3>> customCpuPowerLimits;
    std::array<std::optional<PowerSetting>,3> customCpuPowerSettings;
    std::vector<PowerSetting> customPowerOptions;
    friend bool operator==(const LaptopControlSnapshot&,const LaptopControlSnapshot&)=default;
};
class LaptopControl {
public:
    virtual ~LaptopControl()=default;
    // Identifies the backend's charge policy implementation, never permission
    // or evidence of an admitted device. Unknown backends default to none.
    virtual std::string_view chargePolicyId() const noexcept {return {};}
    // Identifies a specific custom-power implementation, not write admission.
    // An unrecognized typed backend cannot inherit another device's SMU rules.
    virtual std::string_view customPowerPolicyId() const noexcept {return {};}
    virtual std::optional<CpuPowerChoice> savedCpuPowerChoice()const{return {};}
    // A backend must explicitly provide an in-process lock spanning the
    // charge policy read/decision/write; this does not exclude other apps or
    // replace board admission and per-write readback. Unknown backends refuse.
    virtual void withChargePolicyTransaction(const std::function<void()>&){
        throw std::runtime_error("Батарейная транзакция не поддерживается backend");
    }
    virtual LaptopControlSnapshot read()=0;
    // Diagnostic worker may be cancelled by process lifetime. OEM overrides
    // must omit direct configuration-port sessions requiring in-process exit.
    virtual LaptopControlSnapshot readTelemetry(){return read();}
    // Fresh narrow getter for Battery-only views/dumps; no cached or applied state.
    virtual std::optional<double> readBatteryTemperature(){return {};}
    // Explicit choices only; Fn may also replay a saved choice through the common
    // KEYs lifecycle after fresh typed capability/readback checks. No raw EC route.
    // Both setters must confirm their own readback.
    virtual void setChargeThresholds(int start,int end)=0;
    virtual void setChargeMode(int){throw std::runtime_error("Штатный режим зарядки не поддерживается этим транспортом");}
    virtual void setFnLock(bool enabled)=0;
    virtual void setNativeTurbo(bool){throw std::runtime_error("Штатный Turbo не поддерживается этим транспортом");}
    virtual void setStockCpuProfile(int){throw std::runtime_error("Штатные CPU-профили не поддерживаются этим транспортом");}
    // Typed Custom metadata is pure; setting retains exact device admission,
    // readback and rollback. Unknown backends cannot inherit another SMU ABI.
    virtual std::array<int,3> customCpuPowerMaximum()const{throw std::runtime_error("Custom CPU limits unavailable");}
    virtual std::array<int,3> customCpuPowerRecipe(int)const{throw std::runtime_error("Custom CPU recipes unavailable");}
    virtual std::array<int,3> customCpuPowerMinimum()const{return {5,5,5};}
    virtual PowerOptions customPowerOptionRecipe(int)const{return {};}
    virtual void validateCustomPower(const std::array<int,3>& values,const PowerOptions& options={})const{
        if(!options.empty())throw std::runtime_error("Custom power options unavailable");
        const auto maximum=customCpuPowerMaximum(),minimum=customCpuPowerMinimum();
        for(int i=0;i<3;++i)if(values[i]<minimum[i]||values[i]>maximum[i])throw std::invalid_argument("Custom power outside native bounds");
        if(values[0]>values[1]||values[1]>values[2])throw std::invalid_argument("Custom power order invalid");
    }
    virtual std::array<int,3> setCustomPower(const std::array<int,3>& values,const PowerOptions& options){
        validateCustomPower(values,options);return setCustomCpuPower(values);
    }
    virtual std::array<int,3> setCustomCpuPower(const std::array<int,3>&){throw std::runtime_error("Custom CPU writer unavailable");}
    virtual void setKeyboardLevel(int){throw std::runtime_error("Подсветка недоступна через этот транспорт");}
    virtual void setKeyboardTimeout(int){throw std::runtime_error("Таймаут подсветки недоступен через этот транспорт");}
};
// Returns null on other boards. Construction does not apply saved settings.
std::unique_ptr<LaptopControl> createLaptopControl();
}
