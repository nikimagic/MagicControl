#include "MagicControl/ControlAudit.h"
namespace mc {
namespace {
class Audited final:public LaptopControl{
    std::unique_ptr<LaptopControl> inner_;
    template<class F>void run(std::string_view name,std::string fields,F&& f){audit::Scope trace(name,fields);try{f();trace.finish("setter_returned");}catch(const std::exception& e){trace.fail(e.what());throw;}}
public:
    explicit Audited(std::unique_ptr<LaptopControl> v):inner_(std::move(v)){}
    std::string_view chargePolicyId() const noexcept override{return inner_->chargePolicyId();}
    std::string_view customPowerPolicyId() const noexcept override{return inner_->customPowerPolicyId();}
    std::optional<CpuPowerChoice> savedCpuPowerChoice()const override{return inner_->savedCpuPowerChoice();}
    std::array<int,3> customCpuPowerMaximum()const override{return inner_->customCpuPowerMaximum();}
    std::array<int,3> customCpuPowerRecipe(int slot)const override{return inner_->customCpuPowerRecipe(slot);}
    std::array<int,3> customCpuPowerMinimum()const override{return inner_->customCpuPowerMinimum();}
    PowerOptions customPowerOptionRecipe(int slot)const override{return inner_->customPowerOptionRecipe(slot);}
    void validateCustomPower(const std::array<int,3>& v,const PowerOptions& o)const override{inner_->validateCustomPower(v,o);}
    std::array<int,3> setCustomPower(const std::array<int,3>& v,const PowerOptions& o)override{
        std::array<int,3> result{};run("laptop.custom_power","\"option_count\":"+std::to_string(o.size()),[&]{result=inner_->setCustomPower(v,o);});return result;
    }
    std::array<int,3> setCustomCpuPower(const std::array<int,3>& values)override{
        std::array<int,3> result{};run("laptop.custom_cpu_power","\"stapm\":"+std::to_string(values[0])+",\"slow\":"+std::to_string(values[1])+",\"fast\":"+std::to_string(values[2]),[&]{result=inner_->setCustomCpuPower(values);});return result;
    }
    void withChargePolicyTransaction(const std::function<void()>& action) override{inner_->withChargePolicyTransaction(action);}
    LaptopControlSnapshot read()override{try{auto s=inner_->read();audit::state("typed_laptop_control",audit::laptop(s));return s;}catch(const std::exception& e){audit::event("observation_failed","\"source\":\"typed_laptop_control\",\"error\":"+audit::quote(e.what()));throw;}}
    LaptopControlSnapshot readTelemetry()override{auto s=inner_->readTelemetry();audit::state("typed_laptop_telemetry",audit::laptop(s));return s;}
    std::optional<double> readBatteryTemperature()override{return inner_->readBatteryTemperature();}
    void setChargeThresholds(int a,int b)override{run("laptop.charge_thresholds","\"start\":"+std::to_string(a)+",\"end\":"+std::to_string(b),[&]{inner_->setChargeThresholds(a,b);});}
    void setChargeMode(int value)override{run("laptop.charge_mode","\"mode\":"+std::to_string(value),[&]{inner_->setChargeMode(value);});}
    void setFnLock(bool on)override{run("laptop.fn_lock","\"enabled\":"+std::string(on?"true":"false"),[&]{inner_->setFnLock(on);});}
    void setNativeTurbo(bool on)override{run("laptop.native_turbo","\"enabled\":"+std::string(on?"true":"false"),[&]{inner_->setNativeTurbo(on);});}
    void setStockCpuProfile(int profile)override{run("laptop.stock_cpu_profile","\"profile\":"+std::to_string(profile),[&]{inner_->setStockCpuProfile(profile);});}
    void setKeyboardLevel(int level)override{run("laptop.keyboard_level","\"level\":"+std::to_string(level),[&]{inner_->setKeyboardLevel(level);});}
    void setKeyboardTimeout(int seconds)override{run("laptop.keyboard_timeout","\"seconds\":"+std::to_string(seconds),[&]{inner_->setKeyboardTimeout(seconds);});}
};
}
std::unique_ptr<LaptopControl> auditedLaptopControl(std::unique_ptr<LaptopControl> v){return v?std::make_unique<Audited>(std::move(v)):nullptr;}
}
