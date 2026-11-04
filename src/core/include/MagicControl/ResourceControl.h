#pragma once
#include "ResourceCatalog.h"
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <atomic>
#include <algorithm>
#include <stop_token>
#include <type_traits>
#include <exception>
#include <functional>
#include <optional>
#include <thread>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include "DeviceOperation.h"

namespace mc {
enum class ResourceWorkClass {Interactive,Observation};
// One process-owned executor. Page/model clients own lanes and results, never
// threads. Adapters must submit finite operations, not permanent cooling loops.
namespace resource_work {
struct Job {
    std::function<void()> body;std::mutex gate;std::condition_variable finished;
    bool completed{};
};
inline void execute(const std::shared_ptr<Job>& job)noexcept{
    audit::TaskContext context;
    auto body=std::move(job->body);
    try{body();}catch(...){}
    body={}; // Release operation capabilities before a client join returns.
    {std::lock_guard lock(job->gate);job->completed=true;}job->finished.notify_all();
}
class Runtime {
    struct State {
        std::mutex gate;std::condition_variable changed;
        std::deque<std::shared_ptr<Job>> interactive,observation;bool stopping{};
    };
    std::shared_ptr<State> state_{std::make_shared<State>()};std::vector<std::thread> workers_;
    static void run(const std::shared_ptr<State>& state,bool reserved){for(;;){std::shared_ptr<Job> job;
        {std::unique_lock lock(state->gate);state->changed.wait(lock,[&]{return state->stopping||!state->interactive.empty()||(!reserved&&!state->observation.empty());});
         if(state->interactive.empty()&&(reserved||state->observation.empty())){if(state->stopping)return;continue;}
         auto& queue=state->interactive.empty()?state->observation:state->interactive;job=std::move(queue.front());queue.pop_front();}
        execute(job);
    }}
    void stop(){ {std::lock_guard lock(state_->gate);state_->stopping=true;}state_->changed.notify_all();}
public:
    Runtime(){try{for(unsigned i=0;i<4;++i)workers_.emplace_back([state=state_,reserved=i==0]{run(state,reserved);});}catch(...){stop();for(auto& worker:workers_)if(worker.joinable())worker.join();throw;}}
    // Domain clients join their finite work before destroying backends. Detached
    // INFO/WMI observations keep State alive and retain their existing close
    // contract: process teardown never joins an unbounded external RPC call.
    ~Runtime(){stop();for(auto& worker:workers_)if(worker.joinable())worker.detach();}
    void submit(std::shared_ptr<Job> job,ResourceWorkClass kind){
        {std::lock_guard lock(state_->gate);if(state_->stopping)throw std::logic_error("Resource executor stopped");
         (kind==ResourceWorkClass::Interactive?state_->interactive:state_->observation).push_back(std::move(job));}
        state_->changed.notify_all();
    }
};
inline Runtime& runtime(){static Runtime instance;return instance;}
}
// Detached ownership is explicit and reserved for a job that owns all its
// captured capabilities (INFO). Model lanes below always wait on destruction.
inline void submitDetachedResource(std::function<void()> work,ResourceWorkClass kind=ResourceWorkClass::Observation){
    auto job=std::make_shared<resource_work::Job>();job->body=std::move(work);resource_work::runtime().submit(std::move(job),kind);
}
namespace resource_actor {
struct Actor {std::stop_source stop;std::thread worker;std::atomic<bool> started{};};
class Registry {
    std::mutex gate_;std::vector<std::shared_ptr<Actor>> actors_;
public:
    std::shared_ptr<Actor> start(std::function<void(std::stop_token)> work){
        auto actor=std::make_shared<Actor>();{std::lock_guard lock(gate_);actors_.push_back(actor);}
        try{actor->worker=std::thread([actor,work=std::move(work)]{actor->started.wait(false,std::memory_order_acquire);work(actor->stop.get_token());});}
        catch(...){release(actor);throw;}return actor;
    }
    void release(const std::shared_ptr<Actor>& actor){std::lock_guard lock(gate_);std::erase(actors_,actor);}
};
inline Registry& registry(){
    // Process-lifetime owner: normal leases stop/join/release explicitly. An
    // orphaned diagnostic RPC must not introduce a static-destruction join or
    // leave its actor with a pointer to a destroyed registry during app exit.
    static Registry* instance=new Registry;return *instance;
}
}
// A lease on the process-owned controller actor registry. Continuous control
// policies keep their dedicated thread identity, stop token, notify and join;
// they never run behind slow finite WMI jobs. No backend creates its own thread.
class ResourceActor {
    std::shared_ptr<resource_actor::Actor> actor_;
public:
    ResourceActor()=default;
    template<class Work> requires (!std::is_same_v<std::remove_cvref_t<Work>,ResourceActor> &&
        (std::is_invocable_v<std::decay_t<Work>&,std::stop_token>||std::is_invocable_v<std::decay_t<Work>&>))
    explicit ResourceActor(Work&& work){
        auto owned=std::make_shared<std::decay_t<Work>>(std::forward<Work>(work));
        actor_=resource_actor::registry().start([owned]([[maybe_unused]] std::stop_token stop){
            if constexpr(std::is_invocable_v<std::decay_t<Work>&,std::stop_token>)(*owned)(stop);else (*owned)();
        });
        // Match direct jthread construction: publish the lease/identity before
        // a controller callback can inspect its own worker thread.
        actor_->started.store(true,std::memory_order_release);actor_->started.notify_one();
    }
    ~ResourceActor(){request_stop();if(joinable())join();}
    ResourceActor(const ResourceActor&)=delete;ResourceActor& operator=(const ResourceActor&)=delete;
    ResourceActor(ResourceActor&& other)noexcept:actor_(std::move(other.actor_)){}
    ResourceActor& operator=(ResourceActor&& other)noexcept{if(this!=&other){request_stop();if(joinable())join();actor_=std::move(other.actor_);}return *this;}
    bool request_stop()noexcept{return actor_&&actor_->stop.request_stop();}
    bool joinable()const noexcept{return actor_&&actor_->worker.joinable();}
    void join(){if(joinable()){actor_->worker.join();resource_actor::registry().release(actor_);}}
    std::thread::id get_id()const noexcept{return actor_?actor_->worker.get_id():std::thread::id{};}
    std::stop_token get_stop_token()const noexcept{return actor_?actor_->stop.get_token():std::stop_token{};}
};
class ResourceWorker {
public:
    // Isolated failure fixtures may inject a thread. Production uses only the
    // common Runtime; no GUI/platform factory creates a production worker.
    using ThreadFactory=std::function<std::thread(std::function<void()>)>;
    using Thread=std::thread;
    explicit ResourceWorker(ThreadFactory factory={}):factory_(std::move(factory)){}
    ~ResourceWorker(){join();}
    ResourceWorker(const ResourceWorker&)=delete;
    ResourceWorker& operator=(const ResourceWorker&)=delete;
    void launch(std::function<void()> work,std::function<void()> ready,ResourceWorkClass kind=ResourceWorkClass::Observation){
        if(busy_)throw std::logic_error("Resource lane already running");
        join();busy_=true;failure_={};done_.store(false,std::memory_order_release);
        try{
            job_=std::make_shared<resource_work::Job>();
            job_->body=[this,work=std::move(work),ready=std::move(ready)]{
                try{work();}catch(...){failure_=std::current_exception();}
                done_.store(true,std::memory_order_release);
                try{if(ready)ready();}catch(...){}
            };
            if(factory_){injected_=factory_([job=job_]{resource_work::execute(job);});if(!injected_.joinable())throw std::runtime_error("Resource worker was not created");}
            else resource_work::runtime().submit(job_,kind);
        }catch(...){if(job_)job_->body={};job_.reset();busy_=false;done_.store(false,std::memory_order_release);throw;}
    }
    bool takeCompletion(){if(!done_.exchange(false,std::memory_order_acq_rel))return false;join();busy_=false;return true;}
    bool busy()const noexcept{return busy_;}
    std::exception_ptr failure()const noexcept{return failure_;} // After take/join.
    void join(){
        if(injected_.joinable())injected_.join();
        if(job_){std::unique_lock lock(job_->gate);job_->finished.wait(lock,[&]{return job_->completed;});lock.unlock();job_.reset();}
        busy_=false;
    }
    void setFactory(ThreadFactory factory){if(busy_)throw std::logic_error("Resource lane busy");factory_=std::move(factory);}
private:
    ThreadFactory factory_;
    std::shared_ptr<resource_work::Job> job_;std::thread injected_;
    std::atomic<bool> done_{};bool busy_{};std::exception_ptr failure_;
};
// The queue owns admitted reservations and the resource lane. Request is domain
// data (not a GUI callback). One interactive request may follow a passive read.
template<class Request> class ResourceDispatch {
public:
    using ThreadFactory=ResourceWorker::ThreadFactory;
    enum class Decision {Start,Queue,Preview,NoBackend,Busy,PassiveGateBusy,PassiveAlreadyRunning,GateBusy};
    struct Admission{Decision decision;std::shared_ptr<DeviceOperation> reservation;};
    explicit ResourceDispatch(ThreadFactory factory={}):worker_(std::move(factory)){}
    Admission admit(bool preview,bool backend,bool passive,std::shared_ptr<DeviceOperation> reservation={}){
        if(preview)return {Decision::Preview,{}};
        if(!passive&&!backend)return {Decision::NoBackend,{}};
        if(busy_&&(!reading_||queued_))return {Decision::Busy,{}};
        if(!passive&&reservation&&!deviceOperations().owns(reservation))return {Decision::GateBusy,{}};
        if(passive&&deviceOperations().busy())return {Decision::PassiveGateBusy,{}};
        if(busy_&&passive)return {Decision::PassiveAlreadyRunning,{}};
        if(!passive&&!reservation)reservation=deviceOperations().tryBegin();
        if(!passive&&!reservation)return {Decision::GateBusy,{}};
        return {busy_?Decision::Queue:Decision::Start,std::move(reservation)};
    }
    void enqueue(Request request){if(!busy_||!reading_||queued_)throw std::logic_error("Resource queue already occupied");priority_.store(true,std::memory_order_release);queued_=std::move(request);}
    bool hasQueued()const noexcept{return queued_.has_value();}
    std::optional<Request> takeQueued(){auto next=std::move(queued_);queued_.reset();return next;}
    void begin(bool passive){if(busy_)throw std::logic_error("Resource operation already active");busy_=true;reading_=passive;if(!passive)priority_.store(false,std::memory_order_release);}
    void launch(std::function<void()> work,std::function<void()> ready){
        if(!busy_)throw std::logic_error("Resource operation not admitted");
        try{worker_.launch(std::move(work),std::move(ready),reading_?ResourceWorkClass::Observation:ResourceWorkClass::Interactive);}
        catch(...){busy_=false;reading_=false;throw;}
    }
    bool takeCompletion(){return worker_.takeCompletion();}
    std::exception_ptr unhandledFailure()const noexcept{return worker_.failure();}
    void finish()noexcept{busy_=false;reading_=false;}
    void join(){worker_.join();}
    bool busy()const noexcept{return busy_;}
    bool reading()const noexcept{return reading_;}
    bool priorityPending()const noexcept{return priority_.load(std::memory_order_acquire);}
private:
    bool busy_{},reading_{};std::optional<Request> queued_;
    std::atomic<bool> priority_{};ResourceWorker worker_;
};
// A route is scoped to the already selected runtime adapter. The instance
// name is local to that adapter and is never a claimed physical identity.
// A route describes implementation, not hardware availability or permission.
struct ResourceControlKey {
    ResourceKind kind{ResourceKind::Other};
    std::string instance;
    std::string operation;
    friend bool operator==(const ResourceControlKey&,const ResourceControlKey&)=default;
};
// Immutable, typed route table for long-lived application adapters. Unlike an
// observation facet, its presence means only that code is registered: every
// handler still executes the original runtime admission and readback checks.
// Route descriptors are copied and owned; no pointer into a temporary
// registration array or its names can outlive the caller accidentally.
template<class Request,class Context,class Result>
class ResourceControlTable {
public:
    using Handler=Result (*)(const Request&,const Context&);
    struct Binding {ResourceKind kind;std::string instance,operation;Handler invoke;};
    explicit ResourceControlTable(std::span<const Binding> bindings):bindings_(bindings.begin(),bindings.end()){
        for(std::size_t i=0;i<bindings_.size();++i){const auto& entry=bindings_[i];
            if(entry.instance.empty()||entry.operation.empty()||!entry.invoke)throw std::invalid_argument("Resource control: incomplete binding");
            for(std::size_t j=0;j<i;++j)if(bindings_[j].kind==entry.kind&&bindings_[j].instance==entry.instance&&bindings_[j].operation==entry.operation)
                throw std::invalid_argument("Resource control: duplicate binding");}
    }
    [[nodiscard]] bool hasRoute(const ResourceControlKey& key) const noexcept{
        for(const auto& entry:bindings_)if(entry.kind==key.kind&&entry.instance==key.instance&&entry.operation==key.operation)return true;
        return false;
    }
    Result execute(const ResourceControlKey& key,const Request& request,const Context& context) const{
        for(const auto& entry:bindings_)if(entry.kind==key.kind&&entry.instance==key.instance&&entry.operation==key.operation)
            return entry.invoke(request,context);
        throw std::invalid_argument("Resource control: unsupported route");
    }
private:
    std::vector<Binding> bindings_;
};
}
