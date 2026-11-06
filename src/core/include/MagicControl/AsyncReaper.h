#pragma once

#include <chrono>
#include <thread>
#include <utility>

namespace mc::detail {
// Retain the callable and its capabilities until wait has actually finished.
// Launch and detach are injectable for offline failure tests. Both fallback
// paths run on the caller's existing worker, never a GUI-owned thread.
template<class Wait,class Launch,class Detach>
void startReaper(Wait wait,Launch launch,Detach detach) noexcept {
    auto guarded=[wait=std::move(wait)]()mutable noexcept{
        for(;;){try{wait();return;}catch(...){std::this_thread::sleep_for(std::chrono::seconds(1));}}
    };
    std::thread worker;
    try{worker=launch(guarded);}catch(...){guarded();return;}
    if(!worker.joinable()){guarded();return;}
    try{detach(worker);}catch(...){
        // detach may have failed AFTER the thread was created. Join that same
        // thread; starting the wait again could race a handle/PID close.
        while(worker.joinable())try{worker.join();}catch(...){std::this_thread::sleep_for(std::chrono::seconds(1));}
    }
    // Defensive for an injected detach which returned without detaching.
    while(worker.joinable())try{worker.join();}catch(...){std::this_thread::sleep_for(std::chrono::seconds(1));}
}
template<class Wait> void startReaper(Wait wait) noexcept {
    startReaper(std::move(wait),[](auto work){return std::thread(std::move(work));},[](std::thread& worker){worker.detach();});
}
}
