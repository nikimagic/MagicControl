// Pure WMI timestamp classification. No WMI subscription, hardware or files.
#include "MagicControl/FirmwareEventTime.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace std::chrono_literals;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
int main(){try{
    using namespace mc;
    check(parseWmiUint64(L"0")==0&&parseWmiUint64(L"000123")==123,"decimal CIM uint64 parsing failed");
    check(parseWmiUint64(L"18446744073709551615")==std::numeric_limits<std::uint64_t>::max(),"uint64 maximum rejected");
    for(const std::wstring_view invalid:{L"",L" ",L" 1",L"1 ",L"+1",L"-1",L"12a",L"18446744073709551616",L"000000000000000000001"})
        check(!parseWmiUint64(invalid),"invalid/overflow CIM uint64 must be unavailable");
    check(firmwareEventFiletime(FirmwareWallClock::time_point{})==116444736000000000ULL,"Unix epoch to FILETIME offset incorrect");

    const FirmwareWallClock::time_point wallStart{std::chrono::seconds(1700000000)};
    const FirmwareSteadyClock::time_point steadyStart{std::chrono::seconds(5000)};
    const auto wallEnd=wallStart+10s;const auto steadyEnd=steadyStart+10s;
    const auto boundary=makeFirmwareEventBoundary(wallStart,steadyStart,wallEnd,steadyEnd);
    check(boundary&&firmwareEventFiletime(wallEnd)==boundary->filetime100ns,"stable clocks must produce an exact UTC boundary");
    check(makeFirmwareEventBoundary(wallStart,steadyStart,wallEnd+250ms,steadyEnd).has_value(),"+250 ms boundary should be accepted");
    check(makeFirmwareEventBoundary(wallStart,steadyStart,wallEnd-250ms,steadyEnd).has_value(),"-250 ms boundary should be accepted");
    check(!makeFirmwareEventBoundary(wallStart,steadyStart,wallEnd+251ms,steadyEnd),"forward clock jump must be unavailable");
    check(!makeFirmwareEventBoundary(wallStart,steadyStart,wallEnd-251ms,steadyEnd),"backward clock jump must be unavailable");
    check(!makeFirmwareEventBoundary(wallStart,steadyStart,wallEnd,steadyStart-1ms),"reverse steady clock must be unavailable");

    const auto nowWall=wallEnd+2s;const auto nowSteady=steadyEnd+2s;
    const auto now=firmwareEventFiletime(nowWall);check(bool(now),"fixture current FILETIME unavailable");
    check(classifyFirmwareEvent({},boundary,nowWall,nowSteady)==FirmwareEventAge::Unknown,"missing event timestamp must not be replayed");
    check(classifyFirmwareEvent(0,boundary,nowWall,nowSteady)==FirmwareEventAge::Unknown,"zero event timestamp must not be replayed");
    check(classifyFirmwareEvent(boundary->filetime100ns-1,boundary,nowWall,nowSteady)==FirmwareEventAge::Stale,"old event must be stale");
    check(classifyFirmwareEvent(boundary->filetime100ns,boundary,nowWall,nowSteady)==FirmwareEventAge::Stale,"equal boundary must be stale");
    check(classifyFirmwareEvent(boundary->filetime100ns+1,boundary,nowWall,nowSteady)==FirmwareEventAge::Fresh,"valid event after boundary must be fresh");
    check(classifyFirmwareEvent(*now+1,boundary,nowWall,nowSteady)==FirmwareEventAge::Unknown,"even a slightly future event timestamp must be unknown");
    check(classifyFirmwareEvent(*now+5000000,boundary,nowWall,nowSteady)==FirmwareEventAge::Unknown,"future timestamp from a temporary clock jump must not replay");
    check(classifyFirmwareEvent(*now,{},nowWall,nowSteady)==FirmwareEventAge::Unknown,"missing boundary must not be guessed");
    check(classifyFirmwareEvent(boundary->filetime100ns+1,boundary,nowWall+251ms,nowSteady)==FirmwareEventAge::Unknown,"forward clock change after capture must be unknown");
    check(classifyFirmwareEvent(boundary->filetime100ns+1,boundary,nowWall-251ms,nowSteady)==FirmwareEventAge::Unknown,"backward clock change after capture must be unknown");
    check(classifyFirmwareEvent(boundary->filetime100ns+1,boundary,nowWall,steadyEnd-1ms)==FirmwareEventAge::Unknown,"reverse steady clock after capture must be unknown");
    for(const bool mayControl:{false,true}){
        check(firmwareEventDisposition(FirmwareEventAge::Fresh,mayControl)==FirmwareEventDisposition::Deliver,"fresh event must be delivered");
        check(firmwareEventDisposition(FirmwareEventAge::Stale,mayControl)==FirmwareEventDisposition::DiscardStale,"stale event must be discarded");
    }
    check(firmwareEventDisposition(FirmwareEventAge::Unknown,true)==FirmwareEventDisposition::ResubscribeUnknown,"unknown controlling event must not execute");
    check(firmwareEventDisposition(FirmwareEventAge::Unknown,false)==FirmwareEventDisposition::Deliver,"unknown passive notification should remain deliverable");

    // OemEventMonitor passes mayControl=true for every old-queue item: several
    // seemingly passive codes update settings/UI or schedule later actions.
    FirmwareEventQueueFilter filter;
    check(!filter.active()&&filter.decide(FirmwareEventAge::Unknown,true)==FirmwareEventDisposition::Deliver,"ordinary pre-capture WMI event should be delivered");
    filter.captureBoundary();
    check(filter.active()&&filter.renewAfterEmpty(),"empty queue without a trusted event time must trigger proactive resubscription");
    filter.renewed();
    check(!filter.active()&&filter.decide(FirmwareEventAge::Unknown,true)==FirmwareEventDisposition::Deliver,"fresh subscription must accept a new event without sacrificing the first hotkey");
    filter.captureBoundary();
    check(filter.decide(FirmwareEventAge::Stale,true)==FirmwareEventDisposition::DiscardStale&&!filter.renewAfterEmpty(),"valid old timestamp must be discarded and keep the filter active");
    check(filter.decide(FirmwareEventAge::Fresh,true)==FirmwareEventDisposition::Deliver,"confirmed fresh queued event must be delivered");
    check(filter.decide(FirmwareEventAge::Unknown,true)==FirmwareEventDisposition::ResubscribeUnknown,"unknown old-queue item must not execute");
    filter.renewed();check(!filter.active(),"renewed subscription must leave filtering mode");
    std::cout<<"PASS WMI event time: BSTR uint64, FILETIME, boundaries, clock drift and unknown/future rejection\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL WMI event time: "<<error.what()<<'\n';return 1;}}
