#pragma once
#include <optional>
#include <stdexcept>
#include <string>

namespace mc::huawei {
struct KeyboardTimeoutResult {int before{},after{};bool wrote{};};
template<class Read,class Pause,class Log>
int stableKeyboardTimeout(Read& read,Pause& pause,Log& log,std::optional<int> expected={}){
    std::optional<int> previous;std::string last;
    for(int attempt=0;attempt<6;++attempt){
        try{
            const int seconds=read();
            if(seconds<0||seconds>3600)throw std::runtime_error("invalid keyboard timeout="+std::to_string(seconds));
            last="seconds="+std::to_string(seconds);
            if(previous==seconds&&(!expected||seconds==*expected))return seconds;
            previous=seconds;
        }catch(const std::exception& error){previous.reset();last=error.what();log("keyboard timeout read: "+last);}
        if(attempt!=5)pause();
    }
    throw std::runtime_error("stable keyboard timeout not confirmed; "+last);
}
// One policy for HWMI and existing Linux sysfs: explicit native seconds only,
// two matching reads, and rollback exclusively while our value remains owned.
// This never changes brightness, activity counters or the user's other fields.
template<class Read,class Write,class Pause,class Log>
KeyboardTimeoutResult applyKeyboardTimeout(int seconds,Read read,Write write,Pause pause,Log log){
    if(seconds<0||seconds>3600)throw std::invalid_argument("Huawei: keyboard timeout0..3600 seconds");
    const int before=stableKeyboardTimeout(read,pause,log);if(before==seconds)return {before,before,false};
    bool attempted=false;
    try{
        if(read()!=before)throw std::runtime_error("keyboard timeout changed before explicit write");
        attempted=true;write(seconds);stableKeyboardTimeout(read,pause,log,seconds);
        return {before,seconds,true};
    }catch(const std::exception& failure){
        const std::string reason=failure.what();std::string rollback="keyboard timeout write not attempted";
        if(attempted)try{
            const int current=stableKeyboardTimeout(read,pause,log);
            if(current!=before){
                if(current!=seconds)throw std::runtime_error("timeout ownership lost; current="+std::to_string(current));
                write(before);
            }
            stableKeyboardTimeout(read,pause,log,before);rollback="previous keyboard timeout read back; other keyboard fields unchanged";
        }catch(const std::exception& error){rollback=std::string("keyboard timeout rollback NOT confirmed; actual state unknown: ")+error.what();}
        log(reason+"; "+rollback);throw std::runtime_error(reason+"; "+rollback);
    }
}
}
