#pragma once
#include <windows.h>
#include <stdexcept>

namespace mc::huawei {
// Named Windows mutexes are reentrant on the owning thread. The same gate is
// used by sessions and whole charge transactions; it cannot exclude OEM apps
// that do not participate in this mutex.
class WmiGate {
    HANDLE mutex_{};bool locked_{};
public:
    explicit WmiGate(const wchar_t* name=L"Global\\MagicControl.Huawei.HWMI.v2"){
        mutex_=CreateMutexW(nullptr,FALSE,name);
        if(!mutex_)throw std::runtime_error("Huawei: mutex недоступен");
        const auto result=WaitForSingleObject(mutex_,5000);
        locked_=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;
        if(!locked_){CloseHandle(mutex_);mutex_=nullptr;throw std::runtime_error("Huawei WMI занят другой операцией");}
    }
    ~WmiGate(){if(locked_)ReleaseMutex(mutex_);if(mutex_)CloseHandle(mutex_);}
    WmiGate(const WmiGate&)=delete;WmiGate& operator=(const WmiGate&)=delete;
};
}
