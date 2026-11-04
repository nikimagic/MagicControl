#pragma once
#include <windows.h>
#include <winver.h>
#include <array>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>
namespace mc::windows {
struct FileVersionResource {
    std::array<unsigned,4> numbers{};
    DWORD fileType{};
    std::wstring productName;
    std::string text() const {return std::format("{}.{}.{}.{}",numbers[0],numbers[1],numbers[2],numbers[3]);}
};
// Static VERSIONINFO only. Never loads/executes the executable or driver.
inline FileVersionResource readFileVersionResource(const std::wstring& path){
    DWORD unused{};const auto size=GetFileVersionInfoSizeW(path.c_str(),&unused);
    if(!size||size>1024*1024)throw std::runtime_error("GetFileVersionInfoSize Win32="+std::to_string(GetLastError()));
    std::vector<unsigned char> data(size);
    if(!GetFileVersionInfoW(path.c_str(),0,size,data.data()))throw std::runtime_error("GetFileVersionInfo Win32="+std::to_string(GetLastError()));
    VS_FIXEDFILEINFO* info{};UINT length{};
    if(!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&length)||length<sizeof(*info)||info->dwSignature!=0xFEEF04BD)
        throw std::runtime_error("Invalid VERSIONINFO");
    FileVersionResource result;result.numbers={HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),HIWORD(info->dwFileVersionLS),LOWORD(info->dwFileVersionLS)};
    result.fileType=info->dwFileType;
    struct Translation{WORD language,codepage;};Translation* translations{};UINT bytes{};
    if(VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&translations),&bytes))
        for(unsigned i=0;i<bytes/sizeof(Translation);++i){
            const auto key=std::format(L"\\StringFileInfo\\{:04x}{:04x}\\ProductName",translations[i].language,translations[i].codepage);
            wchar_t* product{};UINT chars{};
            if(VerQueryValueW(data.data(),key.c_str(),reinterpret_cast<void**>(&product),&chars)&&chars&&product){
                result.productName.assign(product,chars-1);if(!result.productName.empty())break;
            }
        }
    return result;
}
}
