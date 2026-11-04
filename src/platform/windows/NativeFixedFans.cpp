#include "NativeFixedFans.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "../../devices/huawei/hlyl-wxx9/FanControlWindows.h"
#include "../../devices/minisforum/hpbsd/FanControlWindows.h"
#include "../../devices/lenovo/legion-kwcn/ControlWindows.h"
namespace mc::windows {
namespace {
// Adapter only: retain the original Honor class, state ABI, watchdog, native
// commands and factories byte-for-byte. The new GUI interface owns no IO.
class HonorFixedAdapter final:public NativeFixedFans {
    std::shared_ptr<HlylFanController> controller_;
public:
    explicit HonorFixedAdapter(std::shared_ptr<HlylFanController> c):controller_(std::move(c)){}
    NativeFixedFanState read()override{const auto s=controller_->read();NativeFixedFanState out;
        out.manual=s.manual;out.writable=s.writable;out.actualPercent=s.actualPercent;out.targetPercent=s.targetPercent;out.status=s.status;out.defaultFans=s.defaultFans;return out;}
    void setFixed(const std::array<int,2>& percent)override{controller_->setFixed(percent);}
    NativeFanAutoResult setAutomatic()override{const auto result=controller_->setAutomatic();
        switch(result){case HlylAutoResult::Applied:return NativeFanAutoResult::Applied;case HlylAutoResult::AlreadyAuto:return NativeFanAutoResult::AlreadyAuto;case HlylAutoResult::OwnershipLost:return NativeFanAutoResult::OwnershipLost;}return NativeFanAutoResult::OwnershipLost;}
};
}
std::shared_ptr<NativeFixedFans> nativeFixedFanController(){
    const auto id=deviceProfile(readBoardDescriptor()).id;
    if(id=="minisforum-hpbsd")return hpbsdFanController();
    if(id=="lenovo-legion-kwcn")return legionFanController();
    if(id=="huawei-hlyl-wxx9"){auto controller=hlylFanController();if(controller)return std::make_shared<HonorFixedAdapter>(std::move(controller));}
    return {};
}
}
