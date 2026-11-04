#include "MagicControl/BatteryIdentityHistory.h"
#include "MagicControl/Logging.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/Version.h"
#include <chrono>
#include <charconv>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace mc {
std::optional<unsigned> batteryAgeMonths(std::string_view text,std::chrono::year_month_day today) noexcept {
    if(text.size()!=10||text[4]!='-'||text[7]!='-'||!today.ok())return {};
    const auto number=[&](std::size_t start,std::size_t length)->std::optional<unsigned>{
        unsigned value{};const auto* first=text.data()+start;const auto* end=first+length;
        const auto parsed=std::from_chars(first,end,value);
        return parsed.ec==std::errc{}&&parsed.ptr==end?std::optional<unsigned>{value}:std::nullopt;
    };
    const auto year=number(0,4),month=number(5,2),day=number(8,2);
    if(!year||!month||!day)return {};
    const std::chrono::year_month_day manufactured{std::chrono::year{static_cast<int>(*year)},
        std::chrono::month{*month},std::chrono::day{*day}};
    if(!manufactured.ok()||manufactured>today)return {};
    auto months=(int(today.year())-int(manufactured.year()))*12+
        int(unsigned(today.month()))-int(unsigned(manufactured.month()));
    if(unsigned(today.day())<unsigned(manufactured.day()))--months;
    return months<0?std::nullopt:std::optional<unsigned>{static_cast<unsigned>(months)};
}
std::optional<unsigned> batteryAgeMonthsLocal(std::string_view date) noexcept {
    const auto now=std::time(nullptr);std::tm local{};
#ifdef _WIN32
    if(localtime_s(&local,&now)!=0)return {};
#else
    if(!localtime_r(&now,&local))return {};
#endif
    return batteryAgeMonths(date,{std::chrono::year{local.tm_year+1900},
        std::chrono::month{static_cast<unsigned>(local.tm_mon+1)},std::chrono::day{static_cast<unsigned>(local.tm_mday)}});
}
namespace {
struct History {
    std::mutex mutex;
    std::filesystem::path path;
    std::string started,date,source;
    std::optional<std::uint64_t> thermalHistory;
    std::chrono::steady_clock::time_point startedAt;
    bool active{},written{},preferEc{};
    void append() noexcept { try {
        if(!active||written)return;
        const auto age=batteryAgeMonthsLocal(date);
        const auto line=started+" | date="+(date.empty()?"unavailable":date)+
            " | age_months="+(age?std::to_string(*age):"unavailable")+
            " | thermal_hours="+(thermalHistory?std::to_string(*thermalHistory):"unavailable")+
            " | source="+(source.empty()?"unavailable":source)+" | version=" MAGICCONTROL_VERSION_STRING "\n";
        std::string error;
        if(detail::appendHistoryRow(path,line,error))written=true;
    }catch(...){} }
    ~History(){append();}
};
History& history(){static History value;return value;}
}
void beginBatteryIdentityHistory(bool preferEc) noexcept {try{
    auto& h=history();std::lock_guard lock(h.mutex);if(h.active)return;
    const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&now);
#else
    localtime_r(&now,&local);
#endif
    std::ostringstream text;text<<std::put_time(&local,"%Y-%m-%d %H:%M:%S %z");
    h.started=text.str();h.startedAt=std::chrono::steady_clock::now();
    h.path=defaultLogDirectory()/"battery_id.txt";h.preferEc=preferEc;h.active=true;
}catch(...) {}}
bool batteryIdentityHistoryNeedsEcSample() noexcept {try{
    auto& h=history();std::lock_guard lock(h.mutex);
    return h.active&&!h.written&&h.preferEc;
}catch(...){return false;}}
void observeBatteryIdentity(std::string_view date,std::string_view source,bool ec,std::optional<std::uint64_t> thermalHistory) noexcept {try{
    auto& h=history();std::lock_guard lock(h.mutex);if(!h.active||h.written)return;
    if(!date.empty()&&(ec||h.date.empty())){h.date=std::string(date);h.source=std::string(source);}
    if(ec&&thermalHistory)h.thermalHistory=thermalHistory;
    if((ec&&!date.empty()&&h.thermalHistory)||!h.preferEc||std::chrono::steady_clock::now()-h.startedAt>=std::chrono::seconds(30)){
        h.append();
        if(h.written){const auto age=batteryAgeMonthsLocal(h.date);
            logMessage("battery","Battery identity: date="+(h.date.empty()?std::string("unavailable"):h.date)+
                " age_months="+(age?std::to_string(*age):"unavailable")+
                " thermal_hours="+(h.thermalHistory?std::to_string(*h.thermalHistory):"unavailable")+
                " source="+(h.source.empty()?"unavailable":h.source));}
    }
}catch(...) {}}
}
