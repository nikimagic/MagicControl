#include "MagicControl/ActionLog.h"
#include "MagicControl/MicrophoneControl.h"
#include <pulse/pulseaudio.h>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <memory>

namespace mc {
namespace {
struct PulseCapture {
    pa_mainloop* loop{};pa_context* context{};
    const std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    PulseCapture(){
        loop=pa_mainloop_new();if(!loop)throw std::runtime_error("PulseAudio: mainloop unavailable");
        context=pa_context_new(pa_mainloop_get_api(loop),"MagicControl microphone");
        try{if(!context||pa_context_connect(context,nullptr,PA_CONTEXT_NOAUTOSPAWN,nullptr)<0)fail();
            while(pa_context_get_state(context)!=PA_CONTEXT_READY)pump();}
        catch(...){cleanup();throw;}
    }
    ~PulseCapture(){cleanup();}
    void cleanup(){if(context){pa_context_disconnect(context);pa_context_unref(context);context=nullptr;}if(loop){pa_mainloop_free(loop);loop=nullptr;}}
    [[noreturn]] void fail(){throw std::runtime_error(std::string("PulseAudio/PipeWire: ")+(context?pa_strerror(pa_context_errno(context)):"context unavailable"));}
    void pump(){
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Истекло время ожидания микрофона.");
        int result{};if(pa_mainloop_iterate(loop,0,&result)<0)fail();
        const auto state=pa_context_get_state(context);
        if(state==PA_CONTEXT_FAILED||state==PA_CONTEXT_TERMINATED)fail();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    void finish(pa_operation* raw){
        if(!raw)fail();
        std::unique_ptr<pa_operation,decltype(&pa_operation_unref)> operation(raw,pa_operation_unref);
        try{while(pa_operation_get_state(raw)==PA_OPERATION_RUNNING)pump();if(pa_operation_get_state(raw)!=PA_OPERATION_DONE)fail();}
        catch(...){pa_operation_cancel(raw);throw;}
    }
    std::string defaultName(){
        std::string name;
        finish(pa_context_get_server_info(context,[](pa_context*,const pa_server_info* info,void* data){
            if(info&&info->default_source_name)*static_cast<std::string*>(data)=info->default_source_name;},&name));
        if(name.empty())throw std::runtime_error("Микрофон по умолчанию не найден.");return name;
    }
    MicrophoneState read(const std::string& name){
        struct Info{bool found{},monitor{};MicrophoneState state;} info;
        finish(pa_context_get_source_info_by_name(context,name.c_str(),[](pa_context*,const pa_source_info* value,int end,void* data){
            if(end||!value)return;auto& info=*static_cast<Info*>(data);info.found=true;info.monitor=value->monitor_of_sink!=PA_INVALID_INDEX;
            info.state={value->mute==0,true,"Микрофон по умолчанию · PulseAudio/PipeWire"};
            if(value->description)info.state.detail+=" · "+std::string(value->description);},&info));
        if(!info.found)throw std::runtime_error("Микрофон по умолчанию не найден.");
        if(info.monitor)throw std::runtime_error("Источник по умолчанию является монитором вывода, а не микрофоном.");
        return info.state;
    }
};
}
MicrophoneState readDefaultMicrophone(){
    try{PulseCapture capture;const auto name=capture.defaultName();auto result=capture.read(name);
        if(capture.defaultName()!=name)throw std::runtime_error("Микрофон по умолчанию изменился. Повторите действие.");return result;}
    catch(const std::exception& error){return {{},false,std::string("Микрофон недоступен: ")+error.what()};}
}
void setDefaultMicrophoneEnabled(bool enabled){
    audit::Scope trace("system.microphone","\"requested_enabled\":"+std::string(enabled?"true":"false"));
    PulseCapture capture;const auto name=capture.defaultName();const auto before=capture.read(name);
    if(capture.defaultName()!=name)throw std::runtime_error("Микрофон по умолчанию изменился. Повторите действие.");
    if(before.enabled!=enabled){bool success{};
        capture.finish(pa_context_set_source_mute_by_name(capture.context,name.c_str(),enabled?0:1,
            [](pa_context*,int done,void* data){*static_cast<bool*>(data)=done!=0;},&success));
        if(!success)throw std::runtime_error("ОС не подтвердила состояние микрофона.");}
    if(capture.defaultName()!=name)throw std::runtime_error("Микрофон по умолчанию изменился. Повторите действие.");
    if(capture.read(name).enabled!=enabled)throw std::runtime_error("ОС не подтвердила состояние микрофона.");
    if(capture.defaultName()!=name)throw std::runtime_error("Микрофон по умолчанию изменился. Повторите действие.");
    trace.finish("readback_confirmed","\"enabled\":"+std::string(enabled?"true":"false"));
}
}
