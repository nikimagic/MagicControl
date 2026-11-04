#include "MagicControl/CustomPresetValidation.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/Overclock.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace {
unsigned checks{};
void check(bool valid,std::string_view profile,const char* what){
    ++checks;if(!valid){std::fprintf(stderr,"Profile %.*s: %s\n",int(profile.size()),profile.data(),what);std::exit(1);}
}
bool accepts(const mc::DeviceProfile& profile,const mc::CustomPreset& value){
    try{mc::validateCustomPresetForProfile(profile,value);return true;}catch(const std::runtime_error&){return false;}
}
}
int main(){
    std::set<std::string_view> ids;unsigned profiles{};
    for(const auto* profile:mc::deviceProfiles()){
        check(profile&&!profile->id.empty()&&profile->id!="unknown","registry","valid profile");
        check(ids.insert(profile->id).second,profile->id,"duplicate profile id");++profiles;
        mc::BoardDescriptor actual;actual.manufacturer=profile->manufacturer;actual.baseBoard=profile->baseBoard;actual.productName=profile->productName;
        check(mc::overclockObservationSupported(actual)==(profile->id=="wu-jie-15-xpro"),profile->id,"OvClock exact board admission");
        const auto& policy=profile->customPreset;
        for(int n:profile->fanPointCounts)check(n>=1&&n<=16,profile->id,"fan point count");
        for(int max:policy.maximum)check(max>=policy.minimum,profile->id,"power range");
        mc::CustomPreset p;p.power.fill(policy.minimum);
        check(accepts(*profile,p),profile->id,"minimum limits accepted");
        p.power[0]=policy.minimum-1;check(!accepts(*profile,p),profile->id,"below minimum rejected");
        p.power.fill(policy.minimum);p.power[2]=policy.maximum[2]+1;
        check(!accepts(*profile,p),profile->id,"above maximum rejected");
        p.power.fill(policy.minimum);++p.power[0];
        check(accepts(*profile,p)==!policy.orderedPairs[0],profile->id,"first pair policy");
        p.power.fill(policy.minimum);++p.power[1];
        check(accepts(*profile,p)==!policy.orderedPairs[1],profile->id,"second pair policy");
        p.power={};check(accepts(*profile,p),profile->id,"unset power preserved");
        p.powerRecipe=4;check(!accepts(*profile,p),profile->id,"invalid recipe");p.powerRecipe=0;
        p.powerOptions["bad key"]=1;check(!accepts(*profile,p),profile->id,"invalid option name");p.powerOptions.clear();
        for(int ch=0;ch<2;++ch){
            const int last=profile->fanPointCounts[ch]-1;p.curves[ch][last].duty=101;
            check(!accepts(*profile,p),profile->id,"invalid active fan point rejected");p.curves[ch][last].duty=0;
            if(last<15){p.curves[ch][last+1].duty=101;
                check(accepts(*profile,p),profile->id,"inactive fan points excluded");
                p.curvesReady=false;check(!accepts(*profile,p),profile->id,"unready draft validates all points");
                p.curvesReady=true;p.curves[ch][last+1].duty=0;}
        }
    }
    const auto& unknown=mc::deviceProfile({});
    check(!ids.contains(unknown.id),"unknown","unknown board is not a registered writable profile");
    check(!mc::overclockObservationSupported({}),"unknown","OvClock must not read unknown hardware");
    for(const auto id:{"wu-jie-15-xpro","huawei-hlyl-wxx9","minisforum-hpbsd","lenovo-legion-kwcn"})
        check(ids.contains(id),"registry","expected reviewed profile missing");
    std::printf("Custom profiles: %u profiles, %u checks; no hardware access\n",profiles,checks);
}
