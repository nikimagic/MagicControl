#pragma once
#include "ResourceControl.h"
#include <string>
#include <optional>
#include <functional>
#include <utility>
namespace mc {
struct DisplayRotationResult {std::string detail;bool success{};std::uint64_t receivedAt{},token{};};
// Finite application operation: OEM press parity, timestamp, lane and completion
// belong here; each OS retains its native readback adapter.
class DisplayRotationOperations {
public:
    DisplayRotationOperations(std::function<std::string()> action,std::function<std::uint64_t(std::uint64_t)> begin,std::function<void()> ready)
        :action_(std::move(action)),begin_(std::move(begin)),ready_(std::move(ready)){}
    ~DisplayRotationOperations(){shutdown();}
    void request(std::uint64_t receivedAt);
    std::optional<DisplayRotationResult> take();
    void continueQueued();
    void shutdown(){closed_=true;queued_=false;worker_.join();}
private:
    std::function<std::string()> action_;ResourceWorker worker_;std::function<std::uint64_t(std::uint64_t)> begin_;std::function<void()> ready_;
    DisplayRotationResult pending_;bool busy_{},queued_{},immediate_{},closed_{};
    std::uint64_t receivedAt_{},token_{};
};

inline void DisplayRotationOperations::request(std::uint64_t receivedAt){
    if(closed_)return;queued_=!queued_;receivedAt_=receivedAt;continueQueued();
}
inline void DisplayRotationOperations::continueQueued(){
    if(closed_||busy_||!queued_)return;
    queued_=false;busy_=true;pending_={};immediate_=false;token_=begin_?begin_(receivedAt_):0;
    try{worker_.launch([this,at=receivedAt_,id=audit::capture()]{
        audit::Scope trace("display.rotate","\"received_at_ms\":"+std::to_string(at),id,true);
        try{pending_.detail=action_();pending_.success=true;}
        catch(const std::exception& error){pending_.detail=error.what();}
        catch(...){pending_.detail="Ошибка обработчика поворота экрана.";}
        trace.finish(pending_.success?"readback_confirmed":"failed","\"message\":"+audit::quote(pending_.detail));
    },ready_,ResourceWorkClass::Interactive);}
    catch(...){pending_.detail="Не удалось запустить обработчик поворота экрана.";immediate_=true;if(ready_)ready_();}
}
inline std::optional<DisplayRotationResult> DisplayRotationOperations::take(){
    if(!immediate_&&!worker_.takeCompletion())return {};
    immediate_=false;busy_=false;
    if(worker_.failure()){pending_.success=false;pending_.detail="Ошибка обработчика поворота экрана.";}
    if(!pending_.success)queued_=false;
    pending_.receivedAt=receivedAt_;pending_.token=token_;
    return std::exchange(pending_,{});
}

}
