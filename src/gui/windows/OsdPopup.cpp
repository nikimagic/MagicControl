#include "LocalizationWindows.h"
#include "OsdPopup.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include "../../platform/windows/WindowEventLoopWindows.h"
#include <utility>
#include <shellscalingapi.h>

#include "Layout.h"
#include "../common/OsdIcon.h"

namespace mc::gui {
namespace {
constexpr UINT kCommands=WM_APP+1;
constexpr UINT_PTR kAnimation=77;
constexpr wchar_t kClassName[]=L"MagicControl.Osd";
std::wstring wide(const std::string& value) {
    const int count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(std::max(0,count)),L'\0');
    if(count)MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count);
    return result;
}
std::string utf8(const std::wstring& value) {
    const int count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(static_cast<std::size_t>(std::max(0,count)),'\0');
    if(count)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);
    return result;
}
void fill(HDC dc,RECT r,COLORREF color) {
    const auto old=SelectObject(dc,GetStockObject(DC_BRUSH));
    const auto previous=SetDCBrushColor(dc,color); FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc,previous);SelectObject(dc,old);
}
}

struct OsdPopup::Impl {
    enum class Kind {Show,Begin,Complete,Hide,Enable};
    struct Command {Kind kind;osd::Message message;std::uint64_t token{};bool enabled{};HWND anchor{};};
    std::mutex gate;
    std::deque<Command> commands;
    std::map<std::uint64_t,std::string> settingsOperations;
    windows::WindowEventLoop loop;
    std::atomic<std::uint64_t> nextToken{};
    std::function<void(std::string)> log;
    osd::State state;
    bool enabled{true};
    UINT dpi{96};unsigned fontLanguage{~0u};bool embeddedFont{};
    HMONITOR monitor{};
    HWND activeAnchor{};
    HFONT titleFont{},detailFont{};
    HDC buffer{};HBITMAP bitmap{};HGDIOBJ previousBitmap{};
    int bufferWidth{},bufferHeight{};
    std::map<std::string,std::uint64_t> lastLog;
    std::uint64_t paintedRevision{};
    BYTE alpha{242};

    ~Impl(){loop.shutdown();}
    int s(int value)const{return MulDiv(value,static_cast<int>(dpi),96);}
    bool start(HINSTANCE instance) {
        return loop.start([this,instance]{
            WNDCLASSW klass{};klass.hInstance=instance;klass.lpfnWndProc=procedure;klass.lpszClassName=kClassName;
            RegisterClassW(&klass);
            return CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_LAYERED,
                kClassName,L"MagicControl OSD",WS_POPUP,0,0,osd::kWidth,osd::kHeight,nullptr,nullptr,instance,this);
        },[this](HWND target){
            SetLayeredWindowAttributes(target,0,alpha,LWA_ALPHA);
            if(log)log("OSD: core actor Windows UI loop ready; no EC access");
        },[this]{
            clearBuffer();for(HFONT font:{titleFont,detailFont})if(font)DeleteObject(font);
        });
    }
    void post(Command command) {
        if(loop.stopping())return;
        HWND target=loop.window();if(!target)return;
        {
            std::lock_guard lock(gate);
            if(command.kind==Kind::Show)
                std::erase_if(commands,[&](const Command& item){return item.kind==Kind::Show&&item.message.key==command.message.key;});
            commands.push_back(std::move(command));
        }
        PostMessageW(target,kCommands,0,0);
    }
    void clearBuffer() {
        if(buffer&&previousBitmap)SelectObject(buffer,previousBitmap);
        if(bitmap)DeleteObject(bitmap);if(buffer)DeleteDC(buffer);
        buffer=nullptr;bitmap=nullptr;previousBitmap=nullptr;bufferWidth=bufferHeight=0;
    }
    void fonts() {
        fontLanguage=ui::languageIndex();embeddedFont=embeddedUiFontEnabled();
        for(HFONT font:{titleFont,detailFont})if(font)DeleteObject(font);
        // Keep the existing 35-DIP icon; text uses the same role/family policy
        // as all other windows, including the selected Chinese variant.
        titleFont=createUiFont(ui::FontRole::OsdTitle,dpi);
        detailFont=createUiFont(ui::FontRole::OsdDetail,dpi);
    }
    bool place() {
        HWND target=loop.window();
        const bool anchored=IsWindow(activeAnchor)!=FALSE;
        const HWND reference=anchored?activeAnchor:GetForegroundWindow();
        const HMONITOR nextMonitor=MonitorFromWindow(reference,MONITOR_DEFAULTTOPRIMARY);
        UINT x=anchored?GetDpiForWindow(reference):0,y{};
        if(!x&&FAILED(GetDpiForMonitor(nextMonitor,MDT_EFFECTIVE_DPI,&x,&y)))x=96;
        x=std::max<UINT>(96,x);
        const bool sameMonitor=monitor==nextMonitor,sameDpi=dpi==x;
        const bool fontsChanged=!sameDpi||!titleFont||fontLanguage!=ui::languageIndex()||embeddedFont!=embeddedUiFontEnabled();
        monitor=nextMonitor;dpi=x;if(fontsChanged)fonts();
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);
        const auto area=info.rcWork;
        const int width=std::max(200,std::min(s(osd::kWidth),static_cast<int>(area.right-area.left)-24));
        const int height=std::max(100,std::min(s(osd::kHeight),static_cast<int>(area.bottom-area.top)-24));
        const int left=area.left+std::min(s(osd::kLeft),std::max(0,static_cast<int>(area.right-area.left)-width));
        const int top=area.top+std::max(0,static_cast<int>((area.bottom-area.top)*.37)-height/2);
        RECT bounds{},client{};GetWindowRect(target,&bounds);GetClientRect(target,&client);
        if(IsWindowVisible(target)&&sameMonitor&&sameDpi&&bounds.left==left&&bounds.top==top&&
            client.right-client.left==width&&client.bottom-client.top==height)return fontsChanged;
        SetWindowPos(target,HWND_TOPMOST,left,top,width,height,SWP_NOACTIVATE);
        const HRGN region=CreateRoundRectRgn(0,0,width+1,height+1,s(osd::kRadius*2),s(osd::kRadius*2));
        if(region&&!SetWindowRgn(target,region,FALSE))DeleteObject(region);
        return true;
    }
    void drain() {
        std::deque<Command> pending;
        {std::lock_guard lock(gate);pending.swap(commands);}
        const auto at=osd::now();
        for(auto& item:pending) {
            if(item.kind==Kind::Enable){enabled=item.enabled;if(!enabled)state.dismiss();}
            else if(item.kind==Kind::Hide)state.dismiss();
            else if(enabled) {
                if(item.kind==Kind::Show){const auto revision=state.revision;state.present(std::move(item.message),at);if(state.revision!=revision)activeAnchor=item.anchor;}
                else if(item.kind==Kind::Begin){const auto revision=state.revision;state.begin(item.token,std::move(item.message),at);if(state.revision!=revision)activeAnchor=item.anchor;}
                else state.complete(item.token,std::move(item.message),at);
            }
        }
        update(at,true);
    }
    void update(std::uint64_t at,bool changed=false) {
        state.tick(at);HWND target=loop.window();
        if(!state.visible){KillTimer(target,kAnimation);ShowWindow(target,SW_HIDE);return;}
        const bool placementChanged=place();
        if(log&&(changed||placementChanged)){RECT r{};GetClientRect(target,&r);
            log(std::format("OSD GEOMETRY: key={} dpi={} window-dpi={} client={}x{} anchor={}",state.message.key,dpi,GetDpiForWindow(target),r.right-r.left,r.bottom-r.top,IsWindow(activeAnchor)?"owner":"foreground"));}
        const BYTE next=static_cast<BYTE>(std::clamp(state.opacity(at)*255.0,1.0,242.0));
        if(next!=alpha){alpha=next;SetLayeredWindowAttributes(target,0,alpha,LWA_ALPHA);}
        if(!IsWindowVisible(target))ShowWindow(target,SW_SHOWNOACTIVATE);
        if(changed||placementChanged||paintedRevision!=state.revision)InvalidateRect(target,nullptr,FALSE);
        else if(state.animating(at)) {
            RECT rect{};GetClientRect(target,&rect);rect.top=std::max(0L,rect.bottom-s(36));
            InvalidateRect(target,&rect,FALSE);
        }
        UpdateWindow(target);
        SetTimer(target,kAnimation,state.animating(at)?15:33,nullptr);
    }
    void label(HDC dc,const std::wstring& text,RECT rect,HFONT font,COLORREF color,UINT flags) {
        const auto previous=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
        ::mc::gui::localizedDrawText(dc,text.c_str(),-1,&rect,flags|DT_NOPREFIX);SelectObject(dc,previous);
    }
    void paint(HWND target) {
        PAINTSTRUCT ps{};HDC dest=BeginPaint(target,&ps);RECT client{};GetClientRect(target,&client);
        if(!buffer||bufferWidth!=client.right||bufferHeight!=client.bottom){
            clearBuffer();buffer=CreateCompatibleDC(dest);bitmap=CreateCompatibleBitmap(dest,client.right,client.bottom);
            if(buffer&&bitmap){previousBitmap=SelectObject(buffer,bitmap);bufferWidth=client.right;bufferHeight=client.bottom;}
            else clearBuffer();
        }
        HDC dc=buffer?buffer:dest;
        const int saved=SaveDC(dc);
        IntersectClipRect(dc,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right,ps.rcPaint.bottom);
        const auto& message=state.message;const auto at=osd::now();
        const COLORREF accent=message.error?osd::kError:osd::kAccent;
        fill(dc,client,osd::kBackground);
        HPEN pen=CreatePen(PS_SOLID,std::max(1,s(1)),osd::kBorder);
        const auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        RoundRect(dc,1,1,client.right-2,client.bottom-2,s(osd::kRadius*2),s(osd::kRadius*2));
        SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);
        const int pad=s(osd::kPadding),left=pad+s(osd::kIconWidth+osd::kIconGap),right=client.right-pad;
        {const int side=s(35),x=pad+(s(osd::kIconWidth)-side)/2,y=pad+(s(osd::kIconHeight)-side)/2;
            const auto pen=CreatePen(PS_SOLID,std::max(1,s(1)),accent);const auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
            const auto px=[&](int n){return x+MulDiv(n,side,32);};const auto py=[&](int n){return y+MulDiv(n,side,32);};
            osd::drawIcon(message.icon,[&](int a,int b,int c,int d){MoveToEx(dc,px(a),py(b),nullptr);LineTo(dc,px(c),py(d));},
                [&](int a,int b,int c,int d){Rectangle(dc,px(a),py(b),px(c),py(d));},[&](int a,int b,int c,int d){Ellipse(dc,px(a),py(b),px(c),py(d));});
            SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);}
        label(dc,wide(message.title),{left,pad,right,pad+s(osd::kTitleHeight)},titleFont,osd::kText,DT_WORDBREAK|DT_END_ELLIPSIS);
        label(dc,wide(message.detail),{left,pad+s(osd::kDetailTop),right,pad+s(osd::kDetailTop+osd::kDetailHeight)},detailFont,osd::kDetail,DT_WORDBREAK|DT_END_ELLIPSIS);
        if(message.percent) {
            RECT track{pad,client.bottom-s(osd::kBarBottom),client.right-pad,client.bottom-s(osd::kBarBottom)+std::max(3,s(osd::kBarHeight))};
            fill(dc,track,osd::kTrack);
            const int x=track.left+static_cast<int>((track.right-track.left)*std::clamp(state.percent(at),0.0,100.0)/100.0);
            RECT filled=track;filled.right=x;fill(dc,filled,accent);
            if(message.key=="keyboard") {
                const int y=(track.top+track.bottom)/2;
                const double strength=state.activity(at)*.25;
                const auto channel=[&](unsigned shift){return static_cast<BYTE>(
                    ((osd::kBackground>>shift)&255u)*(1-strength)+((accent>>shift)&255u)*strength);};
                const COLORREF halo=rgb(channel(0),channel(8),channel(16));
                const auto ellipse=[&](int radius,COLORREF color){
                    const auto brush=CreateSolidBrush(color);const auto old=SelectObject(dc,brush);
                    const auto p=SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,x-radius,y-radius,x+radius+1,y+radius+1);
                    SelectObject(dc,p);SelectObject(dc,old);DeleteObject(brush);
                };
                if(strength>0)ellipse(s(7),halo);ellipse(s(3),accent);
            }
        }
        RestoreDC(dc,saved);
        if(buffer)BitBlt(dest,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,
            ps.rcPaint.bottom-ps.rcPaint.top,buffer,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);
        EndPaint(target,&ps);
        const bool firstPaint=paintedRevision!=state.revision;
        paintedRevision=state.revision;
        const auto finished=osd::now();
        if(firstPaint&&message.receivedAt&&finished-lastLog[message.key]>=1000){
            lastLog[message.key]=finished;
            if(log)log(std::format("OSD DELIVERY: key={} event-to-paint-ms={} thread=dedicated EC-reads=0 EC-writes=0",
                message.key,finished-message.receivedAt));
        }
    }
    static LRESULT CALLBACK procedure(HWND target,UINT message,WPARAM wParam,LPARAM lParam) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(target,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);SetWindowLongPtrW(target,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(target,message,wParam,lParam);
        switch(message){
        case WM_NCHITTEST:return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
        case WM_ERASEBKGND:return 1;
        case kCommands:self->drain();return 0;
        case WM_TIMER:if(wParam==kAnimation){self->update(osd::now());return 0;}break;
        case WM_PAINT:self->paint(target);return 0;
        case WM_DPICHANGED:self->monitor=nullptr;PostMessageW(target,kCommands,0,0);return 0;
        case WM_CLOSE:DestroyWindow(target);return 0;
        case WM_DESTROY:self->loop.windowDestroyed();return 0;
        }
        return DefWindowProcW(target,message,wParam,lParam);
    }
};

OsdPopup::OsdPopup()=default;
OsdPopup::~OsdPopup()=default;
bool OsdPopup::create(HINSTANCE instance,std::function<void(std::string)> log) {
    impl_=std::make_unique<Impl>();impl_->log=std::move(log);return impl_->start(instance);
}
void OsdPopup::show(osd::Message message,HWND anchor){
    if(!message.receivedAt&&!message.issuedAt)message.issuedAt=osd::now();
    if(impl_)impl_->post({Impl::Kind::Show,std::move(message),0,false,anchor});
}
void OsdPopup::show(const std::wstring& title,const std::wstring& detail){show({utf8(title),utf8(title),utf8(detail)});}
std::uint64_t OsdPopup::beginOperation(osd::Message message){
    if(!impl_)return 0;
    if(!message.receivedAt&&!message.issuedAt)message.issuedAt=osd::now();
    const auto token=++impl_->nextToken;
    if(!message.receivedAt){std::lock_guard lock(impl_->gate);impl_->settingsOperations[token]=message.key;}
    impl_->post({Impl::Kind::Begin,std::move(message),token});return token;
}
void OsdPopup::completeOperation(std::uint64_t token,osd::Message message){
    if(impl_&&token){
        {std::lock_guard lock(impl_->gate);impl_->settingsOperations.erase(token);}
        impl_->post({Impl::Kind::Complete,std::move(message),token});
    }
}
bool OsdPopup::hasPendingCommand(const std::string& key) const {
    if(!impl_)return false;
    std::lock_guard lock(impl_->gate);
    return std::any_of(impl_->settingsOperations.begin(),impl_->settingsOperations.end(),[&](const auto& entry){return entry.second==key;});
}
void OsdPopup::hide(){if(impl_)impl_->post({Impl::Kind::Hide,{}});}
void OsdPopup::setEnabled(bool enabled){if(impl_)impl_->post({Impl::Kind::Enable,{},0,enabled});}
}
