#pragma once
#include "Monitoring.h"
#include "ResourceCatalog.h"
#include "BoardDescriptor.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mc {
// Observation only. Implementations must not acquire write permissions or
// perform control operations merely to enumerate resources.
class ResourceProvider {
public:
    virtual ~ResourceProvider()=default;
    virtual ResourceCatalog observe()=0;
};
// Defers transport/reader construction until observation, allowing the batch
// to report setup failures while still querying independent sources. A failed
// construction may be retried on the next observation; no operation is sent.
// The caller serializes observations on each instance.
class DeferredResourceProvider final:public ResourceProvider {
public:
    explicit DeferredResourceProvider(std::function<std::unique_ptr<ResourceProvider>()> factory);
    ResourceCatalog observe() override;
private:
    std::function<std::unique_ptr<ResourceProvider>()> factory_;
    std::unique_ptr<ResourceProvider> provider_;
};
// Independent observations are kept in separate catalogs: presentation slots
// or similar names from different providers are never assumed to be the same
// hardware. A failed/skipped provider contributes no fabricated catalog.
struct ResourceProviderEntry {std::string name;ResourceProvider* provider{};std::string skipReason;};
struct ResourceProviderResult {std::string name,status,detail;std::optional<ResourceCatalog> catalog;};
std::vector<ResourceProviderResult> observeResourceProviders(std::span<const ResourceProviderEntry>);
std::string resourceProviderResultsJson(std::span<const ResourceProviderResult>);

// Compatibility source for an already existing monitoring reader. One call
// obtains exactly one snapshot; it neither polls again nor applies settings.
// Board description is captured by value; objects captured by read must
// outlive this provider. Recreate the provider to observe a new machine.
class MonitoringResourceProvider final:public ResourceProvider {
public:
    MonitoringResourceProvider(const BoardDescriptor& board,std::function<MonitoringSnapshot()> read);
    ResourceCatalog observe() override;
private:
    BoardDescriptor board_;
    std::function<MonitoringSnapshot()> read_;
};
}
