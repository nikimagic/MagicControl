#pragma once
#include "../common/Localization.h"
#include <windows.h>
#include <commctrl.h>
#include <new>
namespace mc::gui {
inline std::wstring localized(std::wstring_view source){
    const int bytes=WideCharToMultiByte(CP_UTF8,0,source.data(),static_cast<int>(source.size()),nullptr,0,nullptr,nullptr);
    std::string text(bytes,0);if(bytes)WideCharToMultiByte(CP_UTF8,0,source.data(),static_cast<int>(source.size()),text.data(),bytes,nullptr,nullptr);
    const auto translated=ui::translate(text);const int n=MultiByteToWideChar(CP_UTF8,0,translated.data(),static_cast<int>(translated.size()),nullptr,0);
    std::wstring result(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,translated.data(),static_cast<int>(translated.size()),result.data(),n);return result;
}
inline int localizedDrawText(HDC dc,const wchar_t* source,int count,LPRECT rect,UINT flags){
    if(!source)return DrawTextW(dc,source,count,rect,flags);
    const auto text=localized(count<0?std::wstring_view(source):std::wstring_view(source,static_cast<std::size_t>(count)));
    // DT_MODIFYSTRING requires spare mutable bytes; all ordinary paint/measure
    // calls use the same translated text without changing geometry/renderer.
    auto buffer=text;buffer.resize(text.size()+4,0);
    return DrawTextW(dc,buffer.data(),static_cast<int>(text.size()),rect,flags);
}
inline BOOL localizedTextExtent(HDC dc,const wchar_t* source,int count,LPSIZE size){
    if(!source)return GetTextExtentPoint32W(dc,source,count,size);
    const auto value=localized(count<0?std::wstring_view(source):std::wstring_view(source,static_cast<std::size_t>(count)));
    return GetTextExtentPoint32W(dc,value.c_str(),static_cast<int>(value.size()),size);
}
namespace localization {
struct Caption {std::wstring original;};
inline LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    auto* caption=reinterpret_cast<Caption*>(data);
    if(message==WM_SETTEXT){caption->original=lp?reinterpret_cast<const wchar_t*>(lp):L"";const auto value=localized(caption->original);return DefSubclassProc(h,message,wp,reinterpret_cast<LPARAM>(value.c_str()));}
    if(message==WM_NCDESTROY){RemoveWindowSubclass(h,procedure,id);delete caption;}
    return DefSubclassProc(h,message,wp,lp);
}
inline BOOL CALLBACK apply(HWND h,LPARAM){
    wchar_t klass[32]{};GetClassNameW(h,klass,32);
    if(_wcsicmp(klass,L"STATIC")&&_wcsicmp(klass,L"BUTTON"))return TRUE;
    constexpr UINT_PTR id=0x4D434C47;DWORD_PTR data{};
    if(!GetWindowSubclass(h,procedure,id,&data)){
        const int n=GetWindowTextLengthW(h);std::wstring text(n+1,0);GetWindowTextW(h,text.data(),n+1);text.resize(n);
        auto* caption=new(std::nothrow) Caption{std::move(text)};if(!caption)return TRUE;
        if(!SetWindowSubclass(h,procedure,id,reinterpret_cast<DWORD_PTR>(caption))){delete caption;return TRUE;}data=reinterpret_cast<DWORD_PTR>(caption);
    }
    auto* caption=reinterpret_cast<Caption*>(data);const auto original=caption->original;
    const auto translated=localized(original);const int n=GetWindowTextLengthW(h);std::wstring current(n+1,0);GetWindowTextW(h,current.data(),n+1);current.resize(n);
    if(current!=translated)SetWindowTextW(h,original.c_str());return TRUE;
}
inline std::wstring sourceCaption(HWND h){
    DWORD_PTR data{};
    if(GetWindowSubclass(h,procedure,0x4D434C47,&data))
        return reinterpret_cast<Caption*>(data)->original;
    const int n=GetWindowTextLengthW(h);std::wstring text(n+1,0);
    GetWindowTextW(h,text.data(),n+1);text.resize(n);return text;
}
}
inline void applyWindowLanguage(HWND root){EnumChildWindows(root,localization::apply,0);}
}
