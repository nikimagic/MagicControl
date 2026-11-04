#include "LocalizationWindows.h"
#include "UpdateWindow.h"
#include "MessageDialog.h"
#include "IconResource.h"
#include "../../platform/windows/TextWindows.h"
#include "../common/Theme.h"
#include "Layout.h"
#include "MagicControl/AppUpdate.h"
#include "MagicControl/Version.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include "../common/UpdatePresentation.h"

namespace mc::gui {
namespace {
struct View {
    HWND window{},status{},checkButton{},updateButton{},closeButton{},link{};
    UINT dpi{96};HFONT font{};HBRUSH background{CreateSolidBrush(theme::kSurface)};
    update::Operations operations;std::string shown;
    bool busy{},ready{},closing{};
    int s(int n)const{return MulDiv(n,static_cast<int>(dpi),96);}
    ~View(){operations.shutdown();if(font)DeleteObject(font);DeleteObject(background);}
    void fonts(){
        const auto next=createUiFont(ui::FontRole::Body,dpi);if(!next)return;
        const auto previous=font;font=next;
        for(auto child:{status,checkButton,updateButton,closeButton,link})
            if(child)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        if(previous)DeleteObject(previous);
    }
    void start(bool install){
        if(busy||!operations.start(install))return;
        busy=true;EnableWindow(checkButton,FALSE);EnableWindow(updateButton,FALSE);EnableWindow(closeButton,TRUE);
    }
    void layout(){RECT r{};GetClientRect(window,&r);const int w=r.right;
        MoveWindow(status,s(16),s(52),std::max(0,w-s(32)),s(110),TRUE);
        MoveWindow(link,s(16),s(175),s(110),s(30),TRUE);
        MoveWindow(checkButton,std::max(s(140),w-s(354)),s(175),s(132),s(30),TRUE);
        MoveWindow(updateButton,w-s(214),s(175),s(96),s(30),TRUE);
        MoveWindow(closeButton,w-s(110),s(175),s(94),s(30),TRUE);
    }
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp){
        auto* p=reinterpret_cast<View*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);p->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(!p)return DefWindowProcW(w,m,wp,lp);
        switch(m){
            case WM_CREATE:{
                p->dpi=GetDpiForWindow(w);p->fonts();
                auto button=[&](const wchar_t* title,int id){auto h=CreateWindowW(L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,w,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(p->font),TRUE);return h;};
                p->status=CreateWindowW(L"STATIC",L"Нажмите «Проверить».",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,w,nullptr,nullptr,nullptr);SendMessageW(p->status,WM_SETFONT,reinterpret_cast<WPARAM>(p->font),TRUE);
                const auto previous=update::lastResult();if(!previous.empty())SetWindowTextW(p->status,mc::windows::encoding::wide(previous).c_str());
                p->link=button(L"GitHub",4);p->checkButton=button(L"Проверить",1);p->updateButton=button(L"Обновить",2);p->closeButton=button(L"Закрыть",3);
                applyWindowLanguage(w);
                EnableWindow(p->updateButton,FALSE);SetTimer(w,1,80,nullptr);p->layout();return 0;}
            case WM_SIZE:p->layout();return 0;
            case WM_DPICHANGED:{
                p->dpi=HIWORD(wp);p->fonts();const auto bounds=reinterpret_cast<RECT*>(lp);
                SetWindowPos(w,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);
                p->layout();RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);return 0;}
            case WM_COMMAND:
                if(LOWORD(wp)==1)p->start(false);
                else if(LOWORD(wp)==2)p->start(true);
                else if(LOWORD(wp)==3)SendMessageW(w,WM_CLOSE,0,0);
                else if(LOWORD(wp)==4)ShellExecuteW(w,L"open",L"https://github.com/nikimagic/MagicControl/releases",nullptr,nullptr,SW_SHOWNORMAL);
                return 0;
            case WM_TIMER:{
                const auto result=p->operations.snapshot();const auto text=ui::updateMessage(result);const bool finished=result.done;p->ready=result.ready;
                if(!text.empty()&&text!=p->shown){p->shown=text;SetWindowTextW(p->status,mc::windows::encoding::wide(text).c_str());}
                if(p->busy&&finished){p->busy=false;EnableWindow(p->checkButton,TRUE);EnableWindow(p->updateButton,result.release.available);}
                if(p->ready||p->closing&&finished)closeModalDialog(w);return 0;}
            case WM_CLOSE:if(p->busy){p->closing=true;p->operations.cancel();SetWindowTextW(p->status,L"Отмена…");}else closeModalDialog(w);return 0;
            case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(wp),theme::kTextPrimary);SetBkColor(reinterpret_cast<HDC>(wp),theme::kSurface);return reinterpret_cast<LRESULT>(p->background);
            case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,p->background);return 1;}
            case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);r.left=p->s(16);r.top=p->s(12);r.bottom=p->s(42);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,theme::kTextPrimary);auto old=SelectObject(dc,p->font);::mc::gui::localizedDrawText(dc,L"MagicControl " L"" MAGICCONTROL_VERSION_STRING,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);EndPaint(w,&ps);return 0;}
            case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);const bool disabled=(item.itemState&ODS_DISABLED)!=0;
                auto brush=CreateSolidBrush(item.itemState&ODS_SELECTED?theme::kAccent:theme::kSurfaceRaised);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
                SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled?theme::kTextMuted:theme::kTextPrimary);auto old=SelectObject(item.hDC,p->font);wchar_t title[64]{};GetWindowTextW(item.hwndItem,title,64);auto rect=item.rcItem;::mc::gui::localizedDrawText(item.hDC,title,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item.hDC,old);if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&rect);return TRUE;}
            case WM_DESTROY:KillTimer(w,1);p->window=nullptr;return 0;
        }return DefWindowProcW(w,m,wp,lp);
    }
};
}
bool showUpdateWindow(HWND owner,UINT dpi){
    View view;view.dpi=dpi?dpi:96;const auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=View::procedure;cls.lpszClassName=L"MagicControl.Update";cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));RegisterClassW(&cls);
    RECT parent{};GetWindowRect(owner,&parent);RECT rect{0,0,view.s(520),view.s(222)};AdjustWindowRectExForDpi(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME,view.dpi);
    const auto title=localized(L"MagicControl — Обновления");
    auto w=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,title.c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,(parent.left+parent.right-(rect.right-rect.left))/2,(parent.top+parent.bottom-(rect.bottom-rect.top))/2,rect.right-rect.left,rect.bottom-rect.top,owner,nullptr,instance,&view);
    if(!w)return false;BOOL dark=TRUE;DwmSetWindowAttribute(w,20,&dark,sizeof(dark));
    SendMessageW(w,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadIconW(instance,MAKEINTRESOURCEW(IDI_MAGICCONTROL))));runModalDialog(owner,w);
    view.operations.shutdown();if(view.window)closeModalDialog(view.window);
    return view.operations.snapshot().ready;
}
}
