#pragma once
#include <windows.h>
namespace mc::windows {
inline constexpr auto kPciMutexName=L"Global\\Access_PCI";
// Same shared object used by other hardware monitors. CreateMutexW asks for
// MUTEX_ALL_ACCESS on an existing object and fails when wait/release is allowed
// but ownership/ACL changes are not. Never change its ACL or bypass it with a
// private mutex. Both telemetry and SMU transactions need only these rights.
inline HANDLE openSharedHardwareMutex(const wchar_t* name) noexcept {
    return CreateMutexExW(nullptr,name,0,SYNCHRONIZE|MUTEX_MODIFY_STATE);
}
}
