#include "AdlxRuntime.h"
#include <stdexcept>
namespace mc::windows {
AdlxRuntime::AdlxRuntime(){
    library=LoadLibraryExW(L"amdadlx64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!library)throw std::runtime_error("AMD ADLX: DLL драйвера недоступна");
    using Query=int(__cdecl*)(std::uint64_t*);using Initialize=int(__cdecl*)(std::uint64_t,adlx::IADLXSystem**);
    auto query=reinterpret_cast<Query>(GetProcAddress(library,"ADLXQueryFullVersion"));
    auto initialize=reinterpret_cast<Initialize>(GetProcAddress(library,"ADLXInitialize"));
    terminate=reinterpret_cast<Terminate>(GetProcAddress(library,"ADLXTerminate"));
    std::uint64_t version{};
    // The client version describes the headers/vtables compiled into this
    // binary. Passing the DLL's own version would hide an incompatible SDK.
    if(!query||!initialize||!terminate||query(&version)!=0||initialize(ADLX_FULL_VERSION,&system)!=0||!system){FreeLibrary(library);library=nullptr;throw std::runtime_error("AMD ADLX: драйвер не подтвердил совместимость с SDK2.0; доступные ADL/Windows источники сохраняются");}
}
AdlxRuntime::~AdlxRuntime(){if(system&&terminate)terminate();if(library)FreeLibrary(library);}
std::shared_ptr<AdlxRuntime> acquireAdlxRuntime(){static std::mutex gate;static std::weak_ptr<AdlxRuntime> weak;std::lock_guard lock(gate);auto r=weak.lock();if(!r){r=std::make_shared<AdlxRuntime>();weak=r;}return r;}
}
