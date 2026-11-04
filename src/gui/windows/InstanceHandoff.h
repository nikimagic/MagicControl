#pragma once
#include <windows.h>
#include <string>
namespace mc::gui {
// A lease spans MainWindow destruction, so no new controller can start while
// the previous process is still cleaning up. No process termination is used.
inline UINT gracefulInstanceExitMessage(){static const UINT id=RegisterWindowMessageW(L"MagicControl.GracefulInstanceExit.v1");return id;}
inline constexpr LRESULT kInstanceExitProtocol=0x4D434731;
class InstanceLease {
public:
    enum class Result { Start, ActivatedExisting, Failed };
    ~InstanceLease();
    InstanceLease()=default;
    InstanceLease(const InstanceLease&)=delete;
    InstanceLease& operator=(const InstanceLease&)=delete;
    Result acquire(bool logging);
    void releaseLaunchGate();
    void migrateSettings();
    std::wstring error;
    std::string summary;
private:
    HANDLE instance_{},launch_{};
    bool instanceOwned_{},launchOwned_{},logging_{};
    DWORD previousPid_{};
    std::wstring incomingPath_,previousPath_;
    std::string previousVersion_;
    void record(const char* outcome);
    Result fail(std::wstring text);
};
}
