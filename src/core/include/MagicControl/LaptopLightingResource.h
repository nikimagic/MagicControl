#pragma once
#include "LaptopControl.h"
#include "LightingZones.h"
#include <stdexcept>

namespace mc {
// Both OS adapters use this native entry. Setters retain exact model admission
// and readback. A pair keeps its order and attempts both fields independently.
inline LightingWriteResult executeLightingResource(LaptopControl& control,const LightingWriteRequest& request){
    int level=-1,timeout=-1;
    switch(request.kind){
    case LightingWrite::Level:level=request.value;break;
    case LightingWrite::Timeout:timeout=request.value;break;
    case LightingWrite::NativeUpdate:level=request.value;timeout=request.timeout;break;
    default:throw std::invalid_argument("Lighting operation is unavailable through this native transport");
    }
    if(level<0&&timeout<0)throw std::invalid_argument("No native lighting operation requested");
    LightingWriteResult result;std::string errors;
    const auto attempt=[&](bool brightness,int value){
        try{
            if(brightness)control.setKeyboardLevel(value);else control.setKeyboardTimeout(value);
            if(result.success)result.message=brightness?"Яркость подтверждена чтением.":"Таймаут подтверждён чтением.";
        }catch(const std::exception& error){result.success=false;if(!errors.empty())errors+="; ";errors+=error.what();}
    };
    if(level>=0)attempt(true,level);
    if(timeout>=0)attempt(false,timeout);
    if(!errors.empty())result.message=std::move(errors);
    return result;
}
inline bool lightingReadbackMatches(const LaptopControlSnapshot& observed,const LightingWriteRequest& request){
    if(request.kind!=LightingWrite::Level&&request.kind!=LightingWrite::Timeout&&request.kind!=LightingWrite::NativeUpdate)return false;
    const int level=request.kind==LightingWrite::Timeout?-1:request.value;
    const int timeout=request.kind==LightingWrite::Level?-1:request.kind==LightingWrite::Timeout?request.value:request.timeout;
    return (level<0||observed.keyboardLevel==level)&&(timeout<0||observed.keyboardTimeoutSeconds==timeout);
}
}
