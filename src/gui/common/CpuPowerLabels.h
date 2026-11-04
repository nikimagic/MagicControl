#pragma once
#include <array>
#include <string_view>
#include <string>
#include "Localization.h"
#include "MagicControl/StockCpuProfiles.h"
namespace mc::gui::ui {
inline constexpr int cpuPowerButtonCount(std::string_view profile){
    if(profile=="huawei-hlyl-wxx9")return 3;
    return (profile=="wu-jie-15-xpro"||profile=="mechrevo-gm6ir0c"||profile=="mechrevo-gm5hg0a"||profile=="machenike-l16")?5:4;
}
inline std::array<const char*,5> cpuPowerLabels(std::string_view profile){
    const auto stock=stockCpuProfileNames(profile);
    return {"Minimal",stock[0],stock[1],stock[2],"Auto"};
}
inline std::array<const char*,3> cpuPowerFieldLabels(std::string_view id,bool rawPl4=false){
    if(id=="lenovo-legion-kwcn")return {"Cross Loading, W ","PL1, W ","PL2, W "};
    if(id=="mechrevo-gm6ir0c"||id=="eluktronics-hydroc16g2")return {"PL1, Вт ","PL2, Вт ",rawPl4?"PL4, raw ":"PL4, Вт "};
    if(id=="mechrevo-gm5hg0a"||id=="machenike-l16")return {"SPL, Вт ","SPPT, Вт ","FPPT, Вт "};
    if(id=="wu-jie-15-xpro"||id=="huawei-hlyl-wxx9"||id=="minisforum-hpbsd")return {"STAPM, Вт ","Slow PPT, Вт ","Fast PPT, Вт "};
    return {"Limit 1, Вт ","Limit 2, Вт ","Limit 3, Вт "};
}
inline std::string cpuPowerOptionLabel(std::string_view id,std::string_view label,std::string_view unit){
    if(id=="lenovo-legion-kwcn")return std::string(label)+", "+std::string(unit)+" ";
    return translate(std::string(label))+", "+translate(unit=="W"?"Вт":unit=="s"?"сек":std::string(unit))+" ";
}

}
