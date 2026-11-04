#pragma once
#include <filesystem>
#include <memory>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace mc::update {
inline constexpr char repository[]="https://github.com/nikimagic/MagicControl";
struct Release {
    std::string version, tag, pageUrl, archiveName, archiveUrl, checksumUrl, digest;
    std::filesystem::path localArchive;
    bool githubUnavailable{};
    bool available{}, preview{};
};
Release check(std::stop_token stop={});
// Copies a selected local package or downloads it, then validates it;
// the helper waits for a normal parent exit.
void prepareAndLaunch(const Release&, std::stop_token, const std::function<void(std::string)>& progress);
// Called before GUI, singleton, logging or any hardware initialization.
// A normal restart also finalizes a verified successful install: wait for the
// helper exit and empty the update cache, retaining rollback data on failure.
std::optional<int> runHelper(const std::vector<std::string>& arguments);
std::string lastResult();
// The updater and instance handoff share numeric/SemVer ordering.
// -1/0/1, or no value when either version is invalid.
std::optional<int> compareVersions(const std::string& left,const std::string& right);
struct OperationState {
    Release release;std::string message;bool busy{},done{},ready{},releaseMessage{};
};
// Shared finite check/install lifecycle. UI owns only rendering and normal exit;
// cancellation, progress, release state and completion have one owner on both OS.
class Operations {
public:
    Operations();~Operations();
    Operations(const Operations&)=delete;Operations& operator=(const Operations&)=delete;
    bool start(bool install);
    OperationState snapshot();
    void cancel();void shutdown();
private:struct Impl;std::unique_ptr<Impl> impl_;
};

}
