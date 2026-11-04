#pragma once
#include <windows.h>
#include <algorithm>
#include <string>

namespace mc::windows::encoding {
inline std::string utf8(const std::wstring& value) {
    const int length=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(static_cast<std::size_t>(std::max(0,length)),'\0');
    if(length)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr);
    return result;
}
inline std::wstring wide(const std::string& value) {
    const int length=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(std::max(0,length)),L'\0');
    if(length)MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),length);
    return result;
}
}
