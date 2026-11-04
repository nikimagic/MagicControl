#include "MagicControl/ActionLog.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/ControlAudit.h"
#include "LocalizationWindows.h"
#include "ControlTheme.h"
#include "PageMechanics.h"
#include "DiscreteGpuPage.h"
#include "OsdPopup.h"
#include "../common/OsdMessages.h"
#include "MagicControl/GpuDevice.h"
#include "../../platform/common/DiscreteGpuOperation.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ScrollModel.h"
#include "../common/DiscreteGpuCard.h"
#include "../common/DeviceHeader.h"
#include "Layout.h"
#include "../common/Theme.h"
#include "../../platform/windows/TextWindows.h"
#include "NumericFieldTheme.h"
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
namespace mc::gui {
namespace {constexpr UINT done=WM_APP+187;
void fill(HDC dc,RECT rect,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}}
struct DiscreteGpuPage::Impl : GpuOperations {
    OsdPopup* popup{};std::uint64_t osdToken{};osd::Message osdNotice;
    HWND window{};UINT dpi{96};unsigned fontLanguage{~0u};int width{},height{};bool visible{},syncing{},dragging{},hot{};int grab{};
    ScrollModel scroll;HFONT font{},heading{},metric{};
    page::Buffer buffer;
    struct Controls {std::string key;HWND edit{},spin{},apply{},reset{};bool dirty{};};std::vector<Controls> controls;
    int scale(int n)const{return MulDiv(n,dpi,96);}
    Impl(HWND parent,GpuVendor selected,OsdPopup* osd):GpuOperations(selected),popup(osd){INITCOMMONCONTROLSEX common{sizeof(common),ICC_UPDOWN_CLASS};InitCommonControlsEx(&common);
        WNDCLASSW c{};c.lpfnWndProc=procedure;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"MagicControl.DiscreteGpu";c.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));RegisterClassW(&c);
        window=CreateWindowExW(WS_EX_CONTROLPARENT,c.lpszClassName,mc::windows::encoding::wide(std::string("GPU ")+gpuVendorName(vendor)).c_str(),WS_CHILD|WS_CLIPCHILDREN,0,0,0,0,parent,nullptr,c.hInstance,this);
        if(!window)throw std::runtime_error("Cannot create discrete GPU page");bindReady([target=window]{PostMessageW(target,done,0,0);});fonts();}
    ~Impl(){KillTimer(window,1);shutdown();DestroyWindow(window);DeleteObject(font);DeleteObject(heading);DeleteObject(metric);}
    void fonts(){fontLanguage=ui::languageIndex();if(font)DeleteObject(font);if(heading)DeleteObject(heading);if(metric)DeleteObject(metric);
        font=createUiFont(ui::FontRole::Body,dpi);heading=createUiFont(ui::FontRole::Heading,dpi);metric=createUiFont(ui::FontRole::FanMetric,dpi);
        for(auto& c:controls)for(auto h:{c.edit,c.spin,c.apply,c.reset})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
    std::string key(std::size_t i)const{return readings[i].uuid.empty()?"unavailable:"+std::to_string(i):readings[i].uuid;}
    void rebuild(){bool changed=controls.size()!=readings.size();if(!changed)for(std::size_t i=0;i<controls.size();++i)changed|=controls[i].key!=key(i);
        if(!changed)return;for(auto& c:controls)for(auto h:{c.edit,c.spin,c.apply,c.reset})DestroyWindow(h);controls.clear();
        for(std::size_t i=0;i<readings.size();++i){Controls c;c.key=key(i);const int base=100+static_cast<int>(i)*4;
            auto make=[&](const wchar_t* klass,const wchar_t* text,int offset,DWORD style){return CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(base+offset)),GetModuleHandleW(nullptr),nullptr);};
            c.edit=make(L"EDIT",L"",0,ES_CENTER|WS_BORDER);c.spin=make(UPDOWN_CLASSW,L"",1,UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_NOTHOUSANDS|WS_BORDER);
            c.apply=make(L"BUTTON",L"Применить",2,BS_OWNERDRAW);c.reset=make(L"BUTTON",L"Штатный лимит",3,BS_OWNERDRAW);
            control::themeButton(c.apply);control::themeButton(c.reset);
            SendMessageW(c.edit,EM_SETLIMITTEXT,5,0);SendMessageW(c.spin,UDM_SETBUDDY,reinterpret_cast<WPARAM>(c.edit),0);
            if(!applyNumericFieldTheme(c.edit,c.spin))throw std::runtime_error("Cannot theme NVIDIA numeric field");controls.push_back(c);}
        fonts();}
    void put(HWND h,ui::Rect r){page::place(h,r.left,r.top-scroll.position(),r.width(),r.height());}
    void layout(){scroll.setViewport(height);scroll.setExtent(scale(12+static_cast<int>(std::max<std::size_t>(1,readings.size()))*(ui::kGpuCardHeight+6)));
        for(std::size_t i=0;i<controls.size();++i){const auto g=ui::gpuCardGeometry(width,static_cast<int>(i),{static_cast<int>(dpi)});auto& c=controls[i];
            put(c.edit,g.edit);put(c.spin,g.spin);put(c.apply,g.apply);put(c.reset,g.reset);}
        availability();InvalidateRect(window,nullptr,FALSE);}
    void availability(){syncing=true;const auto enable=[](HWND h,bool ready){if((IsWindowEnabled(h)!=FALSE)!=ready)EnableWindow(h,ready);};
        for(std::size_t i=0;i<controls.size();++i){auto& c=controls[i];const auto& r=readings[i];
        const bool editable=r.available&&r.powerControl&&!applying&&!deviceOperations().interactiveBusy()&&!ui::laptopPreview();
        const UDACCEL increment{0,static_cast<UINT>(std::max(1,r.powerStep.value_or(1)))};SendMessageW(c.spin,UDM_SETACCEL,1,reinterpret_cast<LPARAM>(&increment));
        enable(c.edit,editable);enable(c.spin,editable);enable(c.apply,editable&&!busy);
        enable(c.reset,editable&&!busy&&gpuPowerDefaultKnown(r));
        if(gpuPowerRangeKnown(r)){int low{},high{};SendMessageW(c.spin,UDM_GETRANGE32,reinterpret_cast<WPARAM>(&low),reinterpret_cast<LPARAM>(&high));
            const int nextLow=gpuPowerMinimum(r),nextHigh=gpuPowerMaximum(r);
            if(low!=nextLow||high!=nextHigh)SendMessageW(c.spin,UDM_SETRANGE32,nextLow,nextHigh);}
        if(!c.dirty&&GetFocus()!=c.edit){const auto expected=gpuPowerKnown(r)?std::to_wstring(gpuPowerValue(r)):L"—";wchar_t current[32]{};GetWindowTextW(c.edit,current,32);
            if(expected!=current)SetWindowTextW(c.edit,expected.c_str());}}
        syncing=false;}
    void start(std::string id={},std::optional<int> watts={},bool reset=false){
        if(busy)return;
        const bool action=!id.empty();
        if(action&&popup){std::string device;for(const auto& r:readings)if(r.uuid==id)device=r.name;
            osdNotice=osd::gpuPower(id,gpuVendorName(vendor),device,watts,reset);osdToken=popup->beginOperation(osdNotice);}
        std::optional<std::vector<GpuReading>> preview;
        if(vendor==GpuVendor::Nvidia&&ui::nvidiaModelPreview())preview=ui::nvidiaPreviewCard();
        submit({action?(reset?GpuOperation::ResetPower:GpuOperation::SetPower):GpuOperation::Read,std::move(id),watts},ui::laptopPreview(),std::move(preview));
        availability();
    }
    void finish(){
        std::optional<std::vector<GpuReading>> preview;
        if(vendor==GpuVendor::Nvidia&&ui::nvidiaModelPreview())preview=ui::nvidiaPreviewCard();
        const auto result=take(std::move(preview));
        if(!result)return;
        if(result->action){if(result->success)for(auto& c:controls)if(c.key==result->uuid)c.dirty=false;
            if(popup&&osdToken){auto notice=osdNotice;notice.detail=result->message;notice.error=!result->success;popup->completeOperation(osdToken,std::move(notice));osdToken=0;}}
        rebuild();layout();
    }
    void command(int id,int notification){if(id<100)return;const auto index=static_cast<std::size_t>((id-100)/4);if(index>=controls.size())return;
        if(ui::laptopPreview())return;
        const int action=(id-100)%4;auto& c=controls[index];if(action==0&&notification==EN_CHANGE&&!syncing)c.dirty=true;
        if(notification!=BN_CLICKED||action<2||busy)return;
        try{if(!readings[index].powerControl)throw std::runtime_error("Управление недоступно для этой GPU");
            if(action==3){start(readings[index].uuid,{},true);return;}
            wchar_t value[24]{};GetWindowTextW(c.edit,value,24);wchar_t* end{};const auto watts=wcstol(value,&end,10);
            if(!value[0]||*end||watts < -100000 || watts>100000)throw std::runtime_error("Введите лимит в указанных единицах из разрешённого диапазона");
            start(readings[index].uuid,static_cast<int>(watts));
        }catch(const std::exception& e){message=e.what();
            if(popup){auto notice=osd::gpuPower(c.key,gpuVendorName(vendor),readings[index].name,{},false);notice.detail=message;notice.error=true;popup->show(std::move(notice));}
            InvalidateRect(window,nullptr,FALSE);}}
    void text(HDC dc,std::string value,ui::Rect r,HFONT f,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE){
        RECT rect=toNativeRect(r);OffsetRect(&rect,0,-scroll.position());SelectObject(dc,f);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);
        const auto w=mc::windows::encoding::wide(value);::mc::gui::localizedDrawText(dc,w.c_str(),-1,&rect,flags|DT_NOPREFIX|DT_END_ELLIPSIS);}
    RECT thumb()const{
        return page::scrollThumb(width, height, scroll, dpi, hot, dragging);
    }
    void scrollChanged(int previous){
        const int delta=previous-scroll.position();
        if(delta)ScrollWindowEx(window,0,delta,nullptr,nullptr,nullptr,nullptr,SW_INVALIDATE|SW_SCROLLCHILDREN);
        invalidateThumb();
    }
    void invalidateThumb(){const RECT lane{width-scale(12),0,width,height};InvalidateRect(window,&lane,FALSE);}
    void paint(){
        page::Paint painting(window, buffer);
        if (!painting.valid()) return;
        const HDC dc = painting.dc();
        const auto& client = painting.client();
        fill(dc,client,theme::kBackground);
        if(readings.empty())text(dc,std::string("Читаю данные ")+gpuVendorName(vendor)+"…",{scale(20),scale(20),width,scale(60)},font,theme::kTextSecondary);
        for(std::size_t i=0;i<readings.size();++i){const auto& r=readings[i];const auto g=ui::gpuCardGeometry(width,static_cast<int>(i),{static_cast<int>(dpi)});
            auto card=toNativeRect(g.card);OffsetRect(&card,0,-scroll.position());fill(dc,card,theme::kSurface);text(dc,r.name,g.title,heading,theme::kTextPrimary);
            const auto values=ui::gpuMetrics(r);for(int j=0;j<4;++j){auto box=g.metrics[j];box.bottom=box.top+scale(30);text(dc,values[j],box,metric,theme::kTextPrimary,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                box.top=box.bottom;box.bottom=g.metrics[j].bottom;text(dc,ui::gpuMetricCaptions[j],box,font,theme::kTextSecondary,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
            const auto details=ui::gpuDetails(r);for(int j=0;j<5;++j)text(dc,details[j],g.details[j],font,theme::kTextSecondary);
            text(dc,ui::gpuPowerCaption(r),g.powerLabel,font,theme::kTextSecondary);
            const bool staticPreview=vendor==GpuVendor::Nvidia&&ui::nvidiaModelPreview();
            const auto status=ui::translate(message.empty()||staticPreview?r.status:message);
            text(dc,(ui::laptopPreview()&&!staticPreview?ui::translate("Просмотр · GPU текущего компьютера.")+" ":"")+status+"\n"+(r.uuid.empty()?"":r.uuid),g.status,font,theme::kTextMuted,DT_LEFT|DT_WORDBREAK);}
        if(scroll.scrollable()) page::drawThumb(dc,thumb(),hot,dragging);
        painting.finish();
    }
    void draw(DRAWITEMSTRUCT& item){control::drawButton(item,font,dpi);}
    static LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp){auto* p=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(msg==WM_NCCREATE){p=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}if(!p)return DefWindowProcW(h,msg,wp,lp);
        switch(msg){case WM_ERASEBKGND:return 1;case WM_PAINT:p->paint();return 0;case WM_TIMER:if(p->visible&&!p->busy)p->start();return 0;case done:p->finish();return 0;
        case WM_COMMAND:p->command(LOWORD(wp),HIWORD(wp));return 0;case WM_DRAWITEM:p->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
        case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:
            if(const auto brush=numericFieldColors(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)))return reinterpret_cast<LRESULT>(brush);
            break;
        case WM_MOUSEWHEEL:{const int previous=p->scroll.position();if(page::wheel(p->scroll,GET_WHEEL_DELTA_WPARAM(wp),p->dpi))p->scrollChanged(previous);return 0;}
        case WM_LBUTTONDOWN:if(p->scroll.scrollable()&&GET_X_LPARAM(lp)>=p->width-p->scale(12)){
            const auto t=p->thumb();const int y=GET_Y_LPARAM(lp);
            const int before=p->scroll.position();page::scrollPress(h,p->scroll,t,y,p->dragging,p->grab);p->scrollChanged(before);return 0;}break;
        case WM_LBUTTONUP:if(p->dragging){ReleaseCapture();p->dragging=false;return 0;}break;case WM_CAPTURECHANGED:p->dragging=false;p->invalidateThumb();return 0;
        case WM_MOUSEMOVE:{if(page::hover(h,p->scroll,p->dpi,GET_X_LPARAM(lp),p->hot))p->invalidateThumb();
            if(p->dragging){const auto t=p->thumb();const int before=p->scroll.position();page::scrollDrag(p->scroll,t,p->height,p->dpi,GET_Y_LPARAM(lp),p->grab);if(before!=p->scroll.position())p->scrollChanged(before);}
            TRACKMOUSEEVENT t{sizeof(t),TME_LEAVE,h,0};TrackMouseEvent(&t);return 0;}
        case WM_MOUSELEAVE:if(p->hot){p->hot=false;p->invalidateThumb();}return 0;}
        return DefWindowProcW(h,msg,wp,lp);}
};
DiscreteGpuPage::DiscreteGpuPage(HWND parent,GpuVendor vendor,OsdPopup* osd):impl_(std::make_unique<Impl>(parent,vendor,osd)){}DiscreteGpuPage::~DiscreteGpuPage()=default;
void DiscreteGpuPage::arrange(const RECT& area,UINT dpi,bool visible){auto& p=*impl_;const bool was=p.visible;p.visible=visible;
    if(p.dpi!=dpi||p.fontLanguage!=ui::languageIndex()){p.dpi=dpi;p.fonts();}
    if(!visible){KillTimer(p.window,1);ShowWindow(p.window,SW_HIDE);return;}
    p.width=area.right-area.left;p.height=area.bottom-area.top;page::place(p.window,area.left,area.top,p.width,p.height,true);p.layout();page::present(p.window);SetTimer(p.window,1,1000,nullptr);if(!was&&!p.busy)p.start();}
void DiscreteGpuPage::refreshCommandAvailability(){impl_->availability();}
bool DiscreteGpuPage::preTranslate(MSG& m){auto& p=*impl_;if(!p.visible)return false;if(m.message==WM_MOUSEWHEEL&&(m.hwnd==p.window||IsChild(p.window,m.hwnd))){SendMessageW(p.window,m.message,m.wParam,m.lParam);return true;}return IsDialogMessageW(p.window,&m)!=FALSE;}
}
