#pragma once
#include <filesystem>
#include <functional>
#include <stop_token>
#include <atomic>
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <stdexcept>
namespace mc::diagnostics::nvflash {
using Event=std::function<void(const char*,const std::string&)>;
#ifdef _WIN32
// Same cross-process semaphore as capture. Empty means optional NVML polling
// must skip this sample; it must never wait on a reader from the UI path.
std::shared_ptr<void> tryTelemetryLease();
#endif
enum class Command { List, Check, Save, SaveEntire, Compare };
inline bool saves(Command c){return c==Command::Save||c==Command::SaveEntire;}
inline const char* commandName(Command c){switch(c){case Command::List:return "list";case Command::Check:return "check";case Command::Save:return "save_vbios";case Command::SaveEntire:return "save_entire";case Command::Compare:return "compare";}throw std::runtime_error("Invalid NVFlash read command");}
// One fixed read-only argv contract for both native adapters. No arbitrary
// options can reach the vendor utility. Output is UTF-8 (native-adapted later).
inline std::vector<std::string> arguments(Command c,unsigned index,const std::string& output){
    if(index>255)throw std::runtime_error("Invalid NVFlash index");
    if(c==Command::List)return {"--list"};
    std::vector<std::string> args{"--index="+std::to_string(index)};
    switch(c){
    case Command::Check:args.emplace_back("--check");return args;
    case Command::Save:args.emplace_back("--save");break;
    case Command::SaveEntire:args.emplace_back("--entire");args.emplace_back("--save");break;
    case Command::Compare:args.emplace_back("--compare");break;
    default:throw std::runtime_error("Invalid NVFlash read command");
    }
    if(output.empty()||output.front()=='-')throw std::runtime_error("Invalid NVFlash artifact path");args.push_back(output);return args;
}
// Platform adapters use this shared allowlist, never firmware-writing options.
// No shell, caller options, flash, protectoff, reset or driver-disable commands.
std::shared_ptr<void> preflight(const std::filesystem::path& artifacts,const Event& event);
unsigned run(Command command,unsigned index,const std::filesystem::path& output,
    const std::filesystem::path& artifacts,const std::string& label,
    const Event& event,std::stop_token token,std::atomic<std::uint64_t>* progress,
    std::uint64_t completedBytes,const std::shared_ptr<void>& lease);
void atomicText(const std::filesystem::path&,const std::string&);
}
