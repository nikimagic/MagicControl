#pragma once
#include <cstdint>
namespace mc::gui::ui {
// Card-level scheduling only. Native controls keep text, stepping and focus.
struct NumericCommitDelay {
    static constexpr std::uint64_t stepPauseMs=2000;
    bool pending{};
    std::uint64_t dueMs{};
    void schedule(std::uint64_t now,std::uint64_t delay=stepPauseMs){pending=true;dueMs=now+delay;}
    bool ready(std::uint64_t now)const{return pending&&now>=dueMs;}
    void cancel(){pending=false;}
};
}
