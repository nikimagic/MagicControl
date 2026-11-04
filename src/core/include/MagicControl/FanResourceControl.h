#pragma once
#include "ResourceControl.h"
#include "PortableSettings.h"
#include "NativeFanCurve.h"

namespace mc {
enum class FanResourceOperation { Default, Manual, Maximum, Minimum, Online, Restore, SaveDraft };
enum class FanWriteOrigin {Explicit,Restore,Compound};
// Data only. Curves are copied without normalization/resampling; their exact
// model codecs and protective stages remain with the admitted controller.
struct FanWriteRequest {
    FanResourceOperation operation{FanResourceOperation::Restore};
    std::optional<FanCurveProfiles> profiles{};
    std::optional<FanCurveTable> ecCurve{};
    std::optional<CustomFanCurves> curves{};
    std::optional<std::array<int,2>> target{};
    std::array<bool,2> controlled{true,true};bool linked{true};
    bool wait{},adoptExisting{};
    FanWriteOrigin origin{FanWriteOrigin::Explicit};
};
// The bound controller owns channel interpretation, device identity, curve
// encoding, thermal safeguards and readback. No model mapping is inferred.
inline ResourceControlKey fanResourceControlKey(FanResourceOperation action){
    switch(action){
    case FanResourceOperation::Default:return {ResourceKind::Fan,"selected-controller","mode.default"};
    case FanResourceOperation::Manual:return {ResourceKind::Fan,"selected-controller","mode.manual"};
    case FanResourceOperation::Maximum:return {ResourceKind::Fan,"selected-controller","mode.maximum"};
    case FanResourceOperation::Minimum:return {ResourceKind::Fan,"selected-controller","mode.minimum"};
    case FanResourceOperation::Online:return {ResourceKind::Fan,"selected-controller","mode.online"};
    case FanResourceOperation::Restore:return {ResourceKind::Fan,"selected-controller","mode.restore"};
    case FanResourceOperation::SaveDraft:return {ResourceKind::Fan,"selected-controller","draft.save"};
    }
    throw std::invalid_argument("FANs: unknown resource operation");
}
}
