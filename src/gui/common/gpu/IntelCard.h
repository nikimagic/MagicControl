#pragma once
#include "GpuCardBase.h"
namespace mc::gui::ui::gpu {
inline ProcessorCard intelCard(GpuCardReading reading,const FanReadout& fan,const std::string& fanSource){
    // Until a GPU-only energy domain is established, do not show CPU Package
    // or suspect legacy IGCL card-energy values as Intel GPU consumption.
    reading.powerWatts.reset();auto card=baseCard(reading,fan,fanSource);
    memory(card,reading,"Выделено");
    return card;
}
}
