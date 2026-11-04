#pragma once
#include "MagicControl/ActionLog.h"
#include "MagicControl/GpuDevice.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/ResourceControl.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mc {
enum class GpuOperation { Read, SetPower, ResetPower };
struct GpuOperationRequest {
    GpuOperation kind{GpuOperation::Read};std::string providerId;std::optional<int> value;
    bool writing()const noexcept{return kind!=GpuOperation::Read;}
};
struct GpuOperationContext {
    audit::Id auditId{};bool viewOnly{};
    std::optional<std::vector<GpuReading>> preview;
    std::shared_ptr<DeviceOperation> reservation;
};
struct DiscreteGpuOperationResult {
    std::vector<GpuReading> readings;std::string message;bool action{},success{},preview{},readFailed{};std::string uuid;
};
// Single synchronous command entry for CLI and the shared GUI model. Admission
// precedes every provider call. Only the original provider ID reaches a writer.
DiscreteGpuOperationResult executeGpuOperation(DiscreteGpu&,const GpuOperationRequest&,GpuOperationContext={});
// One application model for both frontends: provider/lane/result/cache ownership.
// View bindings supply only immutable preview data and a native readiness signal.
class GpuOperations {
public:
    explicit GpuOperations(GpuVendor vendor):vendor(vendor),gpu_(vendor){}
    ~GpuOperations(){shutdown();}
    const GpuVendor vendor;std::vector<GpuReading> readings;std::string message;
    bool busy{},applying{};
    void bindReady(std::function<void()> ready){ready_=std::move(ready);}
    bool submit(GpuOperationRequest,bool viewOnly,std::optional<std::vector<GpuReading>> preview={});
    std::optional<DiscreteGpuOperationResult> take(std::optional<std::vector<GpuReading>> currentPreview={});
    void shutdown(){worker_.join();reservation_.reset();}
private:
    DiscreteGpu gpu_;ResourceWorker worker_;GpuDisplayCache displayCache_;
    std::optional<DiscreteGpuOperationResult> pending_;std::function<void()> ready_;
    std::shared_ptr<DeviceOperation> reservation_;GpuOperationRequest active_;bool immediate_{};
};
}
