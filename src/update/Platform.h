#pragma once
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>
namespace mc::update::platform {
std::filesystem::path executable();
std::string run(const std::vector<std::string>& args, std::stop_token stop={}, int timeoutSeconds=60);
void launch(const std::filesystem::path& executable,const std::vector<std::string>& args);
std::string sha256(const std::filesystem::path&);
std::string pid();
bool waitForParent(const std::string& pid);
void extract(const std::filesystem::path& archive,const std::filesystem::path& destination,std::stop_token stop);
std::string curl();
// Bounded HTTPS-only download, shared by update and dependency setup flows.
void download(const std::string& url, const std::filesystem::path& output, std::uintmax_t limit, std::stop_token stop);
void replace(const std::filesystem::path& source,const std::filesystem::path& destination);
// Filesystem/process capabilities only. Never stop a running helper.
bool waitForUpdateHelpers(const std::filesystem::path& folder);
bool linkLike(const std::filesystem::path&);
void removeEntry(const std::filesystem::path&); // one file/link/empty directory
#ifdef _WIN32
void launchInstaller(const std::filesystem::path& root,const std::filesystem::path& stage);
#endif
}
