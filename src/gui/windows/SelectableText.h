#pragma once
#include <windows.h>
#include <richedit.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include "../common/Theme.h"

namespace mc::gui {
// Read-only rich text: native selection/clipboard, application font and palette.
inline HWND createSelectableText(HWND parent){
    static const auto module=LoadLibraryW(L"Msftedit.dll");
    if(!module)throw std::runtime_error("Msftedit.dll unavailable");
    const auto control=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_NOHIDESEL,
        0,0,0,0,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!control)throw std::runtime_error("Cannot create selectable text");
    SendMessageW(control,EM_SETBKGNDCOLOR,0,theme::kSurface);
    SendMessageW(control,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
    return control;
}
inline void setSelectableText(HWND control,std::wstring_view text,LONG activeStart=0,LONG activeLength=0){
    SetWindowTextW(control,std::wstring(text).c_str());
    CHARRANGE range{0,-1};SendMessageW(control,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;format.crTextColor=theme::kTextPrimary;
    SendMessageW(control,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    if(activeLength){range={activeStart,activeStart+activeLength};SendMessageW(control,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
        format.crTextColor=theme::kOk;SendMessageW(control,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));}
    range={0,0};SendMessageW(control,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
    SendMessageW(control,EM_SETMODIFY,FALSE,0);
}
}
