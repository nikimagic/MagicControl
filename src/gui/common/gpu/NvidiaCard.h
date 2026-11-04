#pragma once
#include "GpuCardBase.h"
namespace mc::gui::ui::gpu {
inline ProcessorCard nvidiaCard(const GpuCardReading& reading,const FanReadout& fan,const std::string& fanSource){auto card=baseCard(reading,fan,fanSource);memory(card,reading,"VRAM");return card;}
}
