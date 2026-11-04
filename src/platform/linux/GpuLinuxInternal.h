#pragma once
#include "MagicControl/GpuDevice.h"
#include <filesystem>
#include <stop_token>
#include <fstream>
#include <optional>
#include <sstream>
namespace mc::linuxgpu {
struct Device {GraphicsAdapter adapter;std::filesystem::path path;};
inline std::string text(const std::filesystem::path& p){std::ifstream f(p);std::string s;std::getline(f,s);return s;}
inline std::optional<std::uint64_t> number(const std::filesystem::path& p,int base=10){try{const auto s=text(p);if(s.empty())return {};std::size_t used{};auto v=std::stoull(s,&used,base);return used==s.size()?std::optional<std::uint64_t>(v):std::nullopt;}catch(...){return {};}}
std::vector<Device> devices();
SensorReading readSensorValue(const std::filesystem::path& path,std::string device,std::string metric,std::string unit,double divisor=1.);
std::vector<SensorReading> hwmonReadings(const std::filesystem::path& deviceFilter={},std::stop_token token={});
}
