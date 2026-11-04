#pragma once
#include "Localization.h"
#include <charconv>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>

namespace mc::gui::ui {
// UI text only. Settings, diagnostic JSON/CSV and wire protocols retain their
// own invariant numeric formats; changing the selected language never rewrites them.
inline std::string displayNumber(double value, int precision = 0) {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(precision) << value;
    auto result = text.str();
    if (languageIndex() == 0)
        for (auto& ch : result) if (ch == '.') ch = ',';
    return result;
}
// Preserve the former std::format("{}", value) shortest representation for
// readouts that never specified a fractional precision. Localize only the mark.
inline std::string displayCompactNumber(double value) {
    char buffer[64];
    const auto converted=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general);
    std::string result;
    if(converted.ec==std::errc{})result.assign(buffer,converted.ptr);
    else {
        std::ostringstream text;text.imbue(std::locale::classic());
        text<<std::setprecision(std::numeric_limits<double>::max_digits10)<<value;result=text.str();
    }
    if(languageIndex()==0)for(auto& ch:result)if(ch=='.')ch=',';
    return result;
}
}  // namespace mc::gui::ui
