#include "MagicControl/FanResourceControl.h"
#include "MagicControl/FanControlLimits.h"
#include <iostream>
int main(){try{
    using namespace mc;
    static_assert(fanInput::temperatureMinimum==20&&fanInput::temperatureMaximum==100&&fanInput::percentMinimum==5&&fanInput::percentMaximum==100);
    FanCurveTable curve;for(unsigned i=0;i<16;++i)curve[i]={i==15?255:20+static_cast<int>(i)*5,i==15?100:5+static_cast<int>(i)*6};
    FanWriteRequest request{FanResourceOperation::Restore};request.ecCurve=curve;request.origin=FanWriteOrigin::Restore;request.adoptExisting=true;
    if(!request.ecCurve||*request.ecCurve!=curve||!request.adoptExisting)throw std::runtime_error("FAN request transformed saved curve data");
    CustomFanCurves curves{};for(unsigned ch=0;ch<2;++ch)for(unsigned i=0;i<16;++i)curves[ch][i]={static_cast<int>(i),i==15?255:static_cast<int>(i)-3,static_cast<int>(ch*40+i)};
    request.curves=curves;request.controlled={true,false};request.linked=false;
    if(*request.curves!=curves||request.controlled!=std::array<bool,2>{true,false}||request.linked)throw std::runtime_error("FAN request resampled points/channels");
    FanWriteRequest empty;if(empty.profiles||empty.ecCurve||empty.curves||empty.target)throw std::runtime_error("empty FAN request invented a payload");
    for(auto operation:{FanResourceOperation::Default,FanResourceOperation::Manual,FanResourceOperation::Maximum,FanResourceOperation::Minimum,FanResourceOperation::Online,FanResourceOperation::Restore,FanResourceOperation::SaveDraft})
        if(fanResourceControlKey(operation).operation.empty())throw std::runtime_error("missing FAN intent key");
    bool refused{};try{fanResourceControlKey(static_cast<FanResourceOperation>(42));}catch(const std::invalid_argument&){refused=true;}
    if(!refused||fanInput::validPercent(0)||fanInput::validPercent(101)||!fanInput::validPercent(5)||!fanInput::validPercent(100))throw std::runtime_error("FAN input/type boundary changed");
    std::cout<<"PASS: FAN payload ownership, exact curve copies, channels/origin/adoption and universal input bounds\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
