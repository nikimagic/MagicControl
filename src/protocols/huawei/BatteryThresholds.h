#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace mc::huawei {
using ChargeThresholds = std::pair<int,int>;
// Factory HLYL SBTT(0,100) stores BCCL/BCCH=0/0. This is disabled policy,
// not a 100% threshold and not proof of a completed in-flight commit.
inline bool validChargeThresholds(ChargeThresholds value) noexcept {
    return value==ChargeThresholds{0,0} ||
        (value.first>=0&&value.first<=value.second&&value.second>=1&&value.second<=100);
}
inline std::string chargeThresholdText(ChargeThresholds value){
    return std::to_string(value.first)+"/"+std::to_string(value.second);
}
inline bool chargeThresholdsOwned(ChargeThresholds current,ChargeThresholds before,ChargeThresholds target) noexcept {
    // A two-node sysfs update may have committed just one of our components.
    // A third value is external/unknown and is never overwritten by rollback.
    return (current.first==before.first||current.first==target.first)&&
        (current.second==before.second||current.second==target.second);
}
template<class Read,class Pause>
ChargeThresholds stableChargeThresholds(Read read,Pause pause,std::optional<ChargeThresholds> expected={}){
    std::optional<ChargeThresholds> previous;
    std::string observation="no response";
    std::string failures;
    for(int attempt=0;attempt<8;++attempt){
        try{
            const auto value=read();
            if(!validChargeThresholds(value))throw std::runtime_error("invalid pair "+chargeThresholdText(value));
            observation="pair="+chargeThresholdText(value);
            if(previous==value&&(!expected||value==*expected))return value;
            previous=value;
        }catch(const std::exception& error){
            previous.reset();observation=error.what();
            if(!failures.empty())failures+=" | ";failures+="attempt"+std::to_string(attempt+1)+": "+error.what();
        }
        if(attempt!=7)pause();
    }
    throw std::runtime_error("Huawei: stable charge readback not confirmed"+
        (expected?"; expected="+chargeThresholdText(*expected):std::string{})+"; last="+observation+
        (failures.empty()?std::string{}:"; failures="+failures));
}
}
