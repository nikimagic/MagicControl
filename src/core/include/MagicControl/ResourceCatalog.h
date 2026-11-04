#pragma once
#include "NativeDefaultFan.h"
#include "DeviceClass.h"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mc {
struct MonitoringSnapshot;
struct SensorReading;
struct BoardDescriptor;
// Observation model only. Entries never grant hardware access or write admission.
enum class ResourceKind { Processor, Graphics, Fan, Battery, Memory, Storage, Lighting, Other, Accelerator, Chipset, Sensor, SystemPower, Input, Connectivity, Audio, Camera };
enum class ResourceAvailability { Available, Unavailable, Unknown };
enum class ResourceEvidence { Unknown, Documented, Observed, VerifiedOnDevice };
// Monotonic time provenance; none of these claims a hardware-provided sample
// timestamp unless a future source explicitly supplies and labels one.
enum class ResourceTimestampOrigin { Unknown, CollectorStarted, SoftwareObservation, DisplayCache };
struct ResourceIdentity {
    // Stable within its declared scope; empty means no durable identity known.
    std::string machine, resource, scope;
    friend bool operator==(const ResourceIdentity&,const ResourceIdentity&)=default;
};
struct ResourceFacet {
    std::string name, source, reason;
    ResourceAvailability availability{ResourceAvailability::Unknown};
    ResourceEvidence evidence{ResourceEvidence::Unknown};
    friend bool operator==(const ResourceFacet&,const ResourceFacet&)=default;
};
struct ResourceMeasurement {
    std::string metric, unit, source, status;
    std::optional<double> value;
    std::uint64_t sampledAtMs{};
    // nullopt means the source does not expose per-field freshness.
    std::optional<bool> retained;
    // Optional unconverted source value; never substituted for the measurement.
    std::optional<double> raw;
    ResourceTimestampOrigin timestampOrigin{ResourceTimestampOrigin::Unknown};
    // Exact counters/sizes, especially GPU bytes. Never round uint64 through double.
    std::optional<std::uint64_t> unsignedValue;
    friend bool operator==(const ResourceMeasurement&,const ResourceMeasurement&)=default;
};
struct ResourceObservation {
    ResourceIdentity identity;
    ResourceKind kind{ResourceKind::Other};
    // Source-reported presence. An absent channel can retain diagnostic data;
    // that never makes the hardware available or grants an operation.
    std::optional<bool> present;
    // Ephemeral index in the source snapshot, never a durable identity.
    std::optional<unsigned> presentationSlot;
    std::string name, owner, ownerSource;
    // Raw source-reported serial used for snapshot comparison only. It may be
    // absent, duplicated or a firmware placeholder; it is not a durable ID.
    std::string sourceSerialHint;
    std::vector<ResourceFacet> facets;
    std::vector<ResourceMeasurement> measurements;
    std::optional<NativeDefaultFanChannel> defaultFanCurve;
    friend bool operator==(const ResourceObservation&,const ResourceObservation&)=default;
};
// SMBIOS/profile metadata is a model description, never a unique machine ID
// and never evidence that a runtime transport has admitted a command.
struct MachineObservation {
    std::string manufacturer, board, product, profile, protocol, ecBackend, smuBackend;
    DeviceClass deviceClass{DeviceClass::Unknown};
    bool catalogRecognized{};
    std::vector<ResourceFacet> declaredFeatures;
};
// Versions are evidence about different objects, never interchangeable:
// PnP package metadata, installed SYS VERSIONINFO, and loaded kernel image.
// Missing values mean unknown, not absent or an assumed bundled version.
struct ResourceDriverObservation {
    std::string name,binding,status;
    std::optional<bool> installed,running;
    std::optional<std::string> packageVersion,fileVersion,loadedVersion;
};
struct ResourceProjectionCheck {
    std::size_t comparedValues{};
    std::vector<std::string> mismatches;
    // No values is not a successful hardware or projection verification.
    [[nodiscard]] bool verified() const noexcept {return comparedValues!=0&&mismatches.empty();}
};
struct ResourceCatalog {
    static constexpr unsigned schemaVersion=1;
    MachineObservation machine;
    // Availability describes the source itself, not permissions or the
    // availability of each device/channel it may report.
    std::vector<ResourceFacet> sources;
    std::vector<ResourceDriverObservation> drivers;
    // Only snapshot-backed providers can populate this. Independent providers
    // and an unattempted comparison leave it absent, not zero mismatches.
    std::optional<ResourceProjectionCheck> projectionCheck;
    std::vector<ResourceObservation> resources;
};
std::vector<ResourceObservation> observeSensorResources(std::span<const SensorReading>);
// Projects an existing snapshot without polling, mutation or write admission.
// Processor slots are presentation channels, not durable hardware identities.
ResourceCatalog observeMonitoringResources(const MonitoringSnapshot&);
ResourceCatalog observeMachineResources(const BoardDescriptor&,const MonitoringSnapshot&);
// Read-only interchange; missing numeric values and timestamps stay JSON null.
std::string resourceCatalogJson(const ResourceCatalog&);
// Compare represented values and sources, never authorize operations.
std::vector<std::string> compareMonitoringProjection(const MonitoringSnapshot&,const ResourceCatalog&,std::size_t* observedValues=nullptr);
ResourceProjectionCheck verifyMonitoringProjection(const MonitoringSnapshot&,const ResourceCatalog&);
// Sole accepted resource state for a consumer. The collector snapshot is an
// ingestion DTO; it is not kept as a second source for numeric presentation.
// Replacement/clear invalidates every previously issued view.
class ResourceState {
public:
    ResourceState()=default;
    ResourceState(const ResourceState&)=delete;
    ResourceState& operator=(const ResourceState&)=delete;
    void update(const BoardDescriptor&,const MonitoringSnapshot&) noexcept;
    void clear() noexcept { revision_.reset();current_.reset(); }
    const std::shared_ptr<const ResourceCatalog>& current() const noexcept { return current_; }
private:
    friend class ResourcePresentationView;
    std::shared_ptr<const ResourceCatalog> current_;
    std::shared_ptr<const unsigned char> revision_;
};
// Immutable generation-scoped view. Replacement, clear and destruction of
// the state invalidate it. It never reads a second source or grants writes.
class ResourcePresentationView {
public:
    ResourcePresentationView()=default;
    explicit ResourcePresentationView(const ResourceState&) noexcept;
    [[nodiscard]] bool available() const noexcept { return catalog_&&!revision_.expired(); }
    [[nodiscard]] const ResourceMeasurement* measurement(ResourceKind,unsigned,std::string_view) const noexcept;
    [[nodiscard]] const NativeDefaultFanChannel* defaultFanCurve(unsigned slot) const noexcept;
private:
    std::shared_ptr<const ResourceCatalog> catalog_;
    std::weak_ptr<const unsigned char> revision_;
};
// The accepted resource owns every displayed numeric result. An unavailable
// view or missing metric stays unavailable; source DTO values never fill it.
[[nodiscard]] inline std::optional<double> resourceDisplayValue(const ResourcePresentationView* view,
        ResourceKind kind,unsigned slot,std::string_view metric) noexcept {
    if(!view)return {};
    if(!view->available())return {};
    const auto* measurement=view->measurement(kind,slot,metric);
    return measurement?measurement->value:std::nullopt;
}
[[nodiscard]] inline std::optional<std::uint64_t> resourceDisplayUnsigned(const ResourcePresentationView* view,
        ResourceKind kind,unsigned slot,std::string_view metric) noexcept {
    const auto* measurement=view?view->measurement(kind,slot,metric):nullptr;
    return measurement?measurement->unsignedValue:std::nullopt;
}
}
