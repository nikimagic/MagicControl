#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <format>
#include <stdexcept>
#include <string>
namespace mc::windows {
// Read-only admission shared by native battery and explicit factory diagnostics.
// Never stop a service, rename a process or treat enumeration failure as free.
inline std::string uniwillControlOwner(){
    const HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Не удалось проверить процессы OEM");
    struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{h};
    PROCESSENTRY32W p{sizeof(p)};
    if(!Process32FirstW(h,&p))throw std::runtime_error("Не удалось прочитать процессы OEM");
    do{if(!_wcsicmp(p.szExeFile,L"GCUService.exe")||!_wcsicmp(p.szExeFile,L"AiStoneService.exe"))
        return std::format("{} (PID {})",!_wcsicmp(p.szExeFile,L"GCUService.exe")?"GCUService.exe":"AiStoneService.exe",p.th32ProcessID);
    }while(Process32NextW(h,&p));
    if(GetLastError()!=ERROR_NO_MORE_FILES)throw std::runtime_error("Не удалось завершить проверку процессов OEM");
    return {};
}
}
