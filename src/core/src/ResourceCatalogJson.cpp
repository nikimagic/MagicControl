#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/ResourceProvider.h"
#include <charconv>
#include <cmath>
#include <limits>

namespace mc {
namespace {
// JSON encoding is kept independent of logging and platform library order.
std::string quote(std::string_view value){
    constexpr char digits[]="0123456789abcdef";std::string out="\"";
    for(unsigned char c:value){
        if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
const char* kind(ResourceKind value){switch(value){
case ResourceKind::Processor:return "processor";case ResourceKind::Graphics:return "graphics";
case ResourceKind::Fan:return "fan";case ResourceKind::Battery:return "battery";
case ResourceKind::Memory:return "memory";case ResourceKind::Storage:return "storage";
case ResourceKind::Lighting:return "lighting";case ResourceKind::Accelerator:return "accelerator";
case ResourceKind::Chipset:return "chipset";case ResourceKind::Sensor:return "sensor";
case ResourceKind::SystemPower:return "system_power";case ResourceKind::Input:return "input";
case ResourceKind::Connectivity:return "connectivity";case ResourceKind::Audio:return "audio";
case ResourceKind::Camera:return "camera";default:return "other";}}
const char* deviceClass(DeviceClass value){switch(value){
case DeviceClass::Laptop:return "laptop";case DeviceClass::MiniPc:return "mini_pc";
case DeviceClass::Desktop:return "desktop";case DeviceClass::Server:return "server";default:return "unknown";}}
const char* availability(ResourceAvailability value){switch(value){
case ResourceAvailability::Available:return "available";case ResourceAvailability::Unavailable:return "unavailable";default:return "unknown";}}
const char* evidence(ResourceEvidence value){switch(value){
case ResourceEvidence::Documented:return "documented";case ResourceEvidence::Observed:return "observed";
case ResourceEvidence::VerifiedOnDevice:return "verified_on_device";default:return "unknown";}}
const char* timestampOrigin(ResourceTimestampOrigin value){switch(value){
case ResourceTimestampOrigin::CollectorStarted:return "collector_started";
case ResourceTimestampOrigin::SoftwareObservation:return "software_observation";
case ResourceTimestampOrigin::DisplayCache:return "display_cache";default:return "unknown";}}
std::string number(const std::optional<double>& value){
    if(!value||!std::isfinite(*value))return "null";
    char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof buffer,*value,
        std::chars_format::general,std::numeric_limits<double>::max_digits10);
    return result.ec==std::errc{}?std::string(buffer,result.ptr):"null";
}
std::string facets(const std::vector<ResourceFacet>& values){
    std::string out="[";for(const auto& f:values){if(out.size()>1)out+=',';
        out+="{\"name\":"+quote(f.name)+",\"source\":"+quote(f.source)+",\"reason\":"+quote(f.reason)+
            ",\"availability\":"+quote(availability(f.availability))+",\"evidence\":"+quote(evidence(f.evidence))+"}";}
    return out+"]";
}
std::string nullable(const std::optional<std::string>& value){return value?quote(*value):"null";}
std::string nullable(const std::optional<bool>& value){return value?(*value?"true":"false"):"null";}
std::string drivers(const std::vector<ResourceDriverObservation>& values){
    std::string out="[";for(const auto& d:values){if(out.size()>1)out+=',';
        out+="{\"name\":"+quote(d.name)+",\"binding\":"+quote(d.binding)+",\"status\":"+quote(d.status)+
            ",\"installed\":"+nullable(d.installed)+",\"running\":"+nullable(d.running)+
            ",\"package_version\":"+nullable(d.packageVersion)+",\"installed_file_version\":"+nullable(d.fileVersion)+
            ",\"loaded_version\":"+nullable(d.loadedVersion)+"}";}
    return out+"]";
}
std::string nativeCurve(const NativeDefaultFanChannel& c){
    std::string out="{\"profile\":"+quote(c.profile)+",\"curve_source\":"+quote(c.curveSource)+
        ",\"table\":"+std::to_string(c.table)+",\"temperature_source\":"+quote(c.temperatureSource)+
        ",\"temperature_address\":"+std::to_string(c.temperatureAddress)+",\"control_celsius\":"+number(c.temperatureCelsius)+
        ",\"requested_percent\":"+number(c.requestedPercent)+",\"request_source\":"+quote(c.requestSource)+
        ",\"graph_unit\":"+quote(c.rpm?"rpm":"percent")+",\"requested_rpm\":"+number(c.requestedRpm)+",\"actual_rpm\":"+number(c.actualRpm)+
        ",\"stage\":"+std::to_string(c.stage)+",\"demand_stage\":"+std::to_string(c.demandStage)+
        ",\"request_derived\":"+(c.requestDerived?"true":"false")+",\"forced\":"+(c.forced?"true":"false")+
        ",\"retained\":"+(c.retained?"true":"false")+
        ",\"sampled_at_monotonic_ms\":"+std::to_string(c.sampledAtMs)+",\"points\":[";
    for(std::size_t i=0;i<c.points.size();++i){if(i)out+=',';
        out+="["+number(c.points[i].temperature)+","+number(c.points[i].duty)+"]";}
    out+="],\"inputs\":[";
    for(std::size_t i=0;i<c.inputs.size();++i){const auto& input=c.inputs[i];if(i)out+=',';
        out+="{\"source\":"+quote(input.source)+",\"address\":"+std::to_string(input.address)+",\"temperature\":"+number(input.temperature)+",\"stage\":"+std::to_string(input.stage);
        for(const auto& pair:{std::pair{"rising",&input.rising},std::pair{"falling",&input.falling}}){out+=",\""+std::string(pair.first)+"\":[";
            for(std::size_t n=0;n<pair.second->size();++n){if(n)out+=',';const auto& point=(*pair.second)[n];out+="["+number(point.temperature)+","+number(point.duty)+"]";}out+=']';}
        out+='}';
    }
    return out+"]}";
}
std::string projectionCheck(const std::optional<ResourceProjectionCheck>& value){
    if(!value)return "null";
    std::string out="{\"compared_values\":"+std::to_string(value->comparedValues)+
        ",\"mismatch_count\":"+std::to_string(value->mismatches.size())+",\"verified\":"+(value->verified()?"true":"false")+",\"mismatches\":[";
    bool first=true;for(const auto& issue:value->mismatches){if(!first)out+=',';first=false;out+=quote(issue);}
    return out+"]}";
}
}
std::string resourceCatalogJson(const ResourceCatalog& catalog){
    const auto& m=catalog.machine;
    std::string out="{\"schema\":"+std::to_string(ResourceCatalog::schemaVersion)+",\"machine\":{\"manufacturer\":"+quote(m.manufacturer)+
        ",\"board\":"+quote(m.board)+",\"product\":"+quote(m.product)+",\"profile\":"+quote(m.profile)+
        ",\"class\":"+quote(deviceClass(m.deviceClass))+",\"recognized\":"+(m.catalogRecognized?"true":"false")+
        ",\"protocol\":"+quote(m.protocol)+",\"ec_backend\":"+quote(m.ecBackend)+",\"smu_backend\":"+quote(m.smuBackend)+
        ",\"declared_features\":"+facets(m.declaredFeatures)+"},\"sources\":"+facets(catalog.sources)+",\"drivers\":"+drivers(catalog.drivers)+
        ",\"projection_check\":"+projectionCheck(catalog.projectionCheck)+",\"resources\":[";
    bool first=true;for(const auto& r:catalog.resources){if(!first)out+=',';first=false;
        out+="{\"present\":"+(r.present?std::string(*r.present?"true":"false"):std::string("null"))+",\"kind\":"+quote(kind(r.kind))+",\"identity\":{\"machine\":"+quote(r.identity.machine)+
            ",\"resource\":"+quote(r.identity.resource)+",\"scope\":"+quote(r.identity.scope)+"},\"presentation_slot\":"+
            (r.presentationSlot?std::to_string(*r.presentationSlot):"null")+",\"name\":"+quote(r.name)+
            ",\"source_serial_hint\":"+quote(r.sourceSerialHint)+",\"owner\":"+quote(r.owner)+",\"owner_source\":"+quote(r.ownerSource)+",\"facets\":"+facets(r.facets)+",\"measurements\":[";
        bool firstMeasurement=true;for(const auto& v:r.measurements){if(!firstMeasurement)out+=',';firstMeasurement=false;
            out+="{\"unsigned_value\":"+(v.unsignedValue?std::to_string(*v.unsignedValue):std::string("null"))+",\"metric\":"+quote(v.metric)+",\"unit\":"+quote(v.unit)+",\"value\":"+number(v.value)+
                ",\"raw\":"+number(v.raw)+",\"source\":"+quote(v.source)+",\"status\":"+quote(v.status)+",\"sampled_at_monotonic_ms\":"+
                (v.sampledAtMs?std::to_string(v.sampledAtMs):"null")+",\"retained\":"+
                (v.retained?(*v.retained?"true":"false"):"null")+",\"timestamp_origin\":"+quote(timestampOrigin(v.timestampOrigin))+"}";
        }
        out+="]";
        if(r.defaultFanCurve)out+=",\"native_default_curve\":"+nativeCurve(*r.defaultFanCurve);
        out+="}";
    }
    return out+"]}";
}
std::string resourceProviderResultsJson(std::span<const ResourceProviderResult> results){
    std::string out="[";bool first=true;
    for(const auto& result:results){if(!first)out+=',';first=false;
        out+="{\"name\":"+quote(result.name)+",\"status\":"+quote(result.status)+",\"detail\":"+quote(result.detail)+
            ",\"catalog\":"+(result.catalog?resourceCatalogJson(*result.catalog):"null")+"}";
    }
    return out+"]";
}
}
