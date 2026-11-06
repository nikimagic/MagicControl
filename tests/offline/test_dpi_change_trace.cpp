#include "src/gui/common/DpiChangeTrace.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace mc::gui::ui;
struct Capture {std::vector<DpiTraceRecord> records;bool failed{};};
static void collect(void* p,const DpiTraceRecord& row) noexcept {
    auto& capture=*static_cast<Capture*>(p);
    try{capture.records.push_back(row);}catch(...){capture.failed=true;}
}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    Capture capture;
    {
        DpiChangeTrace outer(collect,&capture,168,192);
        DpiChangeTrace::pageDone("INFO",true); // Reentrant WM_SIZE during SetWindowPos.
        outer.positioned();
        DpiChangeTrace::mark(DpiTraceStage::FontsReady);
        DpiChangeTrace::mark(DpiTraceStage::IconsReady);
        DpiChangeTrace::pageDone("FANs",false);
        {
            DpiChangeTrace inner(collect,&capture,192,168);
            DpiChangeTrace::pageDone("NVIDIA",false);
        }
        DpiChangeTrace::pageDone("INFO",true);
        { DpiChangeTrace disabled(nullptr,nullptr,192,192);DpiChangeTrace::pageDone("hidden",true); }
        DpiChangeTrace::pageDone("NVIDIA",false);
    }
    const auto count=capture.records.size();
    DpiChangeTrace::pageDone("outside",true);
    check(!capture.failed&&count==capture.records.size()&&count==12,"scope leakage or disabled tracing");
    const auto& r=capture.records;
    check(r[0].stage==DpiTraceStage::Begin&&r[0].previousDpi==168&&r[0].requestedDpi==192,"outer begin");
    check(r[1].stage==DpiTraceStage::PageDone&&r[1].phase==DpiTracePhase::Position&&r[1].page=="INFO","nested position layout");
    check(r[2].stage==DpiTraceStage::Positioned&&r[3].stage==DpiTraceStage::FontsReady&&r[4].stage==DpiTraceStage::IconsReady,"position and font ordering");
    check(r[5].stage==DpiTraceStage::PageDone&&r[5].phase==DpiTracePhase::Relayout&&r[5].page=="FANs","reflow page");
    check(r[6].stage==DpiTraceStage::Begin&&r[6].parent==r[0].id&&r[7].id==r[6].id&&r[8].stage==DpiTraceStage::Complete,"nested notification");
    check(r[9].page=="INFO"&&r[9].id==r[0].id&&r[10].page=="NVIDIA"&&r[10].id==r[0].id,"outer scope restored");
    // Scope exit contributes the final event separately from page events.
    check(r.back().stage==DpiTraceStage::Complete&&r.back().id==r[0].id,"outer completion");
    try{DpiChangeTrace failed(collect,&capture,168,192);throw std::runtime_error("synthetic");}
    catch(const std::runtime_error&){}
    check(capture.records.back().stage==DpiTraceStage::Unwound,"C++ exception is not logged as successful completion");
    std::puts("DPI trace: nested/disabled/position/reflow passed; no GUI or hardware");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"DPI trace FAIL: %s\n",error.what());return 1;}}
