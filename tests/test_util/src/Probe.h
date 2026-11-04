#pragma once
#include "Json.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>

namespace probe {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
std::string utcNow();
std::string fileStamp();
std::string hex(std::uint64_t value, int width = 0);
std::string bytesHex(const void *bytes, std::size_t size);
std::optional<std::string> readText(const fs::path &path, std::size_t limit = 1024 * 1024);
std::optional<std::string> readBytes(const fs::path &path, std::size_t limit = 1024 * 1024);
fs::path executablePath();
std::string hostName();
struct Options {
    int count = 10;
    double interval = 5;
    fs::path output;
    bool ec = true, pawn = true, amd = true, nvidia = true, inventoryOnly = false;
    bool ecOnly = false;
};
class Log {
  public:
    Log(const fs::path &path, std::string runId);
    void sample(int id) {
        sample_ = id;
    }
    void event(const std::string &kind, Json record);
    void reading(const std::string &source, const std::string &id, const std::string &name, Json raw,
                 Json value = {}, const std::string &unit = "", const std::string &comment = "",
                 const std::string &status = "ok", const std::string &error = "");
    void source(const std::string &name, const std::function<void()> &collect);
    void unavailable(const std::string &source, const std::string &error);
    void flush();
    std::uint64_t records() const {
        return records_;
    }

  private:
    std::ofstream file_;
    std::string runId_;
    Clock::time_point start_ = Clock::now();
    int sample_{};
    std::uint64_t records_{};
};
class Platform {
  public:
    virtual ~Platform() = default;
    virtual Json inventory() = 0;
    virtual void sample(Log &log) = 0;
};
class Collector {
  public:
    virtual ~Collector() = default;
    virtual Json info() = 0;
    virtual void sample(Log &log) = 0;
};
std::unique_ptr<Collector> createNvidia(bool enabled);
#ifdef _WIN32
std::unique_ptr<Collector> createAmd(bool enabled);
std::unique_ptr<Collector> createPawn(bool enabled, const fs::path &binaryDir);
#endif
std::unique_ptr<Platform> createPlatform(const Options &options, const fs::path &binaryDir);
} // namespace probe
