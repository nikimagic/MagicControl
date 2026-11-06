// Production deferred telemetry queue and fake timer/event sources only.
// No Windows message window, registry, COM endpoint, device or firmware.
#include "MagicControl/DeferredTelemetry.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace std::chrono_literals;
void check(bool ok,const char* detail){if(!ok)throw std::runtime_error(detail);}
struct FakeTimer {
    mc::DeferredTelemetryQueue& queue;bool armed{};unsigned reads{};int display{42};
    std::vector<mc::DeferredTelemetryItem> delivered;
    void work(mc::DeferredTelemetryItem item){++reads;delivered.push_back(item);display=43;}
    void post(mc::DeferredTelemetryItem item){if(!queue.submit(item,[&]{work(item);}))armed=true;}
    void tick(std::size_t limit=16){if(!armed)return;queue.drain([&](auto item){work(item);},limit);if(!queue.pending())armed=false;}
};
}
int main(){try{
    using Item=mc::DeferredTelemetryItem;
    mc::TelemetryGate gate;mc::DeferredTelemetryQueue queue(gate);FakeTimer timer{queue};
    auto pause=gate.beginFirmwareCapture({},1s);
    const std::vector expected{Item::AudioBind,Item::AudioRender,Item::AudioCapture,Item::TouchpadInitial,Item::TouchpadPoll,Item::TouchpadKeyPoll};
    // TouchpadPoll models a WM_SETTINGCHANGE/registry notification;
    // TouchpadKeyPoll models the timer started by a physical hotkey.
    for(const auto item:expected)timer.post(item);
    check(timer.armed&&queue.pending()==expected.size()&&timer.reads==0&&timer.display==42,"paused OSD events must retain requests without reading");
    timer.tick();timer.tick();
    check(timer.armed&&queue.pending()==expected.size()&&timer.reads==0&&timer.display==42,"fake retry timer must not read while paused");
    pause.reset();
    timer.tick(2);check(timer.armed&&queue.pending()==4&&timer.reads==2,"bounded fake timer must leave remaining requests queued");
    timer.tick();
    check(!timer.armed&&queue.pending()==0&&timer.reads==expected.size()&&timer.delivered==expected&&timer.display==43,
        "resume must deliver both audio channels, rebind and touchpad requests in order");

    // A prolonged firmware child must not let WM/audio notifications grow an
    // unbounded queue. The endpoint can change while its snapshots are delayed:
    // the final rebind must precede snapshots, even if they arrived first.
    mc::DeferredTelemetryQueue flood(gate);
    unsigned floodReads{};auto floodPause=gate.beginFirmwareCapture({},1s);
    for(unsigned i=0;i<100000;++i){
        const auto item=expected[i%expected.size()];
        flood.submit(item,[&]{++floodReads;});
        check(flood.pending()<=expected.size(),"paused event flood must have bounded storage");
    }
    flood.submit(Item::AudioRender,[&]{++floodReads;});
    flood.submit(Item::AudioCapture,[&]{++floodReads;});
    flood.submit(Item::TouchpadPoll,[&]{++floodReads;});
    flood.submit(Item::AudioBind,[&]{++floodReads;});
    flood.submit(Item::TouchpadKeyPoll,[&]{++floodReads;});
    check(floodReads==0&&flood.pending()<=expected.size(),"flood must not read during capture");
    int boundEndpoint=1,render=-1,capture=-1;bool touchpad=false;
    std::vector<Item> floodOrder;
    floodPause.reset();
    flood.drain([&](Item item){
        floodOrder.push_back(item);
        switch(item){
        case Item::AudioBind:boundEndpoint=2;break; // fake new default endpoint
        case Item::AudioRender:render=boundEndpoint==2?71:11;break;
        case Item::AudioCapture:capture=boundEndpoint==2?43:13;break;
        case Item::TouchpadInitial:case Item::TouchpadPoll:case Item::TouchpadKeyPoll:touchpad=true;break;
        }
    });
    check(flood.pending()==0&&floodOrder.size()<=expected.size(),"resume must do at most one read per request kind");
    for(const auto item:expected)check(std::count(floodOrder.begin(),floodOrder.end(),item)==1,"flood must retain one request of every pending kind");
    const auto bind=std::find(floodOrder.begin(),floodOrder.end(),Item::AudioBind);
    check(bind<std::find(floodOrder.begin(),floodOrder.end(),Item::AudioRender)&&bind<std::find(floodOrder.begin(),floodOrder.end(),Item::AudioCapture),
        "last rebind must precede both pending audio snapshots");
    check(boundEndpoint==2&&render==71&&capture==43&&touchpad,"resumed snapshots must reflect the final fake endpoint and touchpad state");

    mc::DeferredTelemetryQueue ordered(gate);auto orderPause=gate.beginFirmwareCapture({},1s);
    const auto postOrdered=[&](Item item){ordered.submit(item,[]{});};
    postOrdered(Item::AudioRender);postOrdered(Item::TouchpadPoll);postOrdered(Item::AudioCapture);
    postOrdered(Item::TouchpadKeyPoll);postOrdered(Item::AudioRender);postOrdered(Item::AudioBind);
    check(ordered.pending()==5,"duplicates must replace, not duplicate, pending requests");
    std::vector<Item> actualOrder;orderPause.reset();ordered.drain([&](auto item){actualOrder.push_back(item);});
    const std::vector expectedOrder{Item::TouchpadPoll,Item::TouchpadKeyPoll,Item::AudioBind,Item::AudioCapture,Item::AudioRender};
    check(actualOrder==expectedOrder,"last-request order must be preserved, with rebind before dependent audio snapshots");

    unsigned attempts{};auto second=gate.beginFirmwareCapture({},1s);
    timer.post(Item::AudioCapture);second.reset();
    const auto first=queue.drain([&](auto){++attempts;throw std::runtime_error("fake provider error");});
    check(first==0&&attempts==1&&queue.pending()==1,"failed deferred work must stay pending");
    const auto retried=queue.drain([&](auto){++attempts;});
    check(retried==1&&attempts==2&&queue.pending()==0,"deferred work must retry exactly once after error recovery");

    unsigned afterDestruction{};
    auto third=gate.beginFirmwareCapture({},1s);
    {mc::DeferredTelemetryQueue closing(gate);closing.submit(Item::TouchpadKeyPoll,[&]{++afterDestruction;});}
    third.reset();check(afterDestruction==0,"destroying pending queue must not call an actor callback");
    std::cout<<"PASS deferred OSD queue: fake timer, audio, WM_SETTINGCHANGE, hotkey, retry and destruction\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL deferred telemetry: "<<error.what()<<'\n';return 1;}}
