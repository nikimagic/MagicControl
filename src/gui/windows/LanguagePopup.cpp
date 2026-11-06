#include "MagicControl/ActionLog.h"
#include "LocalizationWindows.h"
#include "LanguagePopup.h"
#include "MessageDialog.h"
#include "PageMechanics.h"
#include "Layout.h"
#include "../common/Theme.h"
#include "../../platform/windows/TextWindows.h"
#include "IconResource.h"
#include "../common/DeviceHeader.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <windowsx.h>
#include "MagicControl/ScrollModel.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace mc::gui {
namespace {
constexpr wchar_t kWallet[] = L"UQBm4jj-N5u15h9E-JM61bZBYcl84m7JRfR0VRwX3IMUiFfu";
constexpr wchar_t kGithub[] = L"https://github.com/nikimagic/MagicControl";
constexpr wchar_t kDonationWebsite[] = L"https://gram.org/";
// Bounded, resizable license viewer. Read-only text can be selected/copied;
// the full UTF-8 license is embedded from the existing project file, not paraphrased.
struct LicenseView {
    HWND window{}, edit{}, close{}, scrollbar{};
    HFONT font{}; HICON smallIcon{}, largeIcon{}; UINT dpi{96};
    HBRUSH background{CreateSolidBrush(theme::kSurface)};
    std::wstring text;
    ScrollModel scroll; bool hot{}, dragging{}; int grab{};
    int s(int n) const { return MulDiv(n,static_cast<int>(dpi),96); }
    ~LicenseView(){if(font)DeleteObject(font);if(smallIcon)DestroyIcon(smallIcon);if(largeIcon)DestroyIcon(largeIcon);DeleteObject(background);}
    // LICENSE UI CONTRACT: preserve native read-only EDIT selection/copy, but do
    // not enable WS_VSCROLL: the white system scrollbar does not match the app.
    // The opaque fixed scrollbar is a separate sibling, never scrolled with text.
    // Use the same 12-DIP lane, 5/7-DIP rounded thumb and palette as KEYs/FANs.
    // Wrapped line count and visible EDIT lines determine extent/viewport; resize
    // must remeasure wrapping and keyboard/selection scrolling must sync the thumb.
    void icons() {
        const auto instance=GetModuleHandleW(nullptr);
        const auto load=[&](int size){return static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_MAGICCONTROL),IMAGE_ICON,size,size,LR_DEFAULTCOLOR));};
        const auto loadedSmallIcon=load(GetSystemMetricsForDpi(SM_CXSMICON,dpi));
        const auto loadedLargeIcon=load(GetSystemMetricsForDpi(SM_CXICON,dpi));
        SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(loadedSmallIcon));
        SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(loadedLargeIcon));
        if(smallIcon)DestroyIcon(smallIcon);if(largeIcon)DestroyIcon(largeIcon);
        smallIcon=loadedSmallIcon;largeIcon=loadedLargeIcon;
    }
    void syncScroll(bool repaint=true) {
        if(!edit||!scrollbar)return;
        RECT r{};SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&r));
        const auto dc=GetDC(edit);const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(edit,dc);
        scroll.setExtent(static_cast<int>(SendMessageW(edit,EM_GETLINECOUNT,0,0)));
        scroll.setViewport(std::max(1,static_cast<int>(r.bottom-r.top)/std::max(1,static_cast<int>(metrics.tmHeight))));
        scroll.setPosition(static_cast<int>(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0)));
        const bool visible=IsWindowVisible(scrollbar)!=FALSE;
        if(visible!=scroll.scrollable())ShowWindow(scrollbar,scroll.scrollable()?SW_SHOWNA:SW_HIDE);
        if(repaint)InvalidateRect(scrollbar,nullptr,FALSE);
    }
    void scrollTo(int position) {
        syncScroll(false);scroll.setPosition(position);
        const int first=static_cast<int>(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0));
        SendMessageW(edit,EM_LINESCROLL,0,scroll.position()-first);
        syncScroll();
    }
    void wheel(int delta) {
        syncScroll(false);
        page::wheel(scroll,delta,dpi,[this](int position){scrollTo(position);},1,1);
    }
    RECT thumb() const {
        RECT r{};GetClientRect(scrollbar,&r);
        return page::scrollThumb(r.right,r.bottom,scroll,dpi,hot,dragging,true,true);
    }
    void arrange() {
        RECT r{};GetClientRect(window,&r);const int pad=s(12), button=s(30),lane=s(12);
        // Match page scrollbars: lane touches the client right edge. Never add
        // another content padding to the right of the fixed scrollbar.
        const int scrollLeft=std::max(0,static_cast<int>(r.right)-lane);
        const int height=std::max(1,static_cast<int>(r.bottom)-3*pad-button);
        MoveWindow(edit,pad,pad,std::max(1,scrollLeft-pad),height,TRUE);
        SetWindowPos(scrollbar,HWND_TOP,scrollLeft,pad,lane,height,SWP_NOACTIVATE);
        MoveWindow(close,std::max(pad,static_cast<int>(r.right)-pad-s(100)),std::max(pad,static_cast<int>(r.bottom)-pad-button),s(100),button,TRUE);
        syncScroll();InvalidateRect(window,nullptr,FALSE);
    }
    static LRESULT CALLBACK editProcedure(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
        auto* self=reinterpret_cast<LicenseView*>(data);
        if(m==WM_MOUSEWHEEL){self->wheel(GET_WHEEL_DELTA_WPARAM(wp));return 0;}
        if(m==WM_KEYDOWN&&wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(w,EM_SETSEL,0,-1);return 0;}
        if(m==WM_NCDESTROY)RemoveWindowSubclass(w,editProcedure,id);
        const auto result=DefSubclassProc(w,m,wp,lp);
        if(m==WM_KEYDOWN||m==WM_LBUTTONUP||m==WM_MOUSEMOVE||m==WM_TIMER||m==WM_VSCROLL)self->syncScroll();
        return result;
    }
    static LRESULT CALLBACK scrollProcedure(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<LicenseView*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<LicenseView*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(w,m,wp,lp);
        switch(m){
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{self->syncScroll(false);PAINTSTRUCT ps{};const auto dc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);FillRect(dc,&r,self->background);
            if(self->scroll.scrollable())page::drawThumb(dc,self->thumb(),self->hot,self->dragging);
            EndPaint(w,&ps);return 0;}
        case WM_MOUSEWHEEL:self->wheel(GET_WHEEL_DELTA_WPARAM(wp));return 0;
        case WM_LBUTTONDOWN:{SetFocus(self->edit);const int y=GET_Y_LPARAM(lp);const auto t=self->thumb();
            page::scrollPress(w,self->scroll,t,y,self->dragging,self->grab,[self](int position){self->scrollTo(position);});
            InvalidateRect(w,nullptr,FALSE);return 0;}
        case WM_MOUSEMOVE:
            if(self->dragging){RECT r{};GetClientRect(w,&r);const auto t=self->thumb();
                page::scrollDrag(self->scroll,t,r.bottom,self->dpi,GET_Y_LPARAM(lp),self->grab,[self](int position){self->scrollTo(position);});}
            if(!self->hot){self->hot=true;InvalidateRect(w,nullptr,FALSE);}
            {TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);}return 0;
        case WM_LBUTTONUP:if(self->dragging)ReleaseCapture();return 0;
        case WM_CAPTURECHANGED:self->dragging=false;InvalidateRect(w,nullptr,FALSE);return 0;
        case WM_MOUSELEAVE:self->hot=false;InvalidateRect(w,nullptr,FALSE);return 0;
        }
        return DefWindowProcW(w,m,wp,lp);
    }
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<LicenseView*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<LicenseView*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(w,m,wp,lp);
        switch(m){
        case WM_CREATE:{
            self->font=createUiFont(ui::FontRole::ClassicBody,self->dpi);self->icons();
            self->edit=CreateWindowExW(0,L"EDIT",self->text.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
                0,0,0,0,w,nullptr,GetModuleHandleW(nullptr),nullptr);
            SetWindowSubclass(self->edit,editProcedure,1,reinterpret_cast<DWORD_PTR>(self));
            WNDCLASSW cls{};cls.lpfnWndProc=scrollProcedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"MagicControl.LicenseScrollbar";
            cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));RegisterClassW(&cls);
            self->scrollbar=CreateWindowExW(0,cls.lpszClassName,L"",WS_CHILD|WS_CLIPSIBLINGS,0,0,0,0,w,nullptr,cls.hInstance,self);
            self->close=CreateWindowExW(0,L"BUTTON",L"Закрыть",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,w,reinterpret_cast<HMENU>(1),cls.hInstance,nullptr);
            SendMessageW(self->edit,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);SendMessageW(self->close,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);return 0;}
        case WM_SIZE:self->arrange();return 0;
        case WM_DPICHANGED:{self->dpi=HIWORD(wp);const auto old=self->font;self->font=createUiFont(ui::FontRole::ClassicBody,self->dpi);
            SendMessageW(self->edit,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);SendMessageW(self->close,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);DeleteObject(old);self->icons();
            const auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);self->arrange();return 0;}
        case WM_MOUSEWHEEL:self->wheel(GET_WHEEL_DELTA_WPARAM(wp));return 0;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(wp),theme::kTextPrimary);SetBkColor(reinterpret_cast<HDC>(wp),theme::kSurface);return reinterpret_cast<LRESULT>(self->background);
        case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,self->background);return 1;}
        case WM_DRAWITEM:{const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if(item->CtlID==1){const auto brush=CreateSolidBrush(theme::kSurfaceRaised);FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
                SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,theme::kTextPrimary);const auto old=SelectObject(item->hDC,self->font);RECT r=item->rcItem;
                ::mc::gui::localizedDrawText(item->hDC,L"Закрыть",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item->hDC,old);if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&r);return TRUE;}break;}
        case WM_COMMAND:if(LOWORD(wp)==1){closeModalDialog(w);return 0;}break;
        case WM_CLOSE:closeModalDialog(w);return 0;
        case WM_NCDESTROY:self->window=nullptr;break;
        }
        return DefWindowProcW(w,m,wp,lp);
    }
};
void showProjectLicense(HWND owner,UINT dpi) {
    LicenseView view;view.dpi=dpi;
    const auto instance=GetModuleHandleW(nullptr);
    const auto resource=FindResourceW(instance,MAKEINTRESOURCEW(IDR_PROJECT_LICENSE),MAKEINTRESOURCEW(10));
    if(!resource)return;
    const auto size=SizeofResource(instance,resource);
    const auto bytes=static_cast<const char*>(LockResource(LoadResource(instance,resource)));
    const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,static_cast<int>(size),nullptr,0);
    if(length<=0)return;view.text.resize(length);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,static_cast<int>(size),view.text.data(),length);
    WNDCLASSW cls{};cls.lpfnWndProc=LicenseView::procedure;cls.hInstance=instance;
    cls.lpszClassName=L"MagicControl.ProjectLicense";cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));RegisterClassW(&cls);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const int width=std::min(view.s(650),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-view.s(24));
    const int height=std::min(view.s(500),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-view.s(24));
    HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"MagicControl — Лицензия",WS_OVERLAPPEDWINDOW,
        monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,
        monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,owner,nullptr,instance,&view);
    if(!window)return;
    const BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
    runModalDialog(owner,window,view.edit);
}
struct Panel {
    HWND window{}, owner{};
    page::Buffer buffer;
    UINT dpi{96}; int selected{}, result{}, hot{-1}, focus{-1};
    HFONT body{}, heading{}, detail{}; bool copied{},qrCopied{};
    bool laptopMenu{};
    int languageSelection{},modelSelection{};
    ui::LaptopMenuGeometry modelGeometry;
    ui::LanguageMenuGeometry languageGeometry;
    ScrollModel modelScroll;
    bool modelScrollHot{},modelDragging{};int modelGrab{};
    std::vector<unsigned char> pixels; UINT bitmapWidth{}, bitmapHeight{};
    int s(int v) const { return MulDiv(v, static_cast<int>(dpi), 96); }
    RECT modelRowRect(int id) const {
        const int row=ui::laptopMenuRow(id);if(row<0)return {};
        auto r=toNativeRect(modelGeometry.rows[row]);OffsetRect(&r,0,-modelScroll.position());return r;
    }
    RECT hitRect(int id) const {
        if(laptopMenu){if(id==ui::kLaptopMenuBackItem)return toNativeRect(modelGeometry.title);
            const auto r=modelRowRect(id),viewport=toNativeRect(modelGeometry.viewport);RECT clipped{};
            IntersectRect(&clipped,&r,&viewport);return clipped;}
        if(id>=0&&id<4)return toNativeRect(languageGeometry.languages[id]);
        switch(id){
        case 9:return toNativeRect(languageGeometry.models);
        case 6:return toNativeRect(languageGeometry.license);
        case 4:return toNativeRect(languageGeometry.github);
        case 7:return toNativeRect(languageGeometry.website);
        case 8:return toNativeRect(languageGeometry.qr);
        case 5:return toNativeRect(languageGeometry.wallet);
        default:return {};
        }
    }
    void measureLaptopMenu(int availableWidth,int availableHeight){
        const auto dc=GetDC(owner);
        const auto measure=[&](const std::wstring& text,HFONT font){
            const auto previous=SelectObject(dc,font);SIZE size{};
            localizedTextExtent(dc,text.c_str(),static_cast<int>(text.size()),&size);
            SelectObject(dc,previous);return static_cast<int>(size.cx);
        };
        int longest{},compactLongest{},statusWidth{};
        for(int i=0;i<static_cast<int>(ui::kSupportedLaptops.size());++i){
            const int nameWidth=measure(mc::windows::encoding::wide(std::string(ui::kSupportedLaptops[i].name)),body);
            longest=std::max(longest,nameWidth);
            if(i<static_cast<int>(ui::kCompactMenuReferenceModels))compactLongest=std::max(compactLongest,nameWidth);
            statusWidth=std::max(statusWidth,measure(mc::windows::encoding::wide(ui::laptopSupportCaption(i)),detail));}
        const auto previous=SelectObject(dc,body);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,previous);
        modelGeometry=ui::laptopMenuGeometry(static_cast<int>(ui::kSupportedLaptops.size()),longest,
            measure(mc::windows::encoding::wide(ui::kLaptopsCaption),heading),measure(mc::windows::encoding::wide(ui::kLaptopPreviewNote),detail),
            static_cast<int>(metrics.tmHeight),availableWidth,ui::Scale{static_cast<int>(dpi)},statusWidth,availableHeight);
        modelScroll.setExtent(modelGeometry.rowsExtent);modelScroll.setViewport(modelGeometry.viewport.height());
        languageGeometry=ui::languageMenuGeometry(ui::menuWidthFromText(compactLongest,
            measure(mc::windows::encoding::wide(ui::kLaptopsCaption),heading),measure(mc::windows::encoding::wide(ui::kLaptopPreviewNote),detail),
            availableWidth,ui::Scale{static_cast<int>(dpi)}),ui::Scale{static_cast<int>(dpi)});
        ReleaseDC(owner,dc);
    }
    RECT modelThumb() const {
        auto thumb=page::scrollThumb(modelGeometry.width,modelGeometry.viewport.height(),modelScroll,dpi,modelScrollHot,modelDragging);
        OffsetRect(&thumb,0,modelGeometry.viewport.top);return thumb;
    }
    void ensureModelVisible(int id){
        const int row=ui::laptopMenuRow(id);if(row<0)return;
        const int top=row*modelGeometry.rowHeight,bottom=top+modelGeometry.rowHeight;
        if(top<modelScroll.position())modelScroll.setPosition(top);
        else if(bottom>modelScroll.position()+modelScroll.viewport())modelScroll.setPosition(bottom-modelScroll.viewport());
    }
    void loadQr() {
        using Microsoft::WRL::ComPtr;
        const auto instance = GetModuleHandleW(nullptr);
        HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(IDR_DONATION_QR), MAKEINTRESOURCEW(10));
        if (!resource) return;
        const auto size = SizeofResource(instance, resource);
        const auto data = LockResource(LoadResource(instance, resource));
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))) return;
        ComPtr<IWICStream> stream;
        if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(
            static_cast<BYTE*>(const_cast<void*>(data)), size))) return;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))) return;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0,&frame)) || FAILED(frame->GetSize(&bitmapWidth,&bitmapHeight))) return;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(frame.Get(),
            GUID_WICPixelFormat32bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return;
        pixels.resize(static_cast<size_t>(bitmapWidth)*bitmapHeight*4);
        if (FAILED(converter->CopyPixels(nullptr,bitmapWidth*4,static_cast<UINT>(pixels.size()),pixels.data()))) pixels.clear();
    }
    static void fill(HDC dc,RECT r,COLORREF color) {
        const auto brush=CreateSolidBrush(color); FillRect(dc,&r,brush); DeleteObject(brush);
    }
    void label(HDC dc,const wchar_t* value,RECT r,COLORREF color,bool bold=false,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
        const auto previous=SelectObject(dc,bold?heading:body);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
        ::mc::gui::localizedDrawText(dc,value,-1,&r,flags|DT_NOPREFIX);SelectObject(dc,previous);
    }
    void paint() {
        page::Paint painting(window, buffer);
        if (!painting.valid()) return;
        const auto dc = painting.dc();
        const auto& client = painting.client();
        fill(dc,client,theme::kSurface);
        if(laptopMenu){if(hot==ui::kLaptopMenuBackItem)fill(dc,hitRect(hot),theme::kHoverOverlay);
            label(dc,mc::windows::encoding::wide(ui::kLaptopsCaption).c_str(),toNativeRect(modelGeometry.title),theme::kTextSecondary,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            label(dc,mc::windows::encoding::wide(ui::kLaptopsBackArrow).c_str(),toNativeRect(modelGeometry.backArrow),theme::kTextSecondary,true,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            fill(dc,{s(16),s(35),modelGeometry.width-s(16),s(36)},theme::kDivider);
            const int saved=SaveDC(dc);const auto viewport=toNativeRect(modelGeometry.viewport);
            IntersectClipRect(dc,viewport.left,viewport.top,viewport.right,viewport.bottom);
            for(const int i:ui::kLaptopMenuOrder){auto r=modelRowRect(i);if(r.bottom<=viewport.top||r.top>=viewport.bottom)continue;
                if(hot==i)fill(dc,r,theme::kHoverOverlay);
                if(i==selected){auto mark=r;mark.left+=s(4);mark.right=mark.left+s(14);label(dc,L"•",mark,theme::kAccentHover,false,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
                r.left+=s(24);r.right-=s(8);const auto name=mc::windows::encoding::wide(std::string(ui::kSupportedLaptops[i].name));
                auto status=r;status.left=std::max(r.left,status.right-modelGeometry.statusWidth);r.right=std::max(r.left,status.left-s(12));
                label(dc,name.c_str(),r,i==selected?theme::kAccentHover:theme::kTextPrimary,false,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                const auto normal=body;body=detail;label(dc,mc::windows::encoding::wide(ui::laptopSupportCaption(i)).c_str(),status,theme::kTextMuted,false,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);body=normal;}
            RestoreDC(dc,saved);
            if(modelScroll.scrollable())page::drawThumb(dc,modelThumb(),modelScrollHot,modelDragging);
            if(GetFocus()==window&&focus>=0){auto r=hitRect(focus);InflateRect(&r,-s(2),-s(2));DrawFocusRect(dc,&r);}
            const auto normalBody=body;body=detail;
            label(dc,mc::windows::encoding::wide(ui::kLaptopPreviewNote).c_str(),toNativeRect(modelGeometry.footer),theme::kTextMuted,false,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            body=normalBody;painting.finish();
            return;
        }
        for (int i=0;i<4;++i) {
            RECT r=hitRect(i);
            if(hot==i) fill(dc,r,theme::kHoverOverlay);
            const auto value=mc::windows::encoding::wide(std::string(ui::kLanguageOptions[i].label));
            r.left+=s(18);label(dc,value.c_str(),r,i==selected?theme::kAccentHover:theme::kTextPrimary);
            if(i==selected) label(dc,L"•",toNativeRect(languageGeometry.selectionMarks[i]),theme::kAccentHover);
        }
        fill(dc,toNativeRect(languageGeometry.modelsTopSeparator),theme::kDivider);
        if(hot==9)fill(dc,hitRect(9),theme::kHoverOverlay);
        label(dc,mc::windows::encoding::wide(ui::kLaptopsForwardCaption).c_str(),hitRect(9),theme::kTextPrimary,false,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        fill(dc,toNativeRect(languageGeometry.separators[0]),theme::kDivider);
        // Only the language grid is left-aligned. All sections below share the QR centre.
        constexpr UINT centred = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
        // Exact attribution from packaging/LICENSE.txt; do not invent a name.
        if(hot==6) fill(dc,hitRect(6),theme::kHoverOverlay);
        label(dc,L"© 2026 NikiMagic",toNativeRect(languageGeometry.copyright),theme::kTextPrimary,true,centred);
        const auto normalBody=body;body=detail;
        label(dc,L"All rights reserved.",toNativeRect(languageGeometry.reserved),theme::kTextSecondary,false,centred);
        body=normalBody;
        if(hot==4) fill(dc,hitRect(4),theme::kHoverOverlay);
        label(dc,L"GitHub · github.com",hitRect(4),theme::kAccentHover,false,centred);
        fill(dc,toNativeRect(languageGeometry.separators[1]),theme::kDivider);
        label(dc,L"Донаты",toNativeRect(languageGeometry.donations),theme::kTextPrimary,true,centred);
        if (!pixels.empty()) {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=static_cast<LONG>(bitmapWidth);info.bmiHeader.biHeight=-static_cast<LONG>(bitmapHeight);
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            const int side=languageGeometry.qr.width(),left=languageGeometry.qr.left;
            SetStretchBltMode(dc,COLORONCOLOR);
            StretchDIBits(dc,left,languageGeometry.qr.top,side,side,0,0,bitmapWidth,bitmapHeight,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
            if(hot==8){const auto brush=CreateSolidBrush(theme::kAccentHover);const auto bounds=hitRect(8);FrameRect(dc,&bounds,brush);DeleteObject(brush);}
        }
        if(hot==5) fill(dc,hitRect(5),theme::kHoverOverlay);
        // Centre each line by its measured glyph width, on the QR's centre axis.
        const std::wstring address(kWallet);
        const auto addressFont=SelectObject(dc,body);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,theme::kTextSecondary);
        TEXTMETRICW addressMetrics{};GetTextMetricsW(dc,&addressMetrics);
        const RECT addressBox=hitRect(5);
        const int lineHeight=addressMetrics.tmHeight;
        const int addressTop=addressBox.top+std::max(0,static_cast<int>(addressBox.bottom-addressBox.top)-2*lineHeight)/2;
        for(int row=0;row<2;++row) {
            const auto line=address.substr(static_cast<size_t>(row)*24,24);
            SIZE measured{};::mc::gui::localizedTextExtent(dc,line.c_str(),static_cast<int>(line.size()),&measured);
            const int x=(languageGeometry.width-measured.cx)/2;
            TextOutW(dc,x,addressTop+row*lineHeight,line.c_str(),static_cast<int>(line.size()));
        }
        SelectObject(dc,addressFont);
        const auto walletFont=body;body=detail;
        label(dc,qrCopied?L"QR-код скопирован":copied?L"Адрес скопирован":L"Нажмите, чтобы скопировать",toNativeRect(languageGeometry.copyHint),
            copied||qrCopied?theme::kBatteryCharging:theme::kTextMuted,false,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        body=walletFont;
        if(hot==7)fill(dc,hitRect(7),theme::kHoverOverlay);
        label(dc,L"gram.org",hitRect(7),theme::kAccentHover,false,centred);
        if(GetFocus()==window && focus>=0) {RECT r=hitRect(focus);InflateRect(&r,-s(2),-s(2));DrawFocusRect(dc,&r);}
        painting.finish();
    }
    void invalidateItem(int id) const {
        if (id < 0) return;
        const auto bounds = hitRect(id);
        InvalidateRect(window, &bounds, FALSE);
    }
    void setHot(int next) {
        if (hot == next) return;
        const int previous = hot; hot = next;
        invalidateItem(previous); invalidateItem(hot);
    }
    void setFocusItem(int next) {
        if (focus == next) return;
        const int previous = focus; focus = next;
        if(laptopMenu){const int before=modelScroll.position();ensureModelVisible(next);
            if(before!=modelScroll.position()){hot=-1;InvalidateRect(window,nullptr,FALSE);return;}}
        invalidateItem(previous); invalidateItem(focus);
    }
    void invalidateCopyHint() const {
        const auto bounds = toNativeRect(languageGeometry.copyHint);
        InvalidateRect(window, &bounds, FALSE);
    }
    void switchMenu(bool models,bool keyboard){
        // Navigate inside one popup, preserving width and language/model state.
        // A return must not reopen a modal popup from a destroyed window proc.
        RECT previous{};GetWindowRect(window,&previous);
        laptopMenu=models;selected=models?modelSelection:languageSelection;
        if(models)ensureModelVisible(selected);
        hot=-1;focus=keyboard?(models?ui::kLaptopMenuBackItem:9):-1;
        if(!models&&pixels.empty())loadQr();
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
        const int width=models?modelGeometry.width:languageGeometry.width,height=models?modelGeometry.height:languageGeometry.height;
        const int x=std::clamp(static_cast<int>(previous.left),static_cast<int>(monitor.rcWork.left),std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width));
        const int y=std::clamp(static_cast<int>(previous.top),static_cast<int>(monitor.rcWork.top),std::max(static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height));
        SetWindowPos(window,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(window,nullptr,FALSE);SetFocus(window);
    }
    void activate(int id,bool keyboard=false) {
        audit::input("windows.about_menu","menu_item_selected","\"item\":"+std::to_string(id)+",\"models_menu\":"+(laptopMenu?"true":"false")+",\"keyboard\":"+(keyboard?"true":"false"));
        audit::Scope trace("ui.about_menu_activate","\"item\":"+std::to_string(id));
        if(id<0)return;
        if(laptopMenu){if(id==ui::kLaptopMenuBackItem){switchMenu(false,keyboard);return;}
            result=100+id;DestroyWindow(window);return;}
        if(id==9){switchMenu(true,keyboard);return;}
        if(id==6) {const HWND parent=owner;DestroyWindow(window);showProjectLicense(parent,dpi);return;}
        if(id<4) {result=id;DestroyWindow(window);return;}
        if(id==4) {ShellExecuteW(window,L"open",kGithub,nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(id==7) {ShellExecuteW(window,L"open",kDonationWebsite,nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(id==8){copyQr();return;}
        if(OpenClipboard(window)) {
            if (const auto memory=GlobalAlloc(GMEM_MOVEABLE,sizeof(kWallet))) {
                if(const auto target=GlobalLock(memory)) {
                    CopyMemory(target,kWallet,sizeof(kWallet));GlobalUnlock(memory);
                    EmptyClipboard();if(SetClipboardData(CF_UNICODETEXT,memory)){copied=true;qrCopied=false;}else GlobalFree(memory);
                } else GlobalFree(memory);
            }
            CloseClipboard();invalidateCopyHint();
        }
    }
    void copyQr(){
        if(pixels.empty()||!bitmapWidth||!bitmapHeight)return;
        // Copy original pixels, not the 180-DIP display or a screenshot. CF_DIB
        // with 24-bit BGR has no ambiguous alpha and pastes as an image.
        const size_t stride=(static_cast<size_t>(bitmapWidth)*3+3)&~size_t(3);
        const size_t imageSize=stride*bitmapHeight;
        const auto memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(BITMAPINFOHEADER)+imageSize);
        if(!memory)return;
        auto* header=static_cast<BITMAPINFOHEADER*>(GlobalLock(memory));
        if(!header){GlobalFree(memory);return;}
        header->biSize=sizeof(*header);header->biWidth=static_cast<LONG>(bitmapWidth);header->biHeight=static_cast<LONG>(bitmapHeight);
        header->biPlanes=1;header->biBitCount=24;header->biCompression=BI_RGB;header->biSizeImage=static_cast<DWORD>(imageSize);
        auto* target=reinterpret_cast<unsigned char*>(header+1);
        for(UINT y=0;y<bitmapHeight;++y)for(UINT x=0;x<bitmapWidth;++x){
            const auto* source=pixels.data()+(static_cast<size_t>(bitmapHeight-1-y)*bitmapWidth+x)*4;
            auto* pixel=target+static_cast<size_t>(y)*stride+static_cast<size_t>(x)*3;
            pixel[0]=source[0];pixel[1]=source[1];pixel[2]=source[2];
        }
        GlobalUnlock(memory);
        if(!OpenClipboard(window)){GlobalFree(memory);return;}
        EmptyClipboard();
        if(SetClipboardData(CF_DIB,memory)){qrCopied=true;copied=false;}else GlobalFree(memory);
        CloseClipboard();invalidateCopyHint();
    }
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM wp,LPARAM lp) {
        auto* p=reinterpret_cast<Panel*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);p->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(!p)return DefWindowProcW(w,m,wp,lp);
        switch(m) {
        case WM_ERASEBKGND:{RECT client{};GetClientRect(w,&client);
            Panel::fill(reinterpret_cast<HDC>(wp),client,theme::kSurface);return 1;}
        case WM_PAINT:p->paint();return 0;
        case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE)DestroyWindow(w);return 0;
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE){DestroyWindow(w);return 0;}
            if(p->laptopMenu&&(wp==VK_TAB||wp==VK_UP||wp==VK_DOWN)){const bool back=wp==VK_UP||(wp==VK_TAB&&(GetKeyState(VK_SHIFT)&0x8000));p->setFocusItem(ui::nextLaptopMenuAction(p->focus,back));return 0;}
            if(p->laptopMenu&&(wp==VK_HOME||wp==VK_END||wp==VK_PRIOR||wp==VK_NEXT)){
                int row=ui::laptopMenuRow(p->focus);const int last=static_cast<int>(ui::kLaptopMenuOrder.size())-1;
                if(wp==VK_HOME)row=0;else if(wp==VK_END)row=last;
                else row=std::clamp(std::max(0,row)+(wp==VK_NEXT?1:-1)*std::max(1,p->modelScroll.viewport()/p->modelGeometry.rowHeight),0,last);
                p->setFocusItem(ui::kLaptopMenuOrder[row]);return 0;}
            if(wp==VK_TAB){constexpr std::array<int,10> order{0,1,2,3,9,6,4,8,5,7};
                const bool back=(GetKeyState(VK_SHIFT)&0x8000)!=0;
                const auto at=std::find(order.begin(),order.end(),p->focus);
                const int position=at==order.end()?(back?0:9):static_cast<int>(at-order.begin());
                p->setFocusItem(order[(position+(back?9:1))%10]);return 0;}
            if(wp==VK_RETURN||wp==VK_SPACE){p->activate(p->focus,true);return 0;}break;
        case WM_MOUSEWHEEL:
            if(p->laptopMenu){if(page::wheel(p->modelScroll,GET_WHEEL_DELTA_WPARAM(wp),p->dpi)){p->hot=-1;InvalidateRect(w,nullptr,FALSE);}return 0;}break;
        case WM_LBUTTONDOWN:
            if(p->laptopMenu&&p->modelScroll.scrollable()&&GET_X_LPARAM(lp)>=p->modelGeometry.width-p->s(12)&&
               GET_Y_LPARAM(lp)>=p->modelGeometry.viewport.top&&GET_Y_LPARAM(lp)<p->modelGeometry.viewport.bottom){
                const auto thumb=p->modelThumb();const int y=GET_Y_LPARAM(lp);
                page::scrollPress(w,p->modelScroll,thumb,y,p->modelDragging,p->modelGrab);
                p->hot=-1;InvalidateRect(w,nullptr,FALSE);return 0;}break;
        case WM_CAPTURECHANGED:if(p->modelDragging){p->modelDragging=false;InvalidateRect(w,nullptr,FALSE);}return 0;
        case WM_MOUSEMOVE: {
            POINT at{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};int hit=-1;
            if(p->laptopMenu&&p->modelDragging){const auto thumb=p->modelThumb();
                page::scrollDrag(p->modelScroll,thumb,p->modelGeometry.viewport.height(),p->dpi,at.y-p->modelGeometry.viewport.top,p->modelGrab);
                InvalidateRect(w,nullptr,FALSE);return 0;}
            const bool scrollHot=p->laptopMenu&&p->modelScroll.scrollable()&&at.x>=p->modelGeometry.width-p->s(12)&&
                at.y>=p->modelGeometry.viewport.top&&at.y<p->modelGeometry.viewport.bottom;
            if(p->modelScrollHot!=scrollHot){p->modelScrollHot=scrollHot;InvalidateRect(w,nullptr,FALSE);}
            for(int i=0;i<(p->laptopMenu?ui::kLaptopMenuBackItem+1:10);++i){auto r=p->hitRect(i);if(PtInRect(&r,at)){hit=i;break;}}
            p->setHot(hit);
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);
            SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(hit>=0?32649:32512)));return 0;
        }
        case WM_MOUSELEAVE:p->setHot(-1);if(p->modelScrollHot){p->modelScrollHot=false;InvalidateRect(w,nullptr,FALSE);}return 0;
        case WM_SETFOCUS:case WM_KILLFOCUS:p->invalidateItem(p->focus);return 0;
        case WM_LBUTTONUP:if(p->modelDragging){p->modelDragging=false;ReleaseCapture();InvalidateRect(w,nullptr,FALSE);return 0;}p->activate(p->hot);return 0;
        case WM_CLOSE:DestroyWindow(w);return 0;
        case WM_NCDESTROY:p->window=nullptr;break;
        }
        return DefWindowProcW(w,m,wp,lp);
    }
};
}
int runPanel(HWND owner,POINT anchor,UINT dpi,int selected,bool laptop) {
    Panel panel;panel.owner=owner;panel.dpi=dpi;panel.laptopMenu=laptop;panel.result=laptop?-1:selected;panel.selected=selected;panel.focus=-1;
    panel.languageSelection=laptop?static_cast<int>(ui::languageIndex()):selected;
    panel.modelSelection=laptop?selected:ui::selectedLaptopIndex();
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&monitor);
    panel.body=createUiFont(ui::FontRole::ClassicBody,dpi);panel.heading=createUiFont(ui::FontRole::ClassicHeading,dpi);panel.detail=createUiFont(ui::FontRole::KeyDetail,dpi);if(!laptop)panel.loadQr();
    panel.measureLaptopMenu(std::max(1,static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-panel.s(16)),
        std::max(1,static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-panel.s(16)));
    if(laptop)panel.ensureModelVisible(selected);
    WNDCLASSW cls{};cls.lpfnWndProc=Panel::procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"MagicControl.LanguagePanel";RegisterClassW(&cls);
    const int width=laptop?panel.modelGeometry.width:panel.languageGeometry.width,height=laptop?panel.modelGeometry.height:panel.languageGeometry.height;
    const int x=std::clamp(static_cast<int>(anchor.x),static_cast<int>(monitor.rcWork.left),std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width));
    const int y=std::clamp(static_cast<int>(anchor.y),static_cast<int>(monitor.rcWork.top),std::max(static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height));
    const HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"MagicControl",WS_POPUP|WS_BORDER,x,y,width,height,owner,nullptr,cls.hInstance,&panel);
    if(window){if(page::presentPopup(window))SetFocus(window);else if(IsWindow(window))DestroyWindow(window);MSG message{};
        while(panel.window){const BOOL got=GetMessageW(&message,nullptr,0,0);if(got<=0){if(got==0)PostQuitMessage(static_cast<int>(message.wParam));break;}TranslateMessage(&message);DispatchMessageW(&message);}
        if(panel.window)DestroyWindow(panel.window);
    }
    DeleteObject(panel.body);DeleteObject(panel.heading);DeleteObject(panel.detail);return panel.result;
}
int showLanguagePopup(HWND owner,POINT anchor,UINT dpi,int selected){return runPanel(owner,anchor,dpi,selected,false);}
int showLaptopPopup(HWND owner,POINT anchor,UINT dpi,int selected){const int result=runPanel(owner,anchor,dpi,selected,true);return result>=100?result-100:-1;}
}
