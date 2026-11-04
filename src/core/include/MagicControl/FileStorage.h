#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace mc::detail {
// OS file capabilities only: no device, driver or hardware initialization.
// Locks use a stable ordinary file. Never unlink it: another process may still
// hold its inode/handle. A failed nonblocking acquisition has an empty error
// only when the lock is busy. Destruction/process exit releases ownership.
// New paths use visible locks/. If legacy .locks/ already exists, bridge its
// same-name lease first for older processes; never create a legacy directory.
std::shared_ptr<void> lockStorageFile(const std::filesystem::path&,bool wait,std::string& error);
// Serialize publishers; create an exclusive, unique sibling temporary and
// flush/close before the sole atomic replacement. Failure removes only our own
// temporary, never the target. Readers needing a read/modify/write transaction
// hold their domain lease as well; the publication lock is always acquired last.
bool writeStorageTextAtomically(const std::filesystem::path&,std::string_view,std::string& error);
}
