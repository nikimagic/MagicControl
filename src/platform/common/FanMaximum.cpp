#include "MagicControl/FanMaximum.h"
#include <algorithm>
#include <chrono>
#include <thread>
namespace mc {
FanMaximumResult measureMaximumFans(std::string source,const std::function<void()>& begin,
    const std::function<FanMaximumSample()>& read,const std::function<void()>& restore,std::stop_token token){
    FanMaximumResult result;result.source=std::move(source);
    if(token.stop_requested()){result.status="Проба отменена до изменения управления";return result;}
    try{
        const auto baseline=read();result.beforeRpm=baseline.rpm;
        result.attempted=true;result.restored=false;begin();result.commandApplied=true;
        const auto start=std::chrono::steady_clock::now();
        constexpr auto hold=std::chrono::seconds(30);
        const auto holdMs=std::chrono::duration_cast<std::chrono::milliseconds>(hold).count();
        while(!token.stop_requested()){
            auto sample=read();sample.elapsedMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
            for(int ch=0;ch<2;++ch)if(sample.rpm[ch]&&*sample.rpm[ch]>0&&*sample.rpm[ch]<=20000){
                result.peakRpm[ch]=std::max(result.peakRpm[ch].value_or(0),*sample.rpm[ch]);
                // Already-Max baselines may legitimately show no increase.
                // In that case retain the plateau, without claiming a response.
                if(baseline.rpm[ch]&&*baseline.rpm[ch]>=0&&
                   (!baseline.percent[ch]||*baseline.percent[ch]<99.)&&
                   *sample.rpm[ch]>*baseline.rpm[ch]+std::max(100,*baseline.rpm[ch]*3/100))
                    result.responseObserved=true;
            }
            result.samples.push_back(sample);
            bool stable=result.samples.size()>=4&&sample.elapsedMs>=holdMs;
            if(stable)for(int ch=0;ch<2;++ch){int low=20001,high=0;
                for(std::size_t i=result.samples.size()-4;i<result.samples.size();++i){
                    const auto v=result.samples[i].rpm[ch];if(!v||*v<=0||*v>20000){stable=false;break;}
                    low=std::min(low,*v);high=std::max(high,*v);
                }
                stable=stable&&high-low<=std::max(100,high*3/100);
            }
            // A brief plateau during firmware ramp-up is not the maximum.
            // Hold the requested Max for the full 30 seconds unless cancelled.
            if(sample.elapsedMs>=holdMs){result.settled=stable;break;}
            for(int i=0;i<5&&!token.stop_requested();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        result.status=token.stop_requested()?"Проба отменена":result.settled?"Max/100% удерживался30с; RPM стабилизировались":"Max/100% удерживался30с; стабилизация обоих RPM не подтверждена";
    }catch(const std::exception& e){result.status=e.what();}
    // Cancellation never skips cleanup. Only the transport can decide whether
    // sleep, an external owner or a lost device prevents a verified restoration.
    if(result.attempted)try{restore();result.restored=true;}
    catch(const std::exception& e){result.restored=false;result.status+="; ВОЗВРАТ НЕ ПОДТВЕРЖДЁН: "+std::string(e.what());}
    return result;
}
}
