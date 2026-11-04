#pragma once
#include "MagicControl/ResourceControl.h"
#include <windows.h>
#include <atomic>
#include <future>
#include <functional>
#include <utility>
namespace mc::windows {
// Native HWND affinity adapter. The process core actor registry owns the
// permanent thread; this adapter owns ready/close/join and the Windows pump.
// Render callbacks never migrate to a finite worker or another thread.
class WindowEventLoop {
public:
    ~WindowEventLoop(){shutdown();}
    WindowEventLoop()=default;
    WindowEventLoop(const WindowEventLoop&)=delete;WindowEventLoop& operator=(const WindowEventLoop&)=delete;
    bool start(std::function<HWND()> create,std::function<void(HWND)> initialize,std::function<void()> cleanup){
        std::promise<bool> ready;auto result=ready.get_future();
        actor_=ResourceActor([this,create=std::move(create),initialize=std::move(initialize),cleanup=std::move(cleanup),ready=std::move(ready)]()mutable{
            bool signalled=false;
            try{
                const HWND target=create();window_.store(target);ready.set_value(target!=nullptr);signalled=true;
                if(target){initialize(target);MSG message{};
                    while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
            }catch(...){if(!signalled)try{ready.set_exception(std::current_exception());}catch(...){}
                if(const auto target=window_.load())DestroyWindow(target);}
            window_.store(nullptr);cleanup();
        });
        try{return result.get();}catch(...){shutdown();return false;}
    }
    HWND window()const noexcept{return window_.load();}
    bool stopping()const noexcept{return stopping_.load();}
    void windowDestroyed(){window_.store(nullptr);PostQuitMessage(0);}
    void shutdown(){stopping_.store(true);if(const auto target=window_.load())PostMessageW(target,WM_CLOSE,0,0);actor_.request_stop();if(actor_.joinable())actor_.join();}
private:
    ResourceActor actor_;std::atomic<HWND> window_{};std::atomic<bool> stopping_{};
};
}
