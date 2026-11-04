#include "MagicControl/ActionLog.h"
#include "LocalizationWindows.h"
#include "ControlTheme.h"
#include "PageMechanics.h"
#include "KeysPage.h"
#include "../common/FooterStatus.h"
#include "../common/DeviceHeader.h"
#include "../common/KeysPolicy.h"
#include "MagicControl/Logging.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <utility>
#include <string>
#include <windowsx.h>

#include "MagicControl/EcAccess.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/ScrollModel.h"
#include "Layout.h"
#include "../common/Theme.h"
#include "OsdPopup.h"
#include "../common/OsdMessages.h"
#include "../../platform/windows/TextWindows.h"
#include "../../platform/windows/KeysHardwareWindows.h"

namespace mc::gui {
namespace {
constexpr UINT_PTR kScrollMetricTimer = 72;
constexpr UINT_PTR kRefreshTimer = 73;

void fill(HDC dc, RECT rect, COLORREF color) {
    const HGDIOBJ previous = SelectObject(dc, GetStockObject(DC_BRUSH));
    const COLORREF oldColor = SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, oldColor); SelectObject(dc, previous);
}
void frame(HDC dc, RECT rect, COLORREF color) {
    const HGDIOBJ previous = SelectObject(dc, GetStockObject(DC_BRUSH));
    const COLORREF oldColor = SetDCBrushColor(dc, color);
    FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, oldColor); SelectObject(dc, previous);
}
void text(HDC dc, const wchar_t* value, RECT rect, HFONT font, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    const HGDIOBJ previous = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    ::mc::gui::localizedDrawText(dc, value, -1, &rect, flags | DT_NOPREFIX); SelectObject(dc, previous);
}
using page::Buffer;
}

struct KeysPage::Impl {
    struct Row { std::wstring title, detail; int value{-1}; };
    HWND page{}, scrollbar{};
    std::array<Row, ui::keyRowCount> rows{};
    HFONT body{}, rowTitleFont{}, detailFont{}, bold{};
    Buffer buffer;
    std::unique_ptr<KeysOperations> operations;
    UINT dpi{96};unsigned fontLanguage{~0u}; int width{}, height{};
    int quickCardBottom{}, devicesCardTop{}, devicesCardBottom{};
    int resultTop{}, contentHeight{}, titleLeft{}, titleWidth{}, offLeft{}, onLeft{}, buttonWidth{}, rowHeight{};
    ScrollModel scroll;
    bool visible{}, loaded{}, scrollHot{}, scrollDragging{};
    OsdPopup* popup{};
    int scrollGrabOffset{}, hoverRow{-1}, pressedRow{-1}, focusRow{-1};
    bool hoverOn{}, pressedOn{}, focusOn{};
    std::wstring resultText{L"Читаю текущие состояния…"};
    std::chrono::steady_clock::time_point scrollBurstStart{}, lastScrollInput{};
    unsigned scrollInputEvents{}, scrollPaintEvents{};
    double scrollPaintTotalMs{}, scrollPaintMaxMs{};

    explicit Impl(HWND parent, EcAccess* access, ApplicationPreferences& settings,
                  std::function<void(const ApplicationPreferences&)> applySettings, OsdPopup* popupWindow)
        : popup(popupWindow) {
        WNDCLASSW klass{}; klass.lpfnWndProc = procedure; klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"MagicControl.KeysPage"; klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassW(&klass);
        page = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"KEYs",
                               WS_CHILD | WS_TABSTOP | WS_CLIPCHILDREN,
                               0, 0, 0, 0, parent, nullptr, klass.hInstance, this);
        WNDCLASSW scrollClass{}; scrollClass.lpfnWndProc = scrollbarProcedure;
        scrollClass.hInstance = klass.hInstance; scrollClass.lpszClassName = L"MagicControl.KeysScrollbar";
        scrollClass.hCursor = klass.hCursor; RegisterClassW(&scrollClass);
        // Opaque fixed sibling surface: parent scroll/paint must never erase the thumb.
        scrollbar = CreateWindowExW(0, scrollClass.lpszClassName, L"",
            WS_CHILD, 0, 0, 0, 0, page, nullptr, klass.hInstance, this);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            rows[i].title = wide(ui::keyTitles[i]);
            rows[i].detail = L"Читаю состояние…";
        }
        rows[1] = {wide(ui::keyTitles[1]), L"Отключает наши уведомления OSD. Выбор сохраняется в settings.", settings.osdDisabled};
        createFonts();
        operations=windows::createKeysOperations(access,ui::laptopPreview(),settings,std::move(applySettings),
            [notifications=popup](KeyAction action,bool on){return notifications?notifications->beginOperation(osd::keySwitch(static_cast<int>(action),on)):0;},
            [window=page]{PostMessageW(window,WM_APP+20,0,0);});
    }
    ~Impl() {
        operations.reset();
        if (page) KillTimer(page, kRefreshTimer);
        if (page) DestroyWindow(page);
        if (body) DeleteObject(body);
        if (rowTitleFont) DeleteObject(rowTitleFont);
        if (detailFont) DeleteObject(detailFont);
        if (bold) DeleteObject(bold);
    }
    int scale(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    int scrollLane() const { return scale(12); }
    bool hitScrollBar(int x) const { return x >= width - scrollLane(); }
    void createFonts() {
        fontLanguage=ui::languageIndex();
        if (body) DeleteObject(body);
        if (rowTitleFont) DeleteObject(rowTitleFont);
        if (detailFont) DeleteObject(detailFont);
        if (bold) DeleteObject(bold);
        body = createUiFont(ui::FontRole::ClassicBody, dpi);
        rowTitleFont = createUiFont(ui::FontRole::KeyTitle, dpi);
        detailFont = createUiFont(ui::FontRole::KeyDetail, dpi);
        bold = createUiFont(ui::FontRole::ClassicHeading, dpi);
    }
    // КОНТРАКТ КАРТОЧЕК И ПРОКРУТКИ KEYs (16.10.2026).
    // keysGeometry получает полную ширину страницы; не вычитать ширину скролла
    // повторно и не сужать карточки ради правой полосы. scrollbar — отдельный
    // НЕПРОЗРАЧНЫЙ дочерний HWND поверх правого края, а не часть движущегося слоя.
    // scrollContents прокручивает только content RECT, исключающий scrollLane().
    // Не копировать thumb через ScrollWindowEx и не добавлять WS_EX_TRANSPARENT:
    // это возвращает затирание/исчезновение полосы. Инвалидировать её отдельно.
    // viewport = видимая высота, extent = весь контент вместе с нижним отступом.
    // Полоса остаётся видимой при scrollable(), без автоскрытия по бездействию.
    // Карточки и кнопки используют один scroll.position(), без двойного смещения.
    // DPI применяется ко всем размерам; layout/paint/scroll не меняют настройки EC.
    void arrange() {
        if (!visible || width < scale(240) || height < scale(100)) return;
        const auto g = ui::keysGeometry(width, height, {static_cast<int>(dpi)});
        buttonWidth = g.buttonWidth; rowHeight = g.rowHeight;
        titleLeft = g.titleLeft; titleWidth = g.titleWidth;
        offLeft = g.offLeft; onLeft = g.onLeft;
        quickCardBottom = g.quickCard.bottom;
        devicesCardTop = g.devicesCard.top; devicesCardBottom = g.devicesCard.bottom;
        resultTop = g.result.top; contentHeight = g.extent;
        scroll.setViewport(height);
        scroll.setExtent(contentHeight);
        SetWindowPos(scrollbar, HWND_TOP, width - scrollLane(), 0, scrollLane(), height,
                     SWP_NOACTIVATE | (scroll.scrollable() ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        InvalidateRect(page, nullptr, FALSE);
        InvalidateRect(scrollbar, nullptr, FALSE);
    }
    void scrollTo(int position) {
        const int previous = scroll.position();
        scroll.setPosition(position);
        scrollContents(scroll.position() - previous);
    }
    void scrollContents(int positionDelta) {
        if (!positionDelta) return;
        // Scroll only content pixels. The right-hand lane is a fixed viewport element.
        RECT content{0, 0, std::max(0, width - scrollLane()), height};
        ScrollWindowEx(page, 0, -positionDelta, &content, &content, nullptr, nullptr, SW_INVALIDATE);
        InvalidateRect(scrollbar, nullptr, FALSE);
    }
    RECT thumb() const {
        RECT client{}; GetClientRect(scrollbar, &client);
        return page::scrollThumb(client.right, client.bottom, scroll, dpi, scrollHot, scrollDragging, true);
    }
    void drawThumb(HDC dc) const {
        page::drawThumb(dc, thumb(), scrollHot, scrollDragging);
    }
    void wheel(int delta) {
        noteScrollInput();
        const int previous = scroll.position();
        if (page::wheel(scroll, delta, dpi)) scrollContents(scroll.position() - previous);
    }
    void noteScrollInput() {
        const auto now = std::chrono::steady_clock::now();
        if (scrollInputEvents == 0) scrollBurstStart = now;
        ++scrollInputEvents;
        lastScrollInput = now;
        SetTimer(page, kScrollMetricTimer, 100, nullptr);
    }
    void noteScrollPaint(std::chrono::steady_clock::time_point started) {
        if (scrollInputEvents == 0 || std::chrono::steady_clock::now() - lastScrollInput >
                                      std::chrono::milliseconds(250)) return;
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        ++scrollPaintEvents; scrollPaintTotalMs += elapsed;
        scrollPaintMaxMs = std::max(scrollPaintMaxMs, elapsed);
    }
    void finishScrollMetrics() {
        if (!scrollInputEvents) { KillTimer(page, kScrollMetricTimer); return; }
        if (std::chrono::steady_clock::now() - lastScrollInput < std::chrono::milliseconds(250)) return;
        const double durationMs = std::chrono::duration<double, std::milli>(
            lastScrollInput - scrollBurstStart).count();
        const double frequency = durationMs > 0.0
            ? static_cast<double>(scrollInputEvents > 1 ? scrollInputEvents - 1 : 0) * 1000.0 / durationMs
            : 0.0;
        try {
            if (loggingEnabled()) logMessage("ui_metrics", std::format(
                "KEYs scroll: input_events={} span_ms={:.1f} event_hz={:.1f} viewport_px={} content_px={} range_px={} paints={} paint_avg_ms={:.2f} paint_max_ms={:.2f}\n",
                scrollInputEvents, durationMs, frequency, scroll.viewport(), scroll.extent(),
                scroll.maximumPosition(), scrollPaintEvents,
                scrollPaintEvents ? scrollPaintTotalMs / scrollPaintEvents : 0.0, scrollPaintMaxMs));
        } catch (...) {}
        scrollInputEvents = scrollPaintEvents = 0;
        scrollPaintTotalMs = scrollPaintMaxMs = 0.0;
        KillTimer(page, kScrollMetricTimer);
    }
    void present(const KeyOperationResult& result) {
        for(std::size_t i=0;i<rows.size();++i){
            rows[i].value=result.snapshot.value[i];
            if(!result.snapshot.detail[i].empty())rows[i].detail=wide(result.snapshot.detail[i]);
        }
        if(!result.message.empty())resultText=wide(result.message);
        if(result.command&&popup&&result.command->noticeToken){
            auto message=osd::keySwitch(static_cast<int>(result.command->action),result.command->enabled);
            message.detail=result.noticeDetail;message.error=!result.success;
            popup->completeOperation(result.command->noticeToken,std::move(message));
        }
        if(result.announceOsdEnabled&&popup)popup->show({"osd","Экранные уведомления","Включены",osd::Icon::Notifications});
        if(!result.command||result.command->action!=KeyAction::Osd)loaded=true;
        render();updateButtons();
    }
    void start(int index,bool on) {
        audit::input("windows.keys","key_toggle_requested","\"row\":"+std::to_string(index)+",\"enabled\":"+(on?"true":"false"));
        const auto submitted=operations->submit(index,on);
        if(submitted.completed)present(*submitted.completed);
        else if(!submitted.message.empty()){resultText=wide(submitted.message);render();updateButtons();}
    }
    static std::wstring wide(const std::string& value) {
        const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(std::max(0, count)), L'\0');
        if (count) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
        return result;
    }
    void refresh() {
        if(!operations->refresh())return;
        updateButtons();if(!loaded)resultText=L"Читаю текущие состояния…";
        InvalidateRect(page,nullptr,FALSE);
    }
    void complete() {while(const auto result=operations->take())present(*result);}
    void observeState(int row,bool on) {
        operations->observe(row,on);
        if(row<0||row>=static_cast<int>(rows.size()))return;
        const auto value=operations->snapshot().value[static_cast<std::size_t>(row)];
        auto& current=rows[static_cast<std::size_t>(row)];
        if(current.value!=value){current.value=value;render();}
    }
    void restore(const std::string& reason){operations->restore(reason);}
    void render() {
        ui::footerStatuses().publish("keys",mc::windows::encoding::utf8(resultText));
        InvalidateRect(page, nullptr, FALSE);
    }
    void updateButtons() {
        InvalidateRect(page, nullptr, FALSE);
    }
    int rowTop(int index) const {
        const auto g = ui::keysGeometry(width, height, {static_cast<int>(dpi)});
        return g.rows[index].top;
    }
    RECT segmentRect(int index, bool on) const {
        const auto g = ui::keysGeometry(width, height, {static_cast<int>(dpi)});
        RECT rect = toNativeRect(on ? g.on[index] : g.off[index]);
        OffsetRect(&rect, 0, -scroll.position());
        return rect;
    }
    bool hitToggle(int x, int y, int& index, bool& on) const {
        if (x < offLeft || x >= onLeft + buttonWidth) return false;
        const int contentY = y + scroll.position();
        const auto g = ui::keysGeometry(width, height, {static_cast<int>(dpi)});
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (contentY < g.off[i].top || contentY >= g.off[i].bottom) continue;
            index = i; on = x >= onLeft; return true;
        }
        return false;
    }
    void drawToggle(HDC dc, int index, bool on, int y) const {
        const Row& row = rows[static_cast<std::size_t>(index)];
        const bool disabled = operations->disabled(index);
        const bool selected = row.value >= 0 && ((row.value != 0) == on);
        const bool hover = hoverRow == index && hoverOn == on;
        const bool pressed = pressedRow == index && pressedOn == on;
        RECT rect{on ? onLeft : offLeft, y, (on ? onLeft : offLeft) + buttonWidth, y + scale(kButtonHeight)};
        control::ButtonStyle style; style.segment = true; style.border = false;
        control::drawButton(dc, rect, on ? L"Вкл." : L"Выкл.", body, dpi,
            {disabled, selected, pressed, hover, false}, style);
    }

    void drawSwitch(HDC dc, int index, int y) const {
        const RECT off = segmentRect(index, false), on = segmentRect(index, true);
        drawToggle(dc, index, false, y); drawToggle(dc, index, true, y);
        RECT outer{off.left, off.top, on.right, off.bottom};
        frame(dc, outer, theme::kDivider);
        const HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
        const COLORREF oldColor = SetDCPenColor(dc, theme::kDivider);
        MoveToEx(dc, on.left, on.top + 1, nullptr); LineTo(dc, on.left, on.bottom - 1);
        SetDCPenColor(dc, oldColor); SelectObject(dc, oldPen);
        if (GetFocus() == page && focusRow == index) {
            RECT focus = focusOn ? on : off; InflateRect(&focus, -scale(3), -scale(3));
            DrawFocusRect(dc, &focus);
        }
    }
    void paint() {
        const auto paintStarted = std::chrono::steady_clock::now();
        page::Paint painting(page, buffer);
        if (!painting.valid()) return;
        const HDC dc = painting.dc();
        const auto& client = painting.client();
        const auto& ps = painting.info();
        fill(dc, client, theme::kBackground);
        const int offset = scroll.position();
        const auto g = ui::keysGeometry(width, height, {static_cast<int>(dpi)});
        const auto shifted = [offset](ui::Rect r) {
            RECT rect = toNativeRect(r); OffsetRect(&rect, 0, -offset); return rect;
        };
        RECT quick = shifted(g.quickCard), devices = shifted(g.devicesCard);
        fill(dc, quick, theme::kSurface); fill(dc, devices, theme::kSurface);
        const RECT quickTitle = shifted(g.quickHeading), devicesTitle = shifted(g.devicesHeading);
        text(dc, L"Быстрое переключение", quickTitle, bold, theme::kTextPrimary);
        text(dc, L"Устройства Windows", devicesTitle, bold, theme::kTextPrimary);
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const int naturalTop = rowTop(i);
            const int top = naturalTop - offset;
            if (top + rowHeight < ps.rcPaint.top || top > ps.rcPaint.bottom) continue;
            RECT title = shifted(g.titles[i]), detail = shifted(g.details[i]);
            text(dc, rows[static_cast<std::size_t>(i)].title.c_str(), title, rowTitleFont, theme::kTextPrimary,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            text(dc, rows[static_cast<std::size_t>(i)].detail.c_str(), detail, detailFont, theme::kTextSecondary,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            drawSwitch(dc, i, g.off[i].top - offset);
        }
        RECT resultRect = shifted(g.result);
        text(dc, resultText.c_str(), resultRect, body, theme::kTextSecondary,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        painting.finish();
        noteScrollPaint(paintStarted);
    }
    void ensureFocusedRowVisible() {
        if (focusRow < 0 || focusRow >= static_cast<int>(rows.size())) return;
        const int top = rowTop(focusRow), bottom = top + rowHeight;
        int target = scroll.position();
        if (top < target) target = top;
        else if (bottom > target + height) target = bottom - height;
        scrollTo(target);
    }
    static LRESULT CALLBACK scrollbarProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
            RECT client{}; GetClientRect(window, &client); fill(dc, client, theme::kBackground);
            if (self->scroll.scrollable()) self->drawThumb(dc);
            EndPaint(window, &ps); return 0;
        }
        case WM_MOUSEWHEEL:
            self->wheel(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
        case WM_LBUTTONDOWN: {
            if (!self->scroll.scrollable()) return 0;
            SetFocus(self->page);
            const int y = GET_Y_LPARAM(lp); const RECT thumb = self->thumb();
            page::scrollPress(window,self->scroll,thumb,y,self->scrollDragging,self->scrollGrabOffset,[self](int position){self->scrollTo(position);});
            InvalidateRect(window, nullptr, FALSE); return 0;
        }
        case WM_LBUTTONUP:
            if (self->scrollDragging) { ReleaseCapture(); return 0; }
            break;
        case WM_CAPTURECHANGED:
            if (self->scrollDragging) {
                self->scrollDragging = false; InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE: {
            const int y = GET_Y_LPARAM(lp);
            if (self->scrollDragging) {
                RECT client{}; GetClientRect(window, &client); const RECT thumb = self->thumb();
                page::scrollDrag(self->scroll,thumb,client.bottom,self->dpi,y,self->scrollGrabOffset,[self](int position){self->scrollTo(position);});
            }
            if (!self->scrollHot) { self->scrollHot = true; InvalidateRect(window, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self->scrollHot) { self->scrollHot = false; InvalidateRect(window, nullptr, FALSE); }
            return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            if (wp == kRefreshTimer) { self->refresh(); return 0; }
            if (wp == kScrollMetricTimer) { self->finishScrollMetrics(); return 0; }
            break;
        case WM_PAINT: self->paint(); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;
        case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_MOUSEWHEEL: self->wheel(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(window);
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (self->scroll.scrollable() && self->hitScrollBar(x)) {
                const RECT thumb = self->thumb();
                page::scrollPress(window,self->scroll,thumb,y,self->scrollDragging,self->scrollGrabOffset,[self](int position){self->scrollTo(position);});
                InvalidateRect(window, nullptr, FALSE); return 0;
            }
            int index{}; bool on{};
            if (self->hitToggle(x, y, index, on)) {
                self->pressedRow = self->focusRow = index; self->pressedOn = self->focusOn = on;
                SetCapture(window); InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (self->scrollDragging) { ReleaseCapture(); return 0; }
            if (self->pressedRow >= 0) {
                const int pressedIndex = self->pressedRow; const bool pressedOn = self->pressedOn;
                self->pressedRow = -1; ReleaseCapture();
                int index{}; bool on{};
                if (self->hitToggle(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), index, on) &&
                    index == pressedIndex && on == pressedOn) self->start(index, on);
                InvalidateRect(window, nullptr, FALSE); return 0;
            }
            break;
        }
        case WM_CAPTURECHANGED:
            self->scrollDragging = false; self->pressedRow = -1;
            InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (self->scrollDragging) {
                RECT client{}; GetClientRect(window, &client); const RECT thumb = self->thumb();
                page::scrollDrag(self->scroll,thumb,client.bottom,self->dpi,y,self->scrollGrabOffset,[self](int position){self->scrollTo(position);});
            }
            int index{}; bool on{};
            const bool found = self->hitToggle(x, y, index, on);
            const int nextHover = found ? index : -1;
            const bool nextOn = found && on;
            if (nextHover != self->hoverRow || nextOn != self->hoverOn) {
                self->hoverRow = nextHover; self->hoverOn = nextOn;
                InvalidateRect(window, nullptr, FALSE);
            }
            const bool hot = self->scroll.scrollable() && self->hitScrollBar(x);
            if (hot != self->scrollHot) { self->scrollHot = hot; InvalidateRect(window, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            self->hoverRow = -1; self->scrollHot = false; InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_APP + 20: self->complete(); return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }
};

KeysPage::KeysPage(HWND parent, EcAccess* ec, ApplicationPreferences& preferences,
                   std::function<void(const ApplicationPreferences&)> applyPreferences, OsdPopup* osd)
    : impl_(std::make_unique<Impl>(parent, ec, preferences, std::move(applyPreferences), osd)) {}
KeysPage::~KeysPage() = default;
void KeysPage::refreshCommandAvailability() { impl_->updateButtons(); }
void KeysPage::restore(const std::string& reason) { impl_->restore(reason); }
void KeysPage::suspend() { impl_->operations->suspend(); }
void KeysPage::observeState(int row,bool on) { impl_->observeState(row,on); }
void KeysPage::arrange(const RECT& area, UINT dpi, bool visible) {
    auto& p = *impl_; const bool wasVisible = p.visible; p.visible = visible;
    if (dpi != p.dpi || p.fontLanguage != ui::languageIndex()) { p.dpi = dpi; p.createFonts(); }
    if (!visible) { KillTimer(p.page, kRefreshTimer); ShowWindow(p.page, SW_HIDE); return; }
    p.width = area.right - area.left; p.height = area.bottom - area.top;
    page::place(p.page, area.left, area.top, p.width, p.height, true);
    p.arrange(); if (!p.loaded || !wasVisible) p.refresh();
    SetTimer(p.page, kRefreshTimer, 2000, nullptr);
    page::present(p.page);
}
bool KeysPage::preTranslate(MSG& message) {
    auto& page = *impl_;
    if (!page.visible) return false;
    if (message.message == WM_KEYDOWN) {
        if (message.wParam == VK_TAB && GetFocus() == page.page) {
            const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            page.focusRow = page.focusRow < 0 ? (backwards ? static_cast<int>(ui::keyRowCount)-1 : 0)
                : (page.focusRow + (backwards ? static_cast<int>(ui::keyRowCount)-1 : 1)) % static_cast<int>(ui::keyRowCount);
            page.focusOn = page.rows[static_cast<std::size_t>(page.focusRow)].value == 1;
            page.ensureFocusedRowVisible(); InvalidateRect(page.page, nullptr, FALSE);
            return true;
        }
        if (GetFocus() == page.page && page.focusRow >= 0) {
            if (message.wParam == VK_LEFT || message.wParam == VK_RIGHT) {
                page.focusOn = message.wParam == VK_RIGHT;
                InvalidateRect(page.page, nullptr, FALSE); return true;
            }
            if (message.wParam == VK_SPACE || message.wParam == VK_RETURN) {
                page.start(page.focusRow, page.focusOn); return true;
            }
        }
        int position = page.scroll.position();
        bool handled = true;
        switch (message.wParam) {
        case VK_UP: position += page.scroll.lineStep(false, page.scale(24)); break;
        case VK_DOWN: position += page.scroll.lineStep(true, page.scale(24)); break;
        case VK_PRIOR: position += page.scroll.pageStep(false); break;
        case VK_NEXT: position += page.scroll.pageStep(true); break;
        case VK_HOME: position = 0; break;
        case VK_END: position = page.scroll.maximumPosition(); break;
        default: handled = false; break;
        }
        if (handled) {
            page.scrollTo(position);
            return true;
        }
    }
    return IsDialogMessageW(page.page, &message) != FALSE;
}
}
