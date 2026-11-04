#include "MagicControl/ResourceControl.h"
#include "MagicControl/Logging.h"
#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <set>

// Only the audit sink is replaced. ActionLog.cpp and ResourceControl.h are the
// production implementations. No filesystem, GUI, backend or hardware access.
namespace mc {
void recordLog(std::string_view,std::string_view) noexcept {}
}
namespace {
using namespace std::chrono_literals;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Blockers {
    std::mutex gate;std::condition_variable changed;bool release{};int entered{};
    std::set<std::thread::id> threads;
    void block(){std::unique_lock lock(gate);++entered;threads.insert(std::this_thread::get_id());changed.notify_all();changed.wait(lock,[&]{return release;});}
    bool wait(int count){std::unique_lock lock(gate);return changed.wait_for(lock,5s,[&]{return entered==count;});}
    void unblock(){std::lock_guard lock(gate);release=true;changed.notify_all();}
};
auto submit(mc::resource_work::Runtime& runtime,std::function<void()> work,mc::ResourceWorkClass kind){
    auto job=std::make_shared<mc::resource_work::Job>();job->body=std::move(work);runtime.submit(job,kind);return job;
}
void finish(const std::shared_ptr<mc::resource_work::Job>& job){
    std::unique_lock lock(job->gate);check(job->finished.wait_for(lock,5s,[&]{return job->completed;}),"job did not finish");
}
}
int main(){
    auto held=std::make_shared<Blockers>();
    try{
        mc::resource_work::Runtime runtime;
        std::array<std::shared_ptr<mc::resource_work::Job>,3> background;
        for(auto& job:background)job=submit(runtime,[held]{held->block();},mc::ResourceWorkClass::Observation);
        check(held->wait(3),"three observation workers must enter");
        std::atomic<bool> queuedObservation{};
        auto fourth=submit(runtime,[&]{queuedObservation=true;},mc::ResourceWorkClass::Observation);
        std::atomic<bool> clean{},bound{};
        auto interactive=submit(runtime,[&]{clean=mc::audit::capture()==0;mc::audit::Scope action("fixture",{},77);bound=mc::audit::current()==77;},mc::ResourceWorkClass::Interactive);
        finish(interactive);
        check(clean&&bound&&!queuedObservation,"interactive must run while three observations are held and another is queued");
        // Force reuse of the reserved worker while the other three stay held.
        auto throwing=submit(runtime,[]{check(mc::audit::capture()==0,"new job inherited audit context");mc::audit::input("fixture","input");throw 42;},mc::ResourceWorkClass::Interactive);
        finish(throwing);
        clean=false;
        auto next=submit(runtime,[&]{clean=mc::audit::current()==0&&mc::audit::capture()==0;},mc::ResourceWorkClass::Interactive);
        finish(next);check(clean,"exception/input TLS leaked to the next pooled job");
        auto heldInteractive=submit(runtime,[held]{held->block();},mc::ResourceWorkClass::Interactive);
        check(held->wait(4),"four finite workers must be available");
        {std::lock_guard lock(held->gate);check(held->threads.size()==4,"finite pool must have four distinct workers");}
        // Every finite worker is now held; the persistent actor must still run.
        auto actorReady=std::make_shared<std::promise<std::thread::id>>();auto actorFuture=actorReady->get_future();
        mc::ResourceActor actor([actorReady](std::stop_token stop){
            actorReady->set_value(std::this_thread::get_id());
            std::mutex gate;std::condition_variable_any changed;std::unique_lock lock(gate);
            changed.wait(lock,stop,[]{return false;});
        });
        check(actorFuture.wait_for(5s)==std::future_status::ready,"actor consumed the saturated finite pool");
        {std::lock_guard lock(held->gate);check(!held->threads.contains(actorFuture.get()),"actor reused a finite worker");}
        actor.request_stop();actor.join();
        held->unblock();for(const auto& job:background)finish(job);finish(heldInteractive);finish(fourth);
        check(queuedObservation,"queued observation was lost");
        std::atomic<bool> released{};
        struct Capture {std::atomic<bool>& released;~Capture(){released=true;}};
        auto capture=std::make_shared<Capture>(released);
        auto completed=submit(runtime,[capture]{},mc::ResourceWorkClass::Interactive);capture.reset();finish(completed);
        check(released,"completion published before operation captures were released");
        std::cout<<"PASS: four workers, reserved interactive, actor isolation, TLS cleanup, capture lifetime\n";
    }catch(const std::exception& error){held->unblock();std::cerr<<error.what()<<'\n';return 1;}
}
