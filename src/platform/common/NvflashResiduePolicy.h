#pragma once
#include <string_view>
namespace mc::diagnostics::nvflash {
// Windows SCM values, asserted against the SDK by the Windows adapter. Pure
// evidence policy only: no registry, service, filesystem, process or driver IO.
inline constexpr unsigned residueStopped=1,residueKernelDriver=1,residueDemandStart=3;
enum class ResidueImage { Untrusted, Missing, PinnedSigned };
inline bool knownResidueService(std::string_view name){
    return name=="nvflash"||name=="nvflash64"||name=="nvflsh64";
}
inline bool knownResidueSys(std::string_view name){
    return name=="nvflash.sys"||name=="nvflash64.sys"||name=="nvflsh64.sys";
}
inline bool residueConfiguration(unsigned type,unsigned start){return type==residueKernelDriver&&start==residueDemandStart;}
struct ResidueProof {
    std::string_view name;
    unsigned state{},type{},start{};
    bool canonicalPath{},noActiveReader{},sameConfiguration{};
    ResidueImage image{ResidueImage::Untrusted};
};
inline bool mayRemoveStoppedResidue(const ResidueProof& p){
    return knownResidueService(p.name)&&p.state==residueStopped&&residueConfiguration(p.type,p.start)&&
        p.canonicalPath&&p.noActiveReader&&p.sameConfiguration&&
        (p.image==ResidueImage::Missing||p.image==ResidueImage::PinnedSigned);
}
// No OS operations here. Test the exact success/error/cleanup precedence used
// by the native runner, including a reader that is still pending after throw.
template<class Read,class Cleanup> unsigned readWithResidueCleanup(Read&& read,Cleanup&& cleanup){
    unsigned code{};
    try{code=read();}
    catch(...){try{cleanup();}catch(...){}throw;}
    cleanup();
    return code;
}
}
