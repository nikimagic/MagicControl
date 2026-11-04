// Restored SSH frontend from the pre-0.1.186 CLI. All control now delegates
// to the current typed core/platform API; no historical raw EC writers.
#include "CurrentCommands.h"
#include "../platform/common/DiagnosticDumpInternal.h"
#if defined(_WIN32)
#include <windows.h>
#include "../platform/windows/DiagnosticWmiWindows.h"
#endif
#include <string>
#include <vector>
namespace {
int entry(const std::vector<std::string>& args) {
    if(auto result=mc::diagnostics::runFastDiagnosticHelper(args))return *result;
#if defined(_WIN32)
    if(auto result=mc::windows::wmi::runDiagnosticHelper(args))return *result;
#endif
    return mc::cli::run(std::vector<std::string>(args.begin()+1,args.end()));
}
}
#if defined(_WIN32)
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);std::vector<std::string> args;
    for(int i=0;i<argc;++i){
        const int n=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,nullptr,0,nullptr,nullptr);
        std::string value(n?static_cast<std::size_t>(n):1,'\0');
        if(n)WideCharToMultiByte(CP_UTF8,0,argv[i],-1,value.data(),n,nullptr,nullptr);
        value.pop_back();args.push_back(std::move(value));
    }
    return entry(args);
}
#else
int main(int argc,char** argv) {return entry(std::vector<std::string>(argv,argv+argc));}
#endif
