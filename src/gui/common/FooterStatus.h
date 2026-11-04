#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace mc::gui::ui {
// One UI message channel for native pages. This never reads hardware or converts
// requested/saved states into claims of successful physical execution.
class FooterStatuses {
    std::mutex mutex_;
    std::map<std::string,std::string,std::less<>> messages_;
    std::string latest_;
    std::function<void()> changed_;
public:
    void observe(std::function<void()> changed){std::lock_guard lock{mutex_};changed_=std::move(changed);}
    void publish(std::string_view source,std::string message){
        if(message.empty())return;
        std::function<void()> notify;
        {std::lock_guard lock{mutex_};auto& previous=messages_[std::string(source)];
            if(previous==message)return;
            previous=message;latest_=std::move(message);notify=changed_;}
        if(notify)notify();
    }
    std::string text(std::string_view preferred={}){
        std::lock_guard lock{mutex_};
        if(const auto found=messages_.find(preferred);found!=messages_.end())return found->second;
        return latest_;
    }
    void clear(){std::function<void()> notify;
        {std::lock_guard lock{mutex_};messages_.clear();latest_.clear();notify=changed_;}
        if(notify)notify();
    }
};
inline FooterStatuses& footerStatuses(){static FooterStatuses messages;return messages;}
inline std::string_view footerSource(int tab){
    return tab==0?"fans":tab==1?"sets":tab==2?"keys":std::string_view{};
}
}
