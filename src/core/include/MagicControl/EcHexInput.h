#pragma once
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mc {
// Parsing only: no address is admitted for a read or write by this helper.
inline std::optional<std::uint16_t> parseEcHex(std::string_view text,unsigned maximum){
    const auto first=text.find_first_not_of(" \t\r\n");if(first==text.npos)return {};
    text=text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
    if(text.starts_with("0x")||text.starts_with("0X"))text.remove_prefix(2);
    unsigned value{};const auto result=std::from_chars(text.data(),text.data()+text.size(),value,16);
    if(text.empty()||result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||value>maximum)return {};
    return static_cast<std::uint16_t>(value);
}
}
