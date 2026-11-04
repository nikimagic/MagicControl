#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "Theme.h"
#include "MagicControl/OsdMessage.h"

namespace mc::gui::osd {

inline std::uint64_t now() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Measured from Fan_X15Pro/OsdWindow.cs. Both frontends share these dimensions,
// colours and timing; the platform adapter decides how a card is presented.
inline constexpr int kWidth=390, kHeight=160, kRadius=14, kPadding=22;
inline constexpr int kIconWidth=60, kIconHeight=54, kTitleHeight=53, kDetailHeight=48;
inline constexpr int kIconGap=14, kDetailTop=55, kBarBottom=26, kBarHeight=5;
inline constexpr int kLeft=28, kHoldMs=2000, kFadeMs=220, kLevelMs=220, kActivityMs=480;
inline constexpr ColorRef kBackground=rgb(24,23,42), kBorder=rgb(66,60,91);
inline constexpr ColorRef kAccent=rgb(160,144,255), kError=rgb(255,149,149);
inline constexpr ColorRef kText=rgb(248,246,255), kDetail=rgb(190,186,215), kTrack=rgb(61,56,82);

class State {
public:
    bool visible{};
    Message message;
    std::uint64_t revision{};

    void present(Message next, std::uint64_t at) {
        const auto origin=next.receivedAt?next.receivedAt:(next.issuedAt?next.issuedAt:at);
        if (origin < latestEvent_) return;
        latestEvent_=origin;
        latestCategoryEvent_[next.key]=origin;
        std::erase_if(operations_,[origin](const auto& item){return item.second.receivedAt && item.second.receivedAt<origin;});
        if (next.pending && visible && !message.pending && message.key==next.key) {
            const auto received=next.receivedAt; next=message; next.receivedAt=received;
        }
        active_=0;
        if (deferred_ && deferred_->key==next.key) deferred_.reset();
        core(std::move(next),at,true);
    }
    void begin(std::uint64_t token, Message next, std::uint64_t at) {
        if (!next.receivedAt && !next.issuedAt) next.issuedAt=at;
        const auto origin=next.receivedAt?next.receivedAt:next.issuedAt;
        if(next.receivedAt && origin<latestEvent_)return;
        for (const auto& [existing, message] : operations_) {
            const auto previous=message.receivedAt?message.receivedAt:message.issuedAt;
            if(message.key==next.key && previous>origin)return;
            (void)existing;
        }
        std::erase_if(operations_,[origin](const auto& item){return item.second.receivedAt && item.second.receivedAt<origin;});
        std::erase_if(operations_,[&](const auto& item){return item.second.key==next.key;});
        // A new request supersedes a completed, deferred result of this category.
        // Otherwise an old result could reappear after the new request finishes.
        if(deferred_&&deferred_->key==next.key)deferred_.reset();
        if(active_&&!operations_.contains(active_))active_=0;
        operations_[token]=next; latestOperation_[next.key]=token;
        latestCategoryEvent_[next.key]=std::max(latestCategoryEvent_[next.key],origin);
        if (origin>=latestEvent_) {
            latestEvent_=origin;
            active_=token;
            if(next.receivedAt && visible && !message.error && !message.pending && message.key==next.key) {
                const auto received=next.receivedAt;next=message;next.receivedAt=received;
            } else next.pending=true;
            core(std::move(next),at,true);
        }
    }
    void complete(std::uint64_t token, Message next, std::uint64_t at) {
        auto found=operations_.find(token);
        if (found==operations_.end()) return;
        const Message original=found->second; operations_.erase(found);
        if (latestOperation_[original.key]!=token) return;
        const auto origin=original.receivedAt?original.receivedAt:original.issuedAt;
        // A later observed value of this category wins over an older successful
        // command callback. Keep genuine failures visible for diagnosis.
        if(!next.error&&origin<latestCategoryEvent_[original.key]){
            if(active_==token)active_=0;
            return;
        }
        next.pending=false;
        if(!next.receivedAt&&!next.issuedAt){next.receivedAt=original.receivedAt;next.issuedAt=original.issuedAt;}
        if (active_!=token) {
            if (!original.receivedAt || next.error) deferred_=std::move(next);
            return;
        }
        active_=0; core(std::move(next),at,false); hideAt_=at+kHoldMs;
    }
    void dismiss() {
        visible=false; active_=0; operations_.clear(); latestOperation_.clear(); latestCategoryEvent_.clear(); deferred_.reset(); ++revision;
    }
    void tick(std::uint64_t at) {
        if (!visible || active_ || at<hideAt_) return;
        if (deferred_) {
            auto next=std::move(*deferred_); deferred_.reset(); core(std::move(next),at,true);
        } else if (!operations_.empty()) {
            const auto& latest=*operations_.rbegin(); active_=latest.first; core(latest.second,at,true);
        } else if (at>=hideAt_+kFadeMs) dismiss();
    }
    double opacity(std::uint64_t at) const {
        if (!visible) return 0;
        if (active_ || at<=hideAt_) return .95;
        return .95*std::clamp(1.0-static_cast<double>(at-hideAt_)/kFadeMs,0.0,1.0);
    }
    double percent(std::uint64_t at) const {
        if (message.key!="keyboard") return message.percent.value_or(0);
        return from_+(target_-from_)*std::clamp(static_cast<double>(at-levelAt_)/kLevelMs,0.0,1.0);
    }
    double activity(std::uint64_t at) const {
        return activityAt_ && at>=activityAt_ ? std::clamp((kActivityMs-static_cast<double>(at-activityAt_))/260.0,0.0,1.0) : 0;
    }
    bool animating(std::uint64_t at) const {
        return visible && message.key=="keyboard" &&
            ((from_!=target_ && at<levelAt_+kLevelMs) || activity(at)>0);
    }
private:
    void core(Message next, std::uint64_t at, bool restart) {
        if (next.key=="keyboard" && next.percent) {
            const double current=visible && message.key=="keyboard" && message.percent ? percent(at) : lastKeyboard_.value_or(*next.percent);
            const double target=std::clamp(*next.percent,0,100);
            if (target!=target_ || !visible || message.key!="keyboard") {from_=current;target_=target;levelAt_=at;}
            lastKeyboard_=target;
            if (next.receivedAt) activityAt_=at;
        } else {from_=target_=next.percent.value_or(0);activityAt_=0;}
        if (restart || !visible) hideAt_=at+kHoldMs;
        message=std::move(next); visible=true; ++revision;
    }
    std::uint64_t active_{}, hideAt_{}, latestEvent_{}, levelAt_{}, activityAt_{};
    double from_{},target_{};
    std::optional<double> lastKeyboard_;
    std::map<std::uint64_t,Message> operations_;
    std::map<std::string,std::uint64_t> latestOperation_;
    std::map<std::string,std::uint64_t> latestCategoryEvent_;
    std::optional<Message> deferred_;
};
}  // namespace mc::gui::osd
