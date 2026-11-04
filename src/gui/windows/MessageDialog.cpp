#include "MagicControl/ActionLog.h"
#include "../../platform/windows/TextWindows.h"
#include "LocalizationWindows.h"
#include "MessageDialog.h"
#include "ControlTheme.h"
#include "PageMechanics.h"
#include "Layout.h"
#include "IconResource.h"
#include <dwmapi.h>
#include <algorithm>
#include <windowsx.h>
#include <richedit.h>
#include "MagicControl/ScrollModel.h"

namespace mc::gui {
namespace {
constexpr wchar_t kModalContextProperty[]=L"MagicControl.ModalContext";
struct ModalContext {
    HWND owner{};
    bool restoreEnabled{};
    void enableOwner(){
        if(!restoreEnabled)return;
        restoreEnabled=false; // EnableWindow can dispatch messages reentrantly.
        if(IsWindow(owner))EnableWindow(owner,TRUE);
    }
};
}
void closeModalDialog(HWND dialog){
    if(!IsWindow(dialog))return;
    auto* context=static_cast<ModalContext*>(GetPropW(dialog,kModalContextProperty));
    const bool restoringOwner=context&&context->restoreEnabled;
    // EnableWindow documentation requires this order. Destroying a modal
    // popup while its owner is disabled activates a different window, then
    // restoring our owner causes the whole application to flash/reactivate.
    if(context)context->enableOwner();
    if(restoringOwner)audit::event("ui.modal_close",
        IsWindowEnabled(context->owner)?"\"owner_enabled_before_destroy\":true":"\"owner_enabled_before_destroy\":false");
    if(context&&IsWindow(dialog)&&GetPropW(dialog,kModalContextProperty)==context)
        RemovePropW(dialog,kModalContextProperty);
    if(IsWindow(dialog))DestroyWindow(dialog);
}
void runModalDialog(HWND owner, HWND dialog, HWND initialFocus) {
    if(!IsWindow(dialog))return;
    const bool enabled = IsWindowEnabled(owner) != FALSE;
    const HWND previousFocus = GetFocus();
    ModalContext context{owner,enabled};
    if(!SetPropW(dialog,kModalContextProperty,&context)){
        DestroyWindow(dialog);return; // Owner has not been disabled yet.
    }
    if (enabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    if (initialFocus) SetFocus(initialFocus);
    MSG message{};
    while (IsWindow(dialog)&&GetPropW(dialog,kModalContextProperty)==&context) {
        const BOOL got = GetMessageW(&message, nullptr, 0, 0);
        if (got <= 0) { if (!got) PostQuitMessage(static_cast<int>(message.wParam)); break; }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
            (message.hwnd == dialog || IsChild(dialog, message.hwnd))) {
            SendMessageW(dialog, WM_CLOSE, 0, 0); continue;
        }
        if (!IsDialogMessageW(dialog, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (IsWindow(dialog)&&GetPropW(dialog,kModalContextProperty)==&context)
        closeModalDialog(dialog);
    context.enableOwner(); // Fallback for externally destroyed windows/WM_QUIT.
    if (enabled && IsWindow(owner)) {
        if(GetActiveWindow()!=owner)SetActiveWindow(owner);
        if(IsWindow(previousFocus)&&IsWindowEnabled(previousFocus)&&
           (previousFocus==owner||IsChild(owner,previousFocus))&&GetFocus()!=previousFocus)
            SetFocus(previousFocus);
    }
}
namespace {
struct Information {
    HWND window{},textView{};std::vector<HWND> buttons;std::vector<std::wstring> choices;int response{-1};
    UINT dpi{96}; HFONT font{};
    std::wstring message;
    page::Buffer buffer;
    HBRUSH surface{CreateSolidBrush(theme::kSurface)};
    ScrollModel scroll;bool hot{},dragging{},owned{},cancelSent{},rich{};int grabOffset{},lineHeight{20};
    std::shared_ptr<OperationMessages> operation;std::function<void()> cancel;std::size_t cursor{};
    ~Information() { if (font) DeleteObject(font);DeleteObject(surface); }
    int s(int n) const { return MulDiv(n, dpi, 96); }
    void fonts() {
        if (font) DeleteObject(font);
        font = createUiFont(ui::FontRole::ClassicBody, dpi);
        for(auto button:buttons) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        if(textView)SendMessageW(textView,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        const auto dc=GetDC(window);const auto old=SelectObject(dc,font);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);lineHeight=std::max(1,int(tm.tmHeight));SelectObject(dc,old);ReleaseDC(window,dc);
    }
    int viewportHeight()const{RECT r{};GetClientRect(window,&r);return std::max(1,int(r.bottom)-s(74));}
    RECT thumb()const{RECT r{};GetClientRect(window,&r);auto t=page::scrollThumb(r.right,viewportHeight(),scroll,dpi,hot,dragging);OffsetRect(&t,0,s(16));return t;}
    void syncScroll(){
        const int first=static_cast<int>(SendMessageW(textView,EM_GETFIRSTVISIBLELINE,0,0));
        SendMessageW(textView,EM_LINESCROLL,0,scroll.position()/lineHeight-first);InvalidateRect(window,nullptr,FALSE);
    }
    void measureScroll(bool follow){
        scroll.setViewport(viewportHeight());scroll.setExtent(static_cast<int>(SendMessageW(textView,EM_GETLINECOUNT,0,0))*lineHeight);
        if(follow)scroll.setPosition(scroll.maximumPosition());syncScroll();
    }
    void pump(){
        if(!operation)return;
        const bool follow=scroll.atEnd();auto batch=operation->read(cursor);
        if(!batch.empty()){
            DWORD selectionStart{},selectionEnd{};SendMessageW(textView,EM_GETSEL,reinterpret_cast<WPARAM>(&selectionStart),reinterpret_cast<LPARAM>(&selectionEnd));
            SendMessageW(textView,WM_SETREDRAW,FALSE,0);
            for(const auto& row:batch){
                const auto end=GetWindowTextLengthW(textView);SendMessageW(textView,EM_SETSEL,end,end);
                if(rich){CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;
                    format.crTextColor=row.tone==OperationMessages::Tone::Error?theme::kError:row.tone==OperationMessages::Tone::Success?theme::kOk:theme::kTextPrimary;
                    SendMessageW(textView,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));}
                const auto text=mc::windows::encoding::wide(row.text)+L"\r\n";SendMessageW(textView,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(text.c_str()));
            }
            if(selectionStart!=selectionEnd)SendMessageW(textView,EM_SETSEL,selectionStart,selectionEnd);
            measureScroll(follow);SendMessageW(textView,WM_SETREDRAW,TRUE,0);InvalidateRect(textView,nullptr,FALSE);
        }
        if(!buttons.empty())SetWindowTextW(buttons.front(),localized(operation->done()?L"Закрыть":cancelSent?L"Отмена…":L"Отмена").c_str());
    }
    void close(){
        if(operation&&!operation->done()){
            if(!cancelSent){cancelSent=true;operation->append("Запрошена отмена. Ожидание безопасного завершения текущего шага.");if(cancel)cancel();}
            pump();return;
        }
        closeModalDialog(window);
    }
    static LRESULT CALLBACK editProcedure(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto* self=reinterpret_cast<Information*>(data);
        if(m==WM_GETDLGCODE)return DefSubclassProc(w,m,wp,lp)|DLGC_WANTTAB;
        if(m==WM_KEYDOWN&&wp==VK_ESCAPE){self->close();return 0;}
        if(m==WM_KEYDOWN&&wp==VK_TAB){
            std::vector<HWND> order{self->textView};order.insert(order.end(),self->buttons.begin(),self->buttons.end());
            const auto found=std::find(order.begin(),order.end(),w);const int at=found==order.end()?0:static_cast<int>(found-order.begin());
            const int direction=(GetKeyState(VK_SHIFT)&0x8000)?-1:1;
            SetFocus(order[static_cast<std::size_t>((at+direction+static_cast<int>(order.size()))%order.size())]);return 0;
        }
        if(m==WM_MOUSEWHEEL){SendMessageW(self->window,m,wp,lp);return 0;}
        const auto result=DefSubclassProc(w,m,wp,lp);
        if(w==self->textView&&(m==WM_KEYUP||m==WM_LBUTTONUP)){self->scroll.setPosition(static_cast<int>(SendMessageW(w,EM_GETFIRSTVISIBLELINE,0,0))*self->lineHeight);InvalidateRect(self->window,nullptr,FALSE);}
        return result;
    }
    void arrange() {
        RECT rect{}; GetClientRect(window, &rect);
        const int count=static_cast<int>(buttons.size()),width=std::max(s(100),(static_cast<int>(rect.right)-s(32)-s(8)*(count-1))/std::max(1,count));
        page::place(textView,s(16),s(16),std::max(1,int(rect.right)-s(44)),viewportHeight());
        if(count==1)page::place(buttons[0],rect.right-s(112),rect.bottom-s(42),s(100),s(30));
        else for(int i=0;i<count;++i)page::place(buttons[i],s(16)+i*(width+s(8)),rect.bottom-s(42),width,s(30));
        measureScroll(scroll.atEnd());
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
    }
    void paint() {
        page::Paint painting(window, buffer); if (!painting.valid()) return;
        const auto dc = painting.dc(); auto rect = painting.client();
        const auto brush = CreateSolidBrush(theme::kSurface); FillRect(dc, &rect, brush); DeleteObject(brush);
        if(scroll.scrollable())page::drawThumb(dc,thumb(),hot,dragging);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Information*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<Information*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, msg, wp, lp);
        switch (msg) {
        case WM_CREATE:
            {static const auto richModule=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);self->rich=self->operation&&richModule;
            self->textView=CreateWindowExW(0,self->rich?L"RICHEDIT50W":L"EDIT",self->message.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_LEFT,0,0,0,0,window,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(self->rich)SendMessageW(self->textView,EM_SETBKGNDCOLOR,0,theme::kSurface);}
            SendMessageW(self->textView,EM_SETLIMITTEXT,0x7ffffffe,0);
            SetWindowSubclass(self->textView,editProcedure,1,reinterpret_cast<DWORD_PTR>(self));
            self->fonts();
            for(std::size_t i=0;i<self->choices.size();++i){
                auto button=CreateWindowExW(0,L"BUTTON",self->choices[i].c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                    0,0,0,0,window,reinterpret_cast<HMENU>(INT_PTR(IDOK+i)),GetModuleHandleW(nullptr),nullptr);
                control::themeButton(button);SetWindowSubclass(button,editProcedure,2,reinterpret_cast<DWORD_PTR>(self));SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),FALSE);self->buttons.push_back(button);
            }
            if(self->operation)SetTimer(window,1,80,nullptr);
            return 0;
        case WM_TIMER:self->pump();return 0;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(wp),theme::kTextPrimary);SetBkColor(reinterpret_cast<HDC>(wp),theme::kSurface);return reinterpret_cast<LRESULT>(self->surface);
        case WM_MOUSEWHEEL:page::wheel(self->scroll,GET_WHEEL_DELTA_WPARAM(wp),self->dpi);self->syncScroll();return 0;
        case WM_LBUTTONDOWN:{RECT r{};GetClientRect(window,&r);if(self->scroll.scrollable()&&GET_X_LPARAM(lp)>=r.right-self->s(12)){
            auto t=self->thumb();OffsetRect(&t,0,-self->s(16));page::scrollPress(window,self->scroll,t,GET_Y_LPARAM(lp)-self->s(16),self->dragging,self->grabOffset);self->syncScroll();}return 0;}
        case WM_MOUSEMOVE:{const int y=GET_Y_LPARAM(lp)-self->s(16);if(self->dragging){auto t=self->thumb();OffsetRect(&t,0,-self->s(16));page::scrollDrag(self->scroll,t,self->viewportHeight(),self->dpi,y,self->grabOffset);self->syncScroll();}
            page::hover(window,self->scroll,self->dpi,GET_X_LPARAM(lp),self->hot);TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);InvalidateRect(window,nullptr,FALSE);return 0;}
        case WM_LBUTTONUP:if(self->dragging)ReleaseCapture();return 0;
        case WM_CAPTURECHANGED:self->dragging=false;InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_MOUSELEAVE:self->hot=false;InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_SIZE: self->arrange(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: self->paint(); return 0;
        case WM_DRAWITEM: control::drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp), self->font, self->dpi); return TRUE;
        case WM_DPICHANGED: {
            self->dpi = HIWORD(wp); self->fonts();
            const auto rect = reinterpret_cast<RECT*>(lp);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right-rect->left, rect->bottom-rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            self->arrange(); return 0;
        }
        case WM_COMMAND: if(HIWORD(wp)==BN_CLICKED&&LOWORD(wp)>=IDOK&&LOWORD(wp)<IDOK+self->buttons.size()){
            self->response=LOWORD(wp)-IDOK;self->close();return 0;}break;
        case WM_CLOSE:self->close();return 0;
        case WM_NCDESTROY:{KillTimer(window,1);const bool owned=self->owned;SetWindowLongPtrW(window,GWLP_USERDATA,0);const auto result=DefWindowProcW(window,msg,wp,lp);if(owned)delete self;return result;}
        }
        return DefWindowProcW(window, msg, wp, lp);
    }
};
}
int showChoiceDialog(HWND owner,UINT dpi,const std::wstring& message,const std::vector<std::wstring>& choices) {
    audit::Scope trace("ui.choice_dialog","\"message\":"+audit::quote(mc::windows::encoding::utf8(message)));
    if(!owner)dpi=GetDpiForSystem();
    Information view; view.dpi = dpi ? dpi : 96; view.message = message;view.choices=choices;if(choices.empty()||choices.size()>3)return -1;
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = Information::procedure;
    cls.lpszClassName = L"MagicControl.Information"; cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&cls);
    // Measure the actual text with the project font; keep this window compact.
    view.fonts(); const auto dc = GetDC(owner); const auto previous = SelectObject(dc, view.font);
    RECT text{0,0,view.s(736),0};
    ::mc::gui::localizedDrawText(dc, message.c_str(), -1, &text, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, previous); ReleaseDC(owner, dc);
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    RECT bounds{0,0,std::min(view.s(780),int(work.right-work.left)-view.s(32)),std::min(std::max(view.s(300),static_cast<int>(text.bottom)+view.s(74)),int(work.bottom-work.top)*3/4)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&bounds, style, FALSE, WS_EX_DLGMODALFRAME, view.dpi);
    RECT parent{};if(!owner||!GetWindowRect(owner,&parent))SystemParametersInfoW(SPI_GETWORKAREA,0,&parent,0);
    const auto window = CreateWindowExW(WS_EX_DLGMODALFRAME, cls.lpszClassName, L"MagicControl", style,
        (parent.left+parent.right-(bounds.right-bounds.left))/2, (parent.top+parent.bottom-(bounds.bottom-bounds.top))/2,
        bounds.right-bounds.left, bounds.bottom-bounds.top, owner, nullptr, instance, &view);
    if (!window) return -1;
    const BOOL dark = TRUE; DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIconW(instance, MAKEINTRESOURCEW(IDI_MAGICCONTROL))));
    runModalDialog(owner, window, view.buttons.empty()?nullptr:view.buttons.back());
    trace.finish(view.response>=0?"selected":"cancelled","\"choice\":"+std::to_string(view.response));return view.response;
}
void showInformationDialog(HWND owner,UINT dpi,const std::wstring& message){
    static_cast<void>(showChoiceDialog(owner,dpi,message,{L"ОК"}));
}
void showOperationDialog(HWND owner,UINT dpi,const std::wstring& title,std::shared_ptr<OperationMessages> messages,std::function<void()> cancel){
    auto* view=new Information;view->dpi=dpi?dpi:96;view->operation=std::move(messages);view->cancel=std::move(cancel);view->choices={L"Отмена"};
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=Information::procedure;cls.lpszClassName=L"MagicControl.Information";cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));RegisterClassW(&cls);
    RECT parent{},work{};if(!GetWindowRect(owner,&parent))SystemParametersInfoW(SPI_GETWORKAREA,0,&parent,0);SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    RECT bounds{0,0,std::min(view->s(780),int(work.right-work.left)-view->s(32)),std::min(view->s(440),int(work.bottom-work.top)*3/4)};
    const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&bounds,style,FALSE,0,view->dpi);
    const auto window=CreateWindowExW(0,cls.lpszClassName,title.c_str(),style,(parent.left+parent.right-(bounds.right-bounds.left))/2,(parent.top+parent.bottom-(bounds.bottom-bounds.top))/2,bounds.right-bounds.left,bounds.bottom-bounds.top,owner,nullptr,instance,view);
    if(!window){delete view;return;}
    view->owned=true;
    const BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadIconW(instance,MAKEINTRESOURCEW(IDI_MAGICCONTROL))));
    view->arrange();view->pump();page::presentPopup(window);
}

}
