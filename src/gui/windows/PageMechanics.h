#pragma once
#include "LocalizationWindows.h"
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <vector>
#include <utility>
#include "Geometry.h"
#include "../../platform/windows/TextWindows.h"
#include "MagicControl/ScrollModel.h"
#include "../common/Theme.h"

// Shared Win32 page mechanics. Content/pages supply only their data and layout.
// Buffer and visible-child clipping are the existing FANs implementation.
namespace mc::gui::page {
// Native tooltips for painted page regions; pages provide only bounds/text.
class RegionTooltips {
    struct Entry { RECT bounds; std::wstring text; };
    HWND window_{},owner_{};HFONT font_{};
    std::vector<Entry> entries_;
public:
    ~RegionTooltips(){if(IsWindow(window_))DestroyWindow(window_);}
    void update(HWND owner,const std::vector<ui::TooltipRegion>& regions,HFONT font=nullptr){
        if(!IsWindow(window_)){
            INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
            window_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,
                CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(!window_)return;
            owner_=owner;
            SendMessageW(window_,TTM_SETMAXTIPWIDTH,0,MulDiv(440,GetDpiForWindow(owner),96));
        }
        if(font&&font!=font_){font_=font;SendMessageW(window_,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);}
        std::vector<Entry> next;
        for(const auto& region:regions)next.push_back({{region.bounds.left,region.bounds.top,region.bounds.right,region.bounds.bottom},mc::windows::encoding::wide(region.text)});
        bool same=next.size()==entries_.size();
        for(std::size_t i=0;same&&i<next.size();++i)same=EqualRect(&next[i].bounds,&entries_[i].bounds)&&next[i].text==entries_[i].text;
        if(same)return; // Polling/paint must not restart the hover delay.
        for(std::size_t i=0;i<entries_.size();++i){TOOLINFOW tool{sizeof(tool)};tool.hwnd=owner_;tool.uId=i+1;SendMessageW(window_,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
        entries_=std::move(next);
        for(std::size_t i=0;i<entries_.size();++i){
            TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_SUBCLASS;tool.hwnd=owner_;tool.uId=i+1;
            tool.rect=entries_[i].bounds;tool.lpszText=entries_[i].text.data();
            SendMessageW(window_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
        }
    }
};
struct Buffer {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    int width{};
    int height{};

    ~Buffer() { clear(); }
    void clear() {
        if (dc && previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        dc = nullptr;
        bitmap = nullptr;
        previous = nullptr;
        width = height = 0;
    }
    bool ensure(HDC source, int newWidth, int newHeight) {
        if (dc && width == newWidth && height == newHeight) return true;
        clear();
        dc = CreateCompatibleDC(source);
        if (dc) bitmap = CreateCompatibleBitmap(source, newWidth, newHeight);
        if (!bitmap) {
            clear();
            return false;
        }
        previous = SelectObject(dc, bitmap);
        width = newWidth;
        height = newHeight;
        return true;
    }
};

struct ChildPaintClipContext {
    HWND parent{};
    HRGN region{};
    RECT dirty{};
};

inline BOOL CALLBACK collectChildPaintClip(HWND child, LPARAM parameter) {
    auto& context = *reinterpret_cast<ChildPaintClipContext*>(parameter);
    if (!IsWindowVisible(child)) return TRUE;

    RECT screen{};
    if (!GetWindowRect(child, &screen)) return TRUE;
    POINT corners[2]{{screen.left, screen.top}, {screen.right, screen.bottom}};
    MapWindowPoints(HWND_DESKTOP, context.parent, corners, 2);

    RECT bounds{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
    RECT dirtyBounds{};
    if (!IntersectRect(&dirtyBounds, &bounds, &context.dirty)) return TRUE;
    RECT client{};
    GetClientRect(context.parent, &client);
    RECT visibleBounds{};
    if (!IntersectRect(&visibleBounds, &dirtyBounds, &client)) return TRUE;

    HRGN childRegion = CreateRectRgnIndirect(&visibleBounds);
    if (childRegion) {
        CombineRgn(context.region, context.region, childRegion, RGN_OR);
        DeleteObject(childRegion);
    }
    return TRUE;
}

inline HRGN createChildPaintClip(HWND parent, const RECT& dirty) {
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (!region) return nullptr;
    ChildPaintClipContext context{parent, region, dirty};
    EnumChildWindows(parent, collectChildPaintClip, reinterpret_cast<LPARAM>(&context));
    return region;
}

class Paint {
    HWND window_{};PAINTSTRUCT paint_{};RECT client_{};HDC target_{},dc_{};
    HRGN children_{};int saved_{};bool finished_{};
public:
    Paint(HWND window,Buffer& buffer):window_(window){
        target_=BeginPaint(window_,&paint_);GetClientRect(window_,&client_);
        if(client_.right<1||client_.bottom<1)return;
        dc_=buffer.ensure(target_,client_.right,client_.bottom)?buffer.dc:target_;
        children_=createChildPaintClip(window_,paint_.rcPaint);saved_=SaveDC(dc_);
        if(children_)ExtSelectClipRgn(dc_,children_,RGN_DIFF);
        IntersectClipRect(dc_,paint_.rcPaint.left,paint_.rcPaint.top,paint_.rcPaint.right,paint_.rcPaint.bottom);
    }
    ~Paint(){finish();}
    Paint(const Paint&)=delete;Paint& operator=(const Paint&)=delete;
    bool valid()const{return dc_!=nullptr;}
    HDC dc()const{return dc_;}
    const RECT& client()const{return client_;}
    const PAINTSTRUCT& info()const{return paint_;}
    void finish(){
        if(finished_)return;finished_=true;
        if(dc_){if(saved_)RestoreDC(dc_,saved_);
            if(dc_!=target_){const int saved=SaveDC(target_);if(children_)ExtSelectClipRgn(target_,children_,RGN_DIFF);
                BitBlt(target_,paint_.rcPaint.left,paint_.rcPaint.top,paint_.rcPaint.right-paint_.rcPaint.left,
                    paint_.rcPaint.bottom-paint_.rcPaint.top,dc_,paint_.rcPaint.left,paint_.rcPaint.top,SRCCOPY);
                if(saved)RestoreDC(target_,saved);}}
        if(children_)DeleteObject(children_);EndPaint(window_,&paint_);
    }
};
// Layout queues invalidation instead of exposing each native child mid-layout.
inline void place(HWND window, int x, int y, int width, int height, bool raise = false) {
    if (!window) return;
    RECT bounds{}; GetWindowRect(window, &bounds);
    MapWindowPoints(HWND_DESKTOP, GetParent(window), reinterpret_cast<POINT*>(&bounds), 2);
    const RECT next{x, y, x + std::max(1, width), y + std::max(1, height)};
    if (EqualRect(&bounds, &next)) {
        if (raise) SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
        return;
    }
    SetWindowPos(window, raise ? HWND_TOP : nullptr, x, y, next.right-next.left, next.bottom-next.top,
        SWP_NOACTIVATE | SWP_NOREDRAW | (raise ? 0 : SWP_NOZORDER));
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_NOERASE);
}
inline void present(HWND window) {
    applyWindowLanguage(window);
    // Call after layout and state synchronization. Finish the full visible tree
    // before the outgoing tab is hidden; the parent never exposes a partial tab.
    ShowWindow(window, SW_SHOWNOACTIVATE);
    RedrawWindow(window, nullptr, nullptr,
        RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
}
inline bool presentPopup(HWND window) {
    if (!window) return false;
    // A new top-level HWND has no painted surface yet. Compose its complete
    // first frame while cloaked; publishing it before WM_PAINT can expose a
    // light placeholder, especially on the first opening in a process.
    // Reuse the normal WM_PAINT / Buffer path, not a second popup renderer.
    const BOOL enabled = TRUE;
    DwmSetWindowAttribute(window, DWMWA_TRANSITIONS_FORCEDISABLED, &enabled, sizeof(enabled));
    const bool cloaked = SUCCEEDED(DwmSetWindowAttribute(window, DWMWA_CLOAK, &enabled, sizeof(enabled)));
    ShowWindow(window, SW_SHOWNOACTIVATE);
    if (IsWindow(window)) {
        RedrawWindow(window, nullptr, nullptr,
            RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
        GdiFlush();
    }
    // When composition/cloaking is unavailable, the same synchronous paint
    // still runs. Popup WM_ERASEBKGND supplies its themed fallback surface.
    const BOOL disabled = FALSE;
    if (cloaked && IsWindow(window) &&
        FAILED(DwmSetWindowAttribute(window, DWMWA_CLOAK, &disabled, sizeof(disabled))))
        return false; // Caller destroys the HWND rather than running an invisible menu.
    if (!IsWindow(window)) return false;
    ShowWindow(window, SW_SHOW);
    return IsWindow(window) != FALSE;
}
inline int pixels(UINT dpi,int value){return MulDiv(value,static_cast<int>(dpi),96);}
// OWNER CONTRACT,24.10.2026: preserve the working scroll mechanism and card
// edges. Never move/narrow cards to reserve this lane; never add a second gutter.
// Fixes must follow the existing Git reference, not redesign layout or scrolling.
// ONE project scroll mechanism. Consumers keep independent positions and only
// adapt viewport coordinates/native text units. Do not copy these formulas.
// exactWidth retains LicenseView's original whole-width rounding; other
// consumers retain their original symmetric half-width rectangle. Collapsing
// these cases shifts/widens the existing thumb by a pixel at several DPIs.
inline RECT scrollThumb(int width,int height,const ScrollModel& scroll,UINT dpi,bool hot,bool dragging,bool fixedLane=false,bool exactWidth=false){
    const int track=std::max(1,height-pixels(dpi,8));
    const int length=std::min(track,std::max(pixels(dpi,28),static_cast<int>(static_cast<long long>(track)*scroll.viewport()/std::max(1,scroll.extent()))));
    const int top=pixels(dpi,4)+static_cast<int>(static_cast<long long>(track-length)*scroll.position()/std::max(1,scroll.maximumPosition()));
    const int thickness=pixels(dpi,hot||dragging?7:5),center=fixedLane?width/2:width-pixels(dpi,12)/2;
    if(exactWidth){const int left=fixedLane?(width-thickness)/2:center-thickness/2;return {left,top,left+thickness,top+length};}
    return {center-thickness/2,top,center-thickness/2+thickness,top+length};
}
inline void drawThumb(HDC dc,const RECT& rect,bool hot,bool dragging){
    const auto brush=CreateSolidBrush(dragging?theme::kScrollThumbDragging:hot?theme::kScrollThumbHot:theme::kScrollThumb);
    const auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,rect.right-rect.left,rect.right-rect.left);
    SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(brush);
}
// Shared pointer mechanics, extracted from INFO's established scrollbar.
// Callers supply the existing viewport-relative thumb; no geometry/gutter policy.
inline void scrollPress(HWND window,ScrollModel& scroll,const RECT& thumb,int y,bool& dragging,int& grabOffset){
    if(y>=thumb.top&&y<thumb.bottom){dragging=true;grabOffset=y-thumb.top;SetCapture(window);}
    else scroll.setPosition(scroll.position()+scroll.pageStep(y>=thumb.bottom));
}
inline void scrollDrag(ScrollModel& scroll,const RECT& thumb,int height,UINT dpi,int y,int grabOffset){
    const int travel=height-pixels(dpi,8)-(thumb.bottom-thumb.top);
    scroll.setPosition(static_cast<int>(static_cast<long long>(y-pixels(dpi,4)-grabOffset)*scroll.maximumPosition()/std::max(1,travel)));
}
inline int wheel(ScrollModel& scroll,int delta,UINT dpi,int lineUnits=0,int pageOverlapUnits=-1){
    if(!scroll.scrollable())return 0;
    UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
    // Pages use pixels and the model's original overlap; native EDIT uses
    // logical lines and its original one-line page overlap. Never feed pixel
    // distances into the license's line-based model.
    const int step=lines==WHEEL_PAGESCROLL?
        (pageOverlapUnits<0?scroll.pageStep(true):std::max(1,scroll.viewport()-pageOverlapUnits)):
        static_cast<int>(std::min<UINT>(lines,100))*(lineUnits>0?lineUnits:pixels(dpi,24));
    if(step<=0)return 0;scroll.accumulateWheel(delta,step);return scroll.applyPendingWheel(step);
}
// Some OS adapters own positioning (EDIT first visible line, or a child HWND).
// Let the shared math calculate the request, then apply it with the adapter's
// original position still present, so its pixel/line delta is not lost.
template<class Change,class Apply>
inline int scrollRequest(ScrollModel& scroll,Change change,Apply apply){
    const int before=scroll.position();change();const int requested=scroll.position();
    scroll.setPosition(before);
    if(requested!=before)apply(requested);
    return scroll.position()-before;
}
template<class Apply>
inline void scrollPress(HWND window,ScrollModel& scroll,const RECT& thumb,int y,bool& dragging,int& grabOffset,Apply apply){
    scrollRequest(scroll,[&]{scrollPress(window,scroll,thumb,y,dragging,grabOffset);},apply);
}
template<class Apply>
inline void scrollDrag(ScrollModel& scroll,const RECT& thumb,int height,UINT dpi,int y,int grabOffset,Apply apply){
    scrollRequest(scroll,[&]{scrollDrag(scroll,thumb,height,dpi,y,grabOffset);},apply);
}
template<class Apply>
inline int wheel(ScrollModel& scroll,int delta,UINT dpi,Apply apply,int lineUnits=0,int pageOverlapUnits=-1){
    return scrollRequest(scroll,[&]{wheel(scroll,delta,dpi,lineUnits,pageOverlapUnits);},apply);
}
inline bool hover(HWND window,const ScrollModel& scroll,UINT dpi,int x,bool& hot){
    RECT client{};GetClientRect(window,&client);const bool next=scroll.scrollable()&&x>=client.right-pixels(dpi,12);
    if(next==hot)return false;hot=next;return true;
}
}
