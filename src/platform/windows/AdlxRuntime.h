#pragma once
#include <memory>
#include <cstdint>
#include <mutex>
#include <utility>
#include <windows.h>
#include "../../third_party/adlx/include/ISystem.h"
namespace mc::windows {
template<class T>struct AdlxRef {
    T* p{};
    AdlxRef()=default;AdlxRef(const AdlxRef&)=delete;
    AdlxRef(AdlxRef&& other)noexcept:p(std::exchange(other.p,nullptr)){}
    ~AdlxRef(){if(p)p->Release();}
    T* operator->()const{return p;}T** out(){return &p;}
    explicit operator bool()const{return p!=nullptr;}
};
// All AMD monitoring/tuning clients share one process-wide ADLX initialization.
// The installed AMD driver supplies the DLL; no SDK DLL is redistributed.
class AdlxRuntime {
public:
    AdlxRuntime();~AdlxRuntime();
    adlx::IADLXSystem* system{};
    std::recursive_mutex gate;
private:HMODULE library{};using Terminate=int(__cdecl*)();Terminate terminate{};
};
std::shared_ptr<AdlxRuntime> acquireAdlxRuntime();
}
