#include "NumericFieldTheme.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../common/Theme.h"
#include "PageMechanics.h"
#include "Layout.h"

namespace mc::gui {
namespace {

constexpr UINT_PTR kThemeSubclass = 0x4D434E55;

struct FieldTheme {
    HWND edit{};
    HWND upDown{};
    int hot{-1};
    int pressed{-1};
    page::Buffer buffer;
    HFONT initialFont{};
    ~FieldTheme(){if(initialFont)DeleteObject(initialFont);}
};

void fillColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

int pixels(HWND window, int logical) {
    return std::max(1, MulDiv(logical, static_cast<int>(GetDpiForWindow(window)), 96));
}

COLORREF borderColor(const FieldTheme& field) {
    const HWND focus = GetFocus();
    return IsWindowEnabled(field.edit) && (focus == field.edit || focus == field.upDown)
        ? theme::kNumericPressed : theme::kNumericBorder;
}

void centerEditClient(HWND edit, RECT& client) {
    HDC dc = GetDC(edit);
    if (!dc) return;
    const auto font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    const HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW metrics{};
    if (GetTextMetricsW(dc, &metrics)) {
        const int available = std::max(0, static_cast<int>(client.bottom - client.top));
        const int height = std::min(available, std::max(1, static_cast<int>(metrics.tmHeight)));
        client.top += (available - height) / 2;
        client.bottom = client.top + height;
    }
    if (previous) SelectObject(dc, previous);
    ReleaseDC(edit, dc);
}

void paintEditFrame(const FieldTheme& field) {
    if (!IsWindow(field.edit)) return;
    RECT bounds{};
    RECT client{};
    GetWindowRect(field.edit, &bounds);
    GetClientRect(field.edit, &client);
    POINT origin{0, 0};
    ClientToScreen(field.edit, &origin);
    OffsetRect(&client, origin.x - bounds.left, origin.y - bounds.top);
    OffsetRect(&bounds, -bounds.left, -bounds.top);
    HDC dc = GetWindowDC(field.edit);
    if (!dc) return;
    const int saved = SaveDC(dc);
    ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
    // Vertical text padding belongs to the background, not to the outline.
    fillColor(dc, bounds, IsWindowEnabled(field.edit) ?
        theme::kNumericBackground : theme::kNumericDisabledBackground);
    const int edge = pixels(field.edit, 1);
    const COLORREF border = borderColor(field);
    fillColor(dc, RECT{bounds.left, bounds.top, bounds.right, bounds.top + edge}, border);
    fillColor(dc, RECT{bounds.left, bounds.bottom - edge, bounds.right, bounds.bottom}, border);
    fillColor(dc, RECT{bounds.left, bounds.top, bounds.left + edge, bounds.bottom}, border);
    fillColor(dc, RECT{bounds.right - edge, bounds.top, bounds.right, bounds.bottom}, border);
    RestoreDC(dc, saved);
    ReleaseDC(field.edit, dc);
}

int halfAt(HWND window, int x, int y) {
    RECT rect{};
    GetClientRect(window, &rect);
    const POINT point{x, y};
    return PtInRect(&rect, point) ? (y < (rect.top + rect.bottom) / 2 ? 0 : 1) : -1;
}

void repaintPair(const FieldTheme& field) {
    if (IsWindow(field.edit))
        RedrawWindow(field.edit, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
    if (IsWindow(field.upDown)) InvalidateRect(field.upDown, nullptr, FALSE);
}

void paintArrows(HWND window, HDC dc, const FieldTheme& field) {
    RECT rect{};
    GetClientRect(window, &rect);
    const int edge = pixels(window, 1);
    const int middle = (rect.top + rect.bottom) / 2;
    const bool enabled = IsWindowEnabled(window) != FALSE;
    int lower{}, upper{};
    SendMessageW(window, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&lower),
                 reinterpret_cast<LPARAM>(&upper));
    wchar_t text[32]{};
    GetWindowTextW(field.edit, text, static_cast<int>(std::size(text)));
    wchar_t* end{};
    const long value = std::wcstol(text, &end, 10);
    const bool valid = end != text && *end == L'\0';

    for (int half = 0; half < 2; ++half) {
        RECT button{rect.left, half == 0 ? rect.top : middle,
                    rect.right, half == 0 ? middle : rect.bottom};
        const bool available = enabled && (!valid || (half == 0 ? value < upper : value > lower));
        const bool down = available && field.pressed == half && field.hot == half &&
                          GetCapture() == window && (GetKeyState(VK_LBUTTON) & 0x8000);
        const COLORREF background = !enabled ? theme::kNumericDisabledBackground :
            down ? theme::kNumericPressed :
            available && field.hot == half ? theme::kNumericHover : theme::kNumericBackground;
        fillColor(dc, button, background);
        const int centerX = (button.left + button.right - edge) / 2;
        const int centerY = (button.top + button.bottom) / 2;
        const int radius = std::min(pixels(window, 3),
            std::max(1, static_cast<int>(rect.right - rect.left) / 3));
        const int slope = std::max(1, radius / 2);
        const int sign = half == 0 ? 1 : -1;
        const POINT points[3]{{centerX - radius, centerY + sign * slope},
                              {centerX, centerY - sign * slope},
                              {centerX + radius, centerY + sign * slope}};
        HPEN pen = CreatePen(PS_SOLID, edge,
            available ? theme::kNumericText : theme::kNumericDisabledText);
        const HGDIOBJ previous = SelectObject(dc, pen);
        Polyline(dc, points, 3);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    // The EDIT's right edge is the shared vertical separator.
    fillColor(dc, RECT{rect.left, middle, rect.right, middle + edge}, theme::kNumericButtonBorder);
    const COLORREF border = borderColor(field);
    fillColor(dc, RECT{rect.left, rect.top, rect.right, rect.top + edge}, border);
    fillColor(dc, RECT{rect.left, rect.bottom - edge, rect.right, rect.bottom}, border);
    fillColor(dc, RECT{rect.right - edge, rect.top, rect.right, rect.bottom}, border);
}

bool editHasText(HWND edit,std::wstring_view requested){
    const int size=GetWindowTextLengthW(edit);if(size<0||static_cast<std::size_t>(size)!=requested.size())return false;
    std::vector<wchar_t> text(static_cast<std::size_t>(size)+1);GetWindowTextW(edit,text.data(),size+1);
    return std::wstring_view(text.data(),size)==requested;
}

LRESULT CALLBACK themedProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                  UINT_PTR, DWORD_PTR parameter) {
    auto& field = *reinterpret_cast<FieldTheme*>(parameter);
    const bool arrows = window == field.upDown;
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, themedProcedure, kThemeSubclass);
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        delete &field;
        return result;
    }
    if(!arrows&&message==WM_SETTEXT&&editHasText(window,lParam?std::wstring_view(reinterpret_cast<const wchar_t*>(lParam)):std::wstring_view{}))return TRUE;
    if(!arrows&&message==WM_SETTEXT){
        // Native EDIT may paint synchronously from SetWindowText. Defer that
        // changed value to our complete buffered WM_PAINT instead of exposing
        // an erased/intermediate glyph row. Do not unhide an already hidden edit.
        const bool visible=IsWindowVisible(window)!=FALSE;
        if(visible)DefSubclassProc(window,WM_SETREDRAW,FALSE,0);
        const auto result=DefSubclassProc(window,message,wParam,lParam);
        if(visible)DefSubclassProc(window,WM_SETREDRAW,TRUE,0);
        repaintPair(field);return result;
    }
    if(arrows&&message==UDM_SETPOS32){
        int minimum{},maximum{};SendMessageW(window,UDM_GETRANGE32,reinterpret_cast<WPARAM>(&minimum),reinterpret_cast<LPARAM>(&maximum));
        const int requested=static_cast<int>(lParam);
        // UDS_SETBUDDYINT reads this canonical text before its next native step.
        // UDM_GETPOS32 itself invalidates native arrows, so avoid it for no-ops.
        if(requested>=minimum&&requested<=maximum&&editHasText(field.edit,std::to_wstring(requested)))return requested;
    }
    if(arrows&&message==UDM_SETRANGE32){int minimum{},maximum{};SendMessageW(window,UDM_GETRANGE32,reinterpret_cast<WPARAM>(&minimum),reinterpret_cast<LPARAM>(&maximum));
        if(minimum==static_cast<int>(wParam)&&maximum==static_cast<int>(lParam))return 0;}
    if(message==WM_PAINT){
        PAINTSTRUCT paint{};HDC target=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);
        if(client.right>0&&client.bottom>0){HDC dc=field.buffer.ensure(target,client.right,client.bottom)?field.buffer.dc:target;
            if(arrows)paintArrows(window,dc,field);
            else{fillColor(dc,client,IsWindowEnabled(window)?theme::kNumericBackground:theme::kNumericDisabledBackground);
                DefSubclassProc(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);}
            if(dc!=target)BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);}
        EndPaint(window,&paint);if(!arrows)paintEditFrame(field);return 0;
    }
    if (!arrows && message == WM_NCCALCSIZE) {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        RECT& client = wParam ? reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]
                             : *reinterpret_cast<RECT*>(lParam);
        // Preserve single-line native EDIT input, caret and selection. Only its
        // client text row is centered inside the consumer's full field bounds.
        centerEditClient(window, client);
        return result;
    }
    if (!arrows && message == WM_NCHITTEST) {
        // The padding must remain clickable as part of the numeric input.
        return HTCLIENT;
    }
    if(message==WM_ERASEBKGND)return 1; // Buffered WM_PAINT owns the complete client, including text background.
    if (!arrows && message == WM_NCPAINT) {
        paintEditFrame(field);
        return 0;
    }
    if (arrows) {
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PRINT:
        case WM_PRINTCLIENT:
            paintArrows(window, reinterpret_cast<HDC>(wParam), field);
            return 0;
        case WM_MOUSEMOVE: {
            const int half = halfAt(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (half != field.hot) {
                field.hot = half;
                InvalidateRect(window, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            break;
        }
        case WM_MOUSELEAVE:
            field.hot = -1;
            InvalidateRect(window, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
            field.pressed = field.hot = halfAt(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            InvalidateRect(window, nullptr, FALSE);
            break;
        case WM_LBUTTONUP:
        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
            field.pressed = -1;
            InvalidateRect(window, nullptr, FALSE);
            break;
        }
    }
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (!arrows && (message == WM_SETFONT || message == WM_DPICHANGED_AFTERPARENT ||
                    message == WM_THEMECHANGED)) {
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        repaintPair(field);
    }
    if (arrows && (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE ||
                   message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
                   message == WM_CAPTURECHANGED || message == WM_TIMER)) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    if (!arrows && (message == WM_SETTEXT || message == WM_CHAR ||
                    message == WM_KEYUP || message == WM_PASTE)) {
        InvalidateRect(field.upDown, nullptr, FALSE);
    }
    switch (message) {
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_THEMECHANGED:
    case WM_SIZE:
    case UDM_SETPOS32:
    case UDM_SETRANGE32:
        repaintPair(field);
        break;
    case WM_PAINT:
        if (!arrows) paintEditFrame(field);
        break;
    }
    return result;
}

}  // namespace

HBRUSH numericFieldColors(HDC dc, HWND control) noexcept {
    DWORD_PTR data{};
    if (!GetWindowSubclass(control, themedProcedure, kThemeSubclass, &data)) return nullptr;
    const bool enabled = IsWindowEnabled(control) != FALSE;
    SetTextColor(dc, enabled ? theme::kNumericText : theme::kNumericDisabledText);
    const COLORREF background = enabled ? theme::kNumericBackground : theme::kNumericDisabledBackground;
    SetBkColor(dc, background); SetDCBrushColor(dc, background);
    return static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
}

bool applyNumericFieldTheme(HWND edit, HWND upDown) noexcept {
    auto* editTheme = new (std::nothrow) FieldTheme{edit, upDown};
    auto* arrowTheme = new (std::nothrow) FieldTheme{edit, upDown};
    if (!editTheme || !arrowTheme) {
        delete editTheme;
        delete arrowTheme;
        return false;
    }
    if (!SetWindowSubclass(edit, themedProcedure, kThemeSubclass,
                            reinterpret_cast<DWORD_PTR>(editTheme))) {
        delete editTheme;
        delete arrowTheme;
        return false;
    }
    if (!SetWindowSubclass(upDown, themedProcedure, kThemeSubclass,
                            reinterpret_cast<DWORD_PTR>(arrowTheme))) {
        RemoveWindowSubclass(edit, themedProcedure, kThemeSubclass);
        delete editTheme;
        delete arrowTheme;
        return false;
    }
    SetWindowLongPtrW(upDown, GWL_STYLE, GetWindowLongPtrW(upDown, GWL_STYLE) & ~WS_BORDER);
    editTheme->initialFont=createUiFont(ui::FontRole::Caption,GetDpiForWindow(edit));
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(editTheme->initialFont),FALSE);
    SendMessageW(upDown,WM_SETFONT,reinterpret_cast<WPARAM>(editTheme->initialFont),FALSE);
    SetWindowPos(edit, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SetWindowPos(upDown, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    repaintPair(*arrowTheme);
    return true;
}

NumericFieldWindows createNumericFieldWindows(HWND parent,int editId,int spinId,int minimum,int maximum,int value,int textLimit,bool hexadecimal){
    static const bool ready=[] { INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_UPDOWN_CLASS};return InitCommonControlsEx(&controls)!=FALSE; }();
    if(!ready)throw std::runtime_error("Не удалось инициализировать числовое поле");
    NumericFieldWindows f;
    // Without ES_AUTOHSCROLL a single-line EDIT rejects a third digit when its
    // rendered width exceeds the client area, even if EM_LIMITTEXT allows it.
    // Keep the shared compact geometry; allow typing100/255/hex just like arrows.
    f.edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(hexadecimal?0:ES_NUMBER)|ES_CENTER|ES_AUTOHSCROLL,0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(editId)),GetModuleHandleW(nullptr),nullptr);
    f.spin=CreateWindowExW(0,UPDOWN_CLASSW,L"",WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_NOTHOUSANDS,0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(spinId)),GetModuleHandleW(nullptr),nullptr);
    if(!f.edit||!f.spin||!applyNumericFieldTheme(f.edit,f.spin)){if(f.edit)DestroyWindow(f.edit);if(f.spin)DestroyWindow(f.spin);throw std::runtime_error("Не удалось создать числовое поле");}
    if(textLimit>0)SendMessageW(f.edit,EM_SETLIMITTEXT,textLimit,0);
    SendMessageW(f.spin,UDM_SETBUDDY,reinterpret_cast<WPARAM>(f.edit),0);
    if(hexadecimal)SendMessageW(f.spin,UDM_SETBASE,16,0);
    SendMessageW(f.spin,UDM_SETRANGE32,minimum,maximum);SendMessageW(f.spin,UDM_SETPOS32,0,value);return f;
}
void placeNumericFieldWindows(NumericFieldWindows field,ui::Rect bounds,int arrowWidth,int offsetY,HFONT font){
    if(font)for(HWND control:{field.edit,field.spin})
        if(reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0))!=font)
            SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    bounds.top+=offsetY;bounds.bottom+=offsetY;
    const auto geometry=ui::numericFieldGeometry(bounds,arrowWidth);
    page::place(field.edit,geometry.edit.left,geometry.edit.top,geometry.edit.width(),geometry.edit.height());
    page::place(field.spin,geometry.spin.left,geometry.spin.top,geometry.spin.width(),geometry.spin.height());
}
bool releaseNumericFieldFocusWindows(const MSG& message,HWND focusTarget){
    const HWND focus=GetFocus();if(!focus||!IsChild(focusTarget,focus))return false;
    DWORD_PTR data{};if(!GetWindowSubclass(focus,themedProcedure,kThemeSubclass,&data))return false;
    const auto& field=*reinterpret_cast<const FieldTheme*>(data);
    if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN){SetFocus(focusTarget);return true;}
    if(message.message==WM_LBUTTONDOWN||message.message==WM_RBUTTONDOWN||message.message==WM_MBUTTONDOWN||message.message==WM_NCLBUTTONDOWN){
        if(message.hwnd!=field.edit&&message.hwnd!=field.upDown)SetFocus(focusTarget);
    }
    return false;
}
}  // namespace mc::gui
