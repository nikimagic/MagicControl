#pragma once
#include <cstdint>
#include <exception>
#include <string_view>

namespace mc::gui::ui {
enum class DpiTraceStage { Begin, Positioned, FontsReady, IconsReady, PageDone, Complete, Unwound };
enum class DpiTracePhase { Position, Relayout };

struct DpiTraceRecord {
    std::uint64_t id{},parent{};
    int previousDpi{},requestedDpi{};
    DpiTraceStage stage{};
    DpiTracePhase phase{};
    std::string_view page;
    bool showRequested{};
};

// A synchronous, UI-thread-only trace scope: no workers, hardware access or
// retained page pointers. Nested native DPI notifications restore their parent.
class DpiChangeTrace {
public:
    using Sink = void(*)(void*,const DpiTraceRecord&) noexcept;
    DpiChangeTrace(Sink sink,void* context,int previousDpi,int requestedDpi) noexcept
        :previous_(current_),sink_(sink),context_(context),id_(++nextId_),
         parent_(previous_?previous_->id_:0),previousDpi_(previousDpi),requestedDpi_(requestedDpi),
         exceptions_(std::uncaught_exceptions()) {
        current_=this;emit(DpiTraceStage::Begin);
    }
    ~DpiChangeTrace() noexcept {
        emit(std::uncaught_exceptions()>exceptions_?DpiTraceStage::Unwound:DpiTraceStage::Complete);
        current_=previous_;
    }
    DpiChangeTrace(const DpiChangeTrace&)=delete;
    DpiChangeTrace& operator=(const DpiChangeTrace&)=delete;

    void positioned() noexcept {emit(DpiTraceStage::Positioned);phase_=DpiTracePhase::Relayout;}
    static void mark(DpiTraceStage stage) noexcept {if(current_)current_->emit(stage);}
    static void pageDone(std::string_view page,bool showRequested) noexcept {
        if(current_)current_->emit(DpiTraceStage::PageDone,page,showRequested);
    }
private:
    void emit(DpiTraceStage stage,std::string_view page={},bool showRequested=false) noexcept {
        if(sink_)sink_(context_,{id_,parent_,previousDpi_,requestedDpi_,stage,phase_,page,showRequested});
    }
    static inline thread_local DpiChangeTrace* current_{};
    static inline thread_local std::uint64_t nextId_{};
    DpiChangeTrace* previous_{};
    Sink sink_{};void* context_{};
    std::uint64_t id_{},parent_{};
    int previousDpi_{},requestedDpi_{},exceptions_{};
    DpiTracePhase phase_{DpiTracePhase::Position};
};

inline constexpr std::string_view dpiTraceStageName(DpiTraceStage stage) noexcept {
    switch(stage){
    case DpiTraceStage::Begin:return "begin";
    case DpiTraceStage::Positioned:return "positioned";
    case DpiTraceStage::FontsReady:return "fonts_ready";
    case DpiTraceStage::IconsReady:return "icons_ready";
    case DpiTraceStage::PageDone:return "page_done";
    case DpiTraceStage::Complete:return "complete";
    case DpiTraceStage::Unwound:return "unwound";
    }
    return "unknown";
}
}
