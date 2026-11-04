#pragma once
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>
#include "../common/Theme.h"
#include "PageMechanics.h"
namespace mc::gui {
struct DiscreteSliderPaint {page::Buffer buffer;int maximum{};bool hot{},pressed{};};
inline constexpr UINT_PTR kDiscreteSliderSubclass=0x4D43534C;
inline int discreteSliderValue(HWND slider){return static_cast<int>(SendMessageW(slider,TBM_GETPOS,0,0));}
inline std::pair<int,int> discreteSliderLocalEnds(HWND slider){
    RECT channel{},thumb{};SendMessageW(slider,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
    SendMessageW(slider,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
    const int half=std::max(0,static_cast<int>(thumb.right-thumb.left)/2);
    return {static_cast<int>(channel.left)+half,static_cast<int>(channel.right)-half};
}
inline std::pair<int,int> discreteSliderEnds(HWND slider){
    const auto local=discreteSliderLocalEnds(slider);POINT points[2]{{local.first,0},{local.second,0}};
    MapWindowPoints(slider,GetParent(slider),points,2);return {points[0].x,points[1].x};
}
inline void paintDiscreteSlider(HWND slider,HDC dc,const DiscreteSliderPaint& state){
    RECT client{},thumb{};GetClientRect(slider,&client);
    SendMessageW(slider,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
    const auto ends=discreteSliderLocalEnds(slider);
    const int dip=std::max(1,MulDiv(1,static_cast<int>(GetDpiForWindow(slider)),96));
    const bool enabled=IsWindowEnabled(slider)!=FALSE;
    const auto fill=[&](RECT rect,COLORREF color){HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);};
    fill(client,theme::kSurface);
    const int middle=(thumb.top+thumb.bottom)/2;
    RECT track{ends.first,middle-dip,ends.second,middle+dip};fill(track,theme::kNumericBorder);
    track.right=std::clamp(static_cast<int>((thumb.left+thumb.right)/2),ends.first,ends.second);
    fill(track,enabled?theme::kAccent:theme::kTextMuted);
    for(int level=0;state.maximum>0&&level<=state.maximum;++level){
        const int x=ends.first+(ends.second-ends.first)*level/state.maximum;
        fill({x,middle-3*dip,x+dip,middle+4*dip},enabled?theme::kTextSecondary:theme::kTextMuted);
    }
    fill(thumb,!enabled?theme::kTextMuted:state.pressed?theme::kAccentPressed:
        state.hot||GetFocus()==slider?theme::kAccentHover:theme::kAccent);
}
inline LRESULT CALLBACK discreteSliderProcedure(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    auto& state=*reinterpret_cast<DiscreteSliderPaint*>(data);
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,discreteSliderProcedure,kDiscreteSliderSubclass);const auto result=DefSubclassProc(window,message,wp,lp);delete &state;return result;}
    if(message==WM_ERASEBKGND)return 1; // Full client is painted once in the buffer.
    if(message==WM_PAINT){
        PAINTSTRUCT paint{};HDC target=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);
        if(client.right>0&&client.bottom>0){HDC dc=state.buffer.ensure(target,client.right,client.bottom)?state.buffer.dc:target;
            paintDiscreteSlider(window,dc,state);if(dc!=target)BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);}
        EndPaint(window,&paint);return 0;
    }
    if(message==WM_PRINT||message==WM_PRINTCLIENT){paintDiscreteSlider(window,reinterpret_cast<HDC>(wp),state);return 0;}
    if(message==TBM_SETPOS&&discreteSliderValue(window)==static_cast<int>(lp))return 0;
    if(message==WM_MOUSEMOVE){RECT thumb{};SendMessageW(window,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};state.hot=PtInRect(&thumb,point)!=FALSE;TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);}
    if(message==WM_MOUSELEAVE)state.hot=false;
    if(message==WM_ENABLE&&!wp)state.pressed=false;
    if(message==WM_LBUTTONDOWN)state.pressed=true;
    if(message==WM_LBUTTONUP||message==WM_CANCELMODE||message==WM_CAPTURECHANGED)state.pressed=false;
    const auto result=DefSubclassProc(window,message,wp,lp);
    if(message==WM_SIZE||message==WM_DPICHANGED_AFTERPARENT){
        RECT client{};GetClientRect(window,&client);
        SendMessageW(window,TBM_SETTHUMBLENGTH,std::max(2,std::min(MulDiv(18,GetDpiForWindow(window),96),static_cast<int>(client.bottom)-MulDiv(4,GetDpiForWindow(window),96))),0);
    }
    switch(message){case WM_MOUSEMOVE:case WM_MOUSELEAVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_CAPTURECHANGED:case WM_ENABLE:case WM_SETFOCUS:case WM_KILLFOCUS:case WM_SIZE:case WM_DPICHANGED_AFTERPARENT:case TBM_SETPOS:case TBM_SETRANGEMIN:case TBM_SETRANGEMAX:
        InvalidateRect(window,nullptr,FALSE);break;}
    return result;
}
// Native input/keyboard/accessibility, one buffered skin for every consumer/model.
inline HWND createDiscreteSlider(HWND parent,int id,int maximum){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("Cannot initialize slider");
    HWND slider=CreateWindowExW(0,TRACKBAR_CLASSW,L"Яркость",WS_CHILD|WS_VISIBLE|WS_TABSTOP|TBS_HORZ|TBS_NOTICKS|TBS_FIXEDLENGTH,
        0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(!slider)throw std::runtime_error("Cannot create slider");
    auto* state=new(std::nothrow) DiscreteSliderPaint{};if(state)state->maximum=maximum;
    if(!state||!SetWindowSubclass(slider,discreteSliderProcedure,kDiscreteSliderSubclass,reinterpret_cast<DWORD_PTR>(state))){delete state;DestroyWindow(slider);throw std::runtime_error("Cannot style slider");}
    SendMessageW(slider,TBM_SETRANGEMIN,FALSE,0);SendMessageW(slider,TBM_SETRANGEMAX,FALSE,std::max(1,maximum));
    SendMessageW(slider,TBM_SETLINESIZE,0,1);SendMessageW(slider,TBM_SETPAGESIZE,0,1);return slider;
}
}
