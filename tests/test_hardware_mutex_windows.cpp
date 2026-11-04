#include "../src/platform/windows/pawnio/HardwareMutexWindows.h"
#include "../src/platform/windows/XrtNpuReport.h"
#include <sddl.h>
#include <cstdio>
#include <string>
#include <thread>
int main(){
    int failures{};const auto check=[&](bool ok,const char* name){if(!ok){++failures;std::printf("FAIL %s\n",name);}};
    const auto name=L"Local\\MagicControl.MutexTest."+std::to_wstring(GetCurrentProcessId());
    PSECURITY_DESCRIPTOR descriptor{};
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;0x00100001;;;AU)",SDDL_REVISION_1,&descriptor,nullptr))return 1;
    SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
    HANDLE owner=CreateMutexExW(&security,name.c_str(),0,SYNCHRONIZE|MUTEX_MODIFY_STATE);LocalFree(descriptor);
    if(!owner)return 1;
    HANDLE full=CreateMutexW(nullptr,FALSE,name.c_str());const auto error=GetLastError();
    check(!full&&error==ERROR_ACCESS_DENIED,"existing mutex rejects unnecessary full access");if(full)CloseHandle(full);
    HANDLE minimal=mc::windows::openSharedHardwareMutex(name.c_str());check(minimal!=nullptr,"minimal wait/release access succeeds");
    if(minimal){
        check(WaitForSingleObject(owner,0)==WAIT_OBJECT_0,"fixture owner acquired");
        DWORD result{};std::thread other([&]{result=WaitForSingleObject(minimal,20);if(result==WAIT_OBJECT_0)ReleaseMutex(minimal);});other.join();
        check(result==WAIT_TIMEOUT,"existing ownership is not bypassed");ReleaseMutex(owner);
        check(WaitForSingleObject(minimal,20)==WAIT_OBJECT_0,"wait succeeds after owner releases");ReleaseMutex(minimal);CloseHandle(minimal);
    }
    CloseHandle(owner);
    using mc::windows::parseNpuPowerReport;
    check(!parseNpuPowerReport("Estimated Power : N/A\n").watts,"NPU N/A stays unknown");
    check(parseNpuPowerReport("Estimated Power : N/A\n").error.find("N/A")!=std::string::npos,"NPU unavailable status explains report");
    check(parseNpuPowerReport("Estimated Power : 0 Watts\r\n").watts==0.,"NPU numeric zero is valid");
    check(parseNpuPowerReport("Estimated Power : 1.277 Watts\n").watts==1.277,"NPU watts parsed without scaling");
    check(!parseNpuPowerReport("Estimated Power : 1277 mW\n").watts,"unknown units are not interpreted as watts");
    check(!parseNpuPowerReport("Estimated Power : -1 Watts\n").watts,"negative power rejected");
    check(!parseNpuPowerReport("Estimated Power : \n").watts,"empty power rejected");
    std::printf("PCI mutex access and XRT report: %d failure(s); test mutex only, no hardware\n",failures);return failures?1:0;
}
