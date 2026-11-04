#include "MagicControl/ResourceProvider.h"
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace mc {
DeferredResourceProvider::DeferredResourceProvider(std::function<std::unique_ptr<ResourceProvider>()> factory)
    :factory_(std::move(factory)){if(!factory_)throw std::invalid_argument("resource provider factory is required");}
ResourceCatalog DeferredResourceProvider::observe(){
    if(!provider_){auto candidate=factory_();if(!candidate)throw std::runtime_error("resource provider factory returned null");provider_=std::move(candidate);}
    return provider_->observe();
}
std::vector<ResourceProviderResult> observeResourceProviders(std::span<const ResourceProviderEntry> providers){
    std::unordered_set<std::string> names;
    for(const auto& entry:providers)if(entry.name.empty()||!names.insert(entry.name).second||(!entry.provider&&entry.skipReason.empty()))
        throw std::invalid_argument("resource provider names must be unique and skipped sources need a reason");
    std::vector<ResourceProviderResult> results;results.reserve(providers.size());
    for(const auto& entry:providers){
        ResourceProviderResult result;result.name=entry.name;
        if(!entry.provider){result.status="skipped";result.detail=entry.skipReason;}
        else try{result.catalog=entry.provider->observe();result.status="ok";}
        catch(const std::exception& error){result.status="failed";result.detail=error.what();}
        catch(...){result.status="failed";result.detail="unknown provider failure";}
        results.push_back(std::move(result));
    }
    return results;
}
MonitoringResourceProvider::MonitoringResourceProvider(const BoardDescriptor& board,std::function<MonitoringSnapshot()> read)
    :board_(board),read_(std::move(read)){
    if(!read_)throw std::invalid_argument("Resource provider requires a monitoring source");
}
ResourceCatalog MonitoringResourceProvider::observe(){
    const auto snapshot=read_();auto catalog=observeMachineResources(board_,snapshot);
    catalog.projectionCheck=verifyMonitoringProjection(snapshot,catalog);return catalog;
}
}
