#pragma once
#include "DiagnosticDumpInternal.h"
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <span>
namespace mc::diagnostics::wire {
using Bytes=std::vector<unsigned char>;
inline constexpr std::uint32_t maximumFrame=32*1024*1024,maximumString=8*1024*1024;
inline constexpr unsigned char sampleRequest=0x61,quitRequest=0x62,firmwareRequest=0x63,firmwareStateRequest=0x64,biosRomRequest=0x65;
template<class T>void put(Bytes& out,T value){
    if(out.size()+sizeof(value)>maximumFrame)throw std::runtime_error("diagnostic frame exceeds wire limit");
    const auto* p=reinterpret_cast<const unsigned char*>(&value);out.insert(out.end(),p,p+sizeof(value));
}
inline void putString(Bytes& out,const std::string& value){
    if(value.size()>maximumString||out.size()+4+value.size()>maximumFrame)throw std::runtime_error("diagnostic text exceeds wire limit");
    put(out,static_cast<std::uint32_t>(value.size()));out.insert(out.end(),value.begin(),value.end());
}
inline void putNumber(Bytes& out,std::optional<double> value){put<std::uint8_t>(out,value&&std::isfinite(*value)?1:0);if(value&&std::isfinite(*value))put(out,*value);}
struct Reader{
    std::span<const unsigned char> bytes;std::size_t at{};
    template<class T>T get(){if(at>bytes.size()||sizeof(T)>bytes.size()-at)throw std::runtime_error("truncated diagnostic wire frame");T result{};std::memcpy(&result,bytes.data()+at,sizeof(result));at+=sizeof(result);return result;}
    std::string string(){const auto count=get<std::uint32_t>();if(count>maximumString||count>bytes.size()-at)throw std::runtime_error("invalid diagnostic string");std::string value(reinterpret_cast<const char*>(bytes.data()+at),count);at+=count;return value;}
    std::optional<double> number(){const auto flag=get<std::uint8_t>();if(flag>1)throw std::runtime_error("invalid diagnostic optional");if(!flag)return {};const auto v=get<double>();if(!std::isfinite(v))throw std::runtime_error("non-finite diagnostic value");return v;}
};
inline Bytes encode(const FastDiagnosticFrame& frame){
    Bytes out;put<std::uint32_t>(out,1);put<std::uint8_t>(out,frame.complete?1:0);put(out,frame.readDurationMs);
    for(const auto* value:{&frame.gpuJson,&frame.graphicsJson,&frame.nvidiaJson,&frame.intelJson,&frame.radeonJson,&frame.batteryJson,&frame.utc,&frame.status})putString(out,*value);
    if(frame.nvidia.size()>32||frame.readings.size()>65536)throw std::runtime_error("diagnostic record count exceeds limit");
    put(out,static_cast<std::uint32_t>(frame.nvidia.size()));
    for(const auto& gpu:frame.nvidia){putString(out,gpu.name);putString(out,gpu.uuid);put<std::uint8_t>(out,gpu.available?1:0);}
    put(out,static_cast<std::uint32_t>(frame.readings.size()));
    for(const auto& row:frame.readings){for(const auto* value:{&row.device,&row.metric,&row.unit,&row.source,&row.status})putString(out,*value);putNumber(out,row.value);putNumber(out,row.raw);put(out,row.sampledAtMs);}
    return out;
}
inline FastDiagnosticFrame decode(std::span<const unsigned char> bytes){
    Reader reader{bytes};if(reader.get<std::uint32_t>()!=1)throw std::runtime_error("unsupported diagnostic wire version");FastDiagnosticFrame frame;
    const auto complete=reader.get<std::uint8_t>();if(complete>1)throw std::runtime_error("invalid diagnostic completion flag");frame.complete=complete!=0;frame.readDurationMs=reader.get<std::int64_t>();
    for(auto* value:{&frame.gpuJson,&frame.graphicsJson,&frame.nvidiaJson,&frame.intelJson,&frame.radeonJson,&frame.batteryJson,&frame.utc,&frame.status})*value=reader.string();
    const auto gpus=reader.get<std::uint32_t>();if(gpus>32)throw std::runtime_error("invalid diagnostic GPU count");
    for(unsigned i=0;i<gpus;++i){FastGpuIdentity gpu;gpu.name=reader.string();gpu.uuid=reader.string();const auto available=reader.get<std::uint8_t>();if(available>1)throw std::runtime_error("invalid diagnostic GPU state");gpu.available=available!=0;frame.nvidia.push_back(std::move(gpu));}
    const auto count=reader.get<std::uint32_t>();if(count>65536)throw std::runtime_error("invalid diagnostic sensor count");
    for(unsigned i=0;i<count;++i){SensorReading row;for(auto* value:{&row.device,&row.metric,&row.unit,&row.source,&row.status})*value=reader.string();row.value=reader.number();row.raw=reader.number();row.sampledAtMs=reader.get<std::uint64_t>();frame.readings.push_back(std::move(row));}
    if(reader.at!=bytes.size())throw std::runtime_error("trailing diagnostic wire data");return frame;
}
inline FastDiagnosticFrame failure(std::string message,std::int64_t readMs=0){
    FastDiagnosticFrame frame;frame.status=std::move(message);frame.readDurationMs=readMs;
    frame.readings.push_back({"Isolated fast providers","provider_status","","MagicControl · read-only worker",frame.status,{},{},batteryMonotonicMs()});return frame;
}
inline Bytes encodeFirmware(const FirmwareRecord& record){
    Bytes out;put<std::uint32_t>(out,2);put<std::uint8_t>(out,record.success);put<std::uint8_t>(out,record.final);
    for(const auto* value:{&record.kind,&record.name,&record.source,&record.status,&record.errorDomain,&record.metadataJson})putString(out,*value);
    for(const auto& value:{record.errorCode,record.attributes}){put<std::uint8_t>(out,value?1:0);if(value)put(out,*value);}
    if(record.bytes.size()>maximumFirmwareArtifactBytes||out.size()+4+record.bytes.size()>maximumFrame)
        throw std::runtime_error("firmware artifact exceeds wire budget");
    put(out,static_cast<std::uint32_t>(record.bytes.size()));out.insert(out.end(),record.bytes.begin(),record.bytes.end());return out;
}
inline FirmwareRecord decodeFirmware(std::span<const unsigned char> bytes){
    Reader reader{bytes};if(reader.get<std::uint32_t>()!=2)throw std::runtime_error("invalid firmware wire version");
    FirmwareRecord record;const auto success=reader.get<std::uint8_t>(),final=reader.get<std::uint8_t>();
    if(success>1||final>1)throw std::runtime_error("invalid firmware flags");record.success=success!=0;record.final=final!=0;
    for(auto* value:{&record.kind,&record.name,&record.source,&record.status,&record.errorDomain,&record.metadataJson})*value=reader.string();
    for(auto* value:{&record.errorCode,&record.attributes}){const auto has=reader.get<std::uint8_t>();if(has>1)throw std::runtime_error("invalid firmware optional flag");if(has)*value=reader.get<std::uint32_t>();}
    const auto length=reader.get<std::uint32_t>();
    if(length>maximumFirmwareArtifactBytes||length>bytes.size()-reader.at)throw std::runtime_error("invalid firmware payload length");
    record.bytes.assign(bytes.begin()+reader.at,bytes.begin()+reader.at+length);reader.at+=length;
    if(reader.at!=bytes.size()||(record.final&&!record.bytes.empty()))throw std::runtime_error("invalid firmware trailing data");return record;
}
// Shared fixed protocol. Provider instances survive requests, preserving energy
// counter deltas. No paths/queries/setter opcodes can be supplied over this pipe.
template<class Read,class Write>int serve(Read&& read,Write&& write){
    std::unique_ptr<FastDiagnosticSource> source;
    const auto send=[&](const FastDiagnosticFrame& frame){auto body=encode(frame);const auto size=static_cast<std::uint32_t>(body.size());
        if(!write(&size,sizeof(size))||!write(body.data(),body.size()))throw std::runtime_error("diagnostic worker pipe closed");};
    const auto sendFirmware=[&](const FirmwareRecord& record){const auto body=encodeFirmware(record);const auto size=static_cast<std::uint32_t>(body.size());
        if(!write(&size,sizeof(size))||!write(body.data(),body.size()))throw std::runtime_error("firmware worker pipe closed");};
    for(;;){unsigned char command{};if(!read(&command,1))return 0;if(command==quitRequest)return 0;
        if(command==firmwareRequest||command==firmwareStateRequest||command==biosRomRequest){
            // This dispatch happens before FastDiagnosticSource construction:
            // no GPU/PawnIO/EC provider or controller is opened for this pass.
            FirmwareRecord done;done.kind="capture";done.final=true;std::size_t count{},total{};
            const bool biosRom=command==biosRomRequest;
            try{const auto emit=[&](const FirmwareRecord& record){
                if(record.final||++count>=(biosRom?maximumBiosRomRecords:maximumFirmwareRecords)||record.bytes.size()>maximumFirmwareTotalBytes-total)
                    throw std::runtime_error("firmware capture exceeds record/total budget");
                total+=record.bytes.size();sendFirmware(record);
            };if(biosRom)collectPlatformBiosRom(emit);else collectPlatformFirmware(command==firmwareStateRequest,emit);
            done.success=true;done.status=biosRom?"ROM2 aperture read finished; full SPI capacity unknown":"Read-only firmware evidence pass finished; individual source errors remain explicit";}
            catch(const std::exception& e){done.status=e.what();}
            try{sendFirmware(done);}catch(...){return 1;}continue;
        }
        if(command!=sampleRequest)return 2;
        FastDiagnosticFrame frame;
        try{if(!source){auto initializing=failure("initializing read-only providers");send(initializing);source=std::make_unique<FastDiagnosticSource>();}frame=source->read(send);}
        catch(const std::exception& e){frame=failure(e.what());}
        // A failed whole pass is a terminal reply too; it must not masquerade
        // as a progress fragment and make the parent wait another deadline.
        if(!frame.complete)frame.status="terminal_error: "+frame.status;
        try{send(frame);}catch(...){return 1;}
    }
}
}
