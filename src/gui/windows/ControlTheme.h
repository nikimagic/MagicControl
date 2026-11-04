#pragma once
#include "MagicControl/ActionLog.h"
#include "../../platform/windows/TextWindows.h"
#include "LocalizationWindows.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
#include "../common/Theme.h"
#include "Layout.h"

namespace mc::gui::control {
struct ButtonState { bool disabled{}, selected{}, pressed{}, hovered{}, focused{}; };
struct ButtonStyle {
    COLORREF background{theme::kSurfaceRaised};
    COLORREF foreground{theme::kTextPrimary};
    bool highlightDisabledSelection{true};
    bool selectedBorder{};
    bool segment{};
    bool border{true};
    int padding{};
    UINT alignment{DT_CENTER};
    bool focusIndicator{true};
};
inline void drawButton(HDC dc, RECT rect, const wchar_t* caption, HFONT font, UINT dpi,
                       ButtonState state, ButtonStyle style = {}) {
    const bool highlighted = state.selected && (!state.disabled || style.highlightDisabledSelection);
    const COLORREF background = style.segment
        ? state.disabled ? (state.selected ? theme::kHoverOverlay : theme::kSurface)
          : state.pressed && state.selected ? theme::kAccentPressed
          : state.selected ? theme::kAccent : state.hovered ? theme::kHoverOverlay : style.background
        : state.disabled && !highlighted ? theme::kSurface
          : state.pressed && !state.disabled ? theme::kAccentPressed
          : highlighted ? theme::kAccent : state.hovered ? theme::kHoverOverlay : style.background;
    const auto brush = CreateSolidBrush(background);
    FillRect(dc, &rect, brush); DeleteObject(brush);
    if (style.border) {
        const auto border = CreateSolidBrush(style.selectedBorder && state.selected ? theme::kAccentHover : theme::kDivider);
        FrameRect(dc, &rect, border); DeleteObject(border);
    }
    RECT content = rect;
    InflateRect(&content, -MulDiv(style.padding, dpi, 96), 0);
    if (state.pressed && !state.disabled) OffsetRect(&content, 1, 1);
    const auto previous = SelectObject(dc, font);
    const auto oldColor = SetTextColor(dc, state.disabled && (!highlighted || style.segment) ? theme::kTextMuted : style.foreground);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    std::wstring text = caption;
    const auto newline = text.find(L'\n');
    const UINT flags = style.alignment | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
    if (newline != std::wstring::npos) {
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        const int top = (content.top + content.bottom - 2 * metrics.tmHeight) / 2;
        RECT first{content.left, top, content.right, top + metrics.tmHeight};
        RECT second{content.left, top + metrics.tmHeight, content.right, top + 2 * metrics.tmHeight};
        ::mc::gui::localizedDrawText(dc, text.c_str(), static_cast<int>(newline), &first, flags);
        ::mc::gui::localizedDrawText(dc, text.c_str() + newline + 1, -1, &second, flags);
    } else ::mc::gui::localizedDrawText(dc, text.c_str(), -1, &content, flags | DT_VCENTER);
    SetBkMode(dc, oldMode); SetTextColor(dc, oldColor); SelectObject(dc, previous);
    if (state.focused && style.focusIndicator) {
        const int inset = MulDiv(3, dpi, 96); InflateRect(&rect, -inset, -inset); DrawFocusRect(dc, &rect);
    }
}
inline void drawButton(const DRAWITEMSTRUCT& item, HFONT font, UINT dpi, bool selected = false, ButtonStyle style = {}) {
    // Native captions may already be localized by the window adapter. Render
    // the retained source exactly once, preserving contextual OEM mode names.
    const auto caption=::mc::gui::localization::sourceCaption(item.hwndItem);
    drawButton(item.hDC, item.rcItem, caption.c_str(), font, dpi,
        {(item.itemState & ODS_DISABLED) != 0, selected, (item.itemState & ODS_SELECTED) != 0,
         GetPropW(item.hwndItem, L"MC.Hover") != nullptr, (item.itemState & ODS_FOCUS) != 0}, style);
}
// The same hover lifecycle for every native owner-drawn action button.
inline LRESULT CALLBACK buttonProcedure(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    // BS_OWNERDRAW paints an opaque rectangle in WM_DRAWITEM. Letting BUTTON
    // erase first briefly exposes the system BTNFACE colour between tab frames.
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_MOUSEMOVE && !GetPropW(window, L"MC.Hover")) {
        SetPropW(window, L"MC.Hover", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
        InvalidateRect(window, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE) {
        RemovePropW(window, L"MC.Hover"); InvalidateRect(window, nullptr, FALSE);
    } else if (msg == WM_NCDESTROY) RemovePropW(window, L"MC.Hover");
    return DefSubclassProc(window, msg, wp, lp);
}
inline void themeButton(HWND window) { SetWindowSubclass(window, buttonProcedure, 0x4D434254, 0); }

// Native popup navigation/accessibility with our font and common palette.
// One scoped adapter serves both tray submenus and inventory context menus.
class PopupTypography {
    struct Item{HMENU menu{};UINT position{},type{};ULONG_PTR data{};std::wstring text;bool separator{},submenu{};};
    HWND owner_;UINT dpi_;HFONT font_;std::vector<std::unique_ptr<Item>> items_;
    int s(int n)const{return MulDiv(n,dpi_,96);}
    Item* find(ULONG_PTR data){for(auto& i:items_)if(reinterpret_cast<ULONG_PTR>(i.get())==data)return i.get();return nullptr;}
    void attach(HMENU menu){for(int n=0;n<GetMenuItemCount(menu);++n){
        MENUITEMINFOW m{sizeof(m)};m.fMask=MIIM_FTYPE|MIIM_DATA|MIIM_STRING|MIIM_SUBMENU;
        if(!GetMenuItemInfoW(menu,n,TRUE,&m))continue;
        auto item=std::make_unique<Item>();item->menu=menu;item->position=n;item->type=m.fType;item->data=m.dwItemData;
        item->separator=(m.fType&MFT_SEPARATOR)!=0;item->submenu=m.hSubMenu!=nullptr;
        item->text.resize(m.cch+1);m.dwTypeData=item->text.data();m.cch=static_cast<UINT>(item->text.size());
        GetMenuItemInfoW(menu,n,TRUE,&m);item->text.resize(m.cch);
        const auto sub=m.hSubMenu;m.fMask=MIIM_FTYPE|MIIM_DATA;m.fType=item->type|MFT_OWNERDRAW;
        m.dwItemData=reinterpret_cast<ULONG_PTR>(item.get());SetMenuItemInfoW(menu,n,TRUE,&m);
        items_.push_back(std::move(item));if(sub)attach(sub);
    }}
    static LRESULT CALLBACK procedure(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<PopupTypography*>(data);
        if(msg==WM_MEASUREITEM){auto* m=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if(m&&m->CtlType==ODT_MENU)if(auto* i=self.find(m->itemData)){
                HDC dc=GetDC(w);auto old=SelectObject(dc,self.font_);SIZE size{};TEXTMETRICW tm{};
                GetTextExtentPoint32W(dc,i->text.c_str(),static_cast<int>(i->text.size()),&size);GetTextMetricsW(dc,&tm);
                SelectObject(dc,old);ReleaseDC(w,dc);m->itemWidth=size.cx+self.s(62);
                m->itemHeight=i->separator?self.s(9):std::max(self.s(26),static_cast<int>(tm.tmHeight)+self.s(10));return TRUE;}}
        if(msg==WM_DRAWITEM){auto* d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if(d&&d->CtlType==ODT_MENU)if(auto* i=self.find(d->itemData)){
                const int saved=SaveDC(d->hDC);const auto color=(d->itemState&ODS_SELECTED)?theme::kAccent:theme::kSurface;
                SetDCBrushColor(d->hDC,color);FillRect(d->hDC,&d->rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                RECT r=d->rcItem;const int cy=(r.top+r.bottom)/2;
                auto pen=CreatePen(PS_SOLID,std::max(1,self.s(1)),theme::kTextSecondary);SelectObject(d->hDC,pen);
                if(i->separator){MoveToEx(d->hDC,r.left+self.s(8),cy,nullptr);LineTo(d->hDC,r.right-self.s(8),cy);}
                else{const bool disabled=(d->itemState&(ODS_DISABLED|ODS_GRAYED))!=0;
                    SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,disabled?theme::kTextMuted:theme::kTextPrimary);SelectObject(d->hDC,self.font_);
                    r.left+=self.s(28);r.right-=self.s(26);DrawTextW(d->hDC,i->text.c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_EXPANDTABS|DT_NOPREFIX);
                    if(d->itemState&ODS_CHECKED){const int x=d->rcItem.left+self.s(9);POINT p[]{{x,cy},{x+self.s(4),cy+self.s(4)},{x+self.s(11),cy-self.s(5)}};Polyline(d->hDC,p,3);}
                    if(i->submenu){const int x=d->rcItem.right-self.s(13);POINT p[]{{x-self.s(3),cy-self.s(4)},{x+self.s(1),cy},{x-self.s(3),cy+self.s(4)}};Polyline(d->hDC,p,3);}}
                RestoreDC(d->hDC,saved);DeleteObject(pen);return TRUE;}}
        return DefSubclassProc(w,msg,wp,lp);
    }
public:
    PopupTypography(HWND owner,HMENU menu):owner_(owner),dpi_(GetDpiForWindow(owner)),font_(createUiFont(ui::FontRole::ClassicBody,dpi_)){
        SetWindowSubclass(owner_,procedure,0x4D434D46,reinterpret_cast<DWORD_PTR>(this));attach(menu);
    }
    ~PopupTypography(){RemoveWindowSubclass(owner_,procedure,0x4D434D46);
        for(auto& i:items_){MENUITEMINFOW m{sizeof(m)};m.fMask=MIIM_FTYPE|MIIM_DATA;m.fType=i->type;m.dwItemData=i->data;SetMenuItemInfoW(i->menu,i->position,TRUE,&m);}DeleteObject(font_);}
};
inline UINT trackPopup(HMENU menu,UINT flags,int x,int y,HWND owner){
    audit::Scope trace("ui.popup_menu");PopupTypography font(owner,menu);const auto result=TrackPopupMenuEx(menu,flags|TPM_RETURNCMD,x,y,owner,nullptr);
    wchar_t caption[512]{};if(result)GetMenuStringW(menu,result,caption,512,MF_BYCOMMAND);
    if(result)audit::input("windows.popup","menu_selected","\"command_id\":"+std::to_string(result)+",\"caption\":"+audit::quote(mc::windows::encoding::utf8(caption)));
    trace.finish(result?"selected":"cancelled","\"command_id\":"+std::to_string(result));return result;
}
inline BOOL chooseColor(CHOOSECOLORW& dialog){
    struct Fonts {HFONT font{};UINT dpi{};~Fonts(){if(font)DeleteObject(font);}
        void apply(HWND w,UINT next){if(font&&next==dpi)return;dpi=next;auto old=font;
            font=createUiFont(ui::FontRole::ClassicBody,dpi);
            EnumChildWindows(w,+[](HWND child,LPARAM value)->BOOL{SendMessageW(child,WM_SETFONT,static_cast<WPARAM>(value),TRUE);return TRUE;},reinterpret_cast<LPARAM>(font));
            if(old)DeleteObject(old);}} fonts;
    dialog.Flags|=CC_ENABLEHOOK;dialog.lCustData=reinterpret_cast<LPARAM>(&fonts);
    dialog.lpfnHook=+[](HWND w,UINT msg,WPARAM wp,LPARAM lp)->UINT_PTR{
        if(msg==WM_INITDIALOG){auto* f=reinterpret_cast<Fonts*>(reinterpret_cast<CHOOSECOLORW*>(lp)->lCustData);
            SetPropW(w,L"MC.Color.Font",f);f->apply(w,GetDpiForWindow(w));}
        else if(msg==WM_DPICHANGED){if(auto* f=static_cast<Fonts*>(GetPropW(w,L"MC.Color.Font")))f->apply(w,HIWORD(wp));}
        else if(msg==WM_NCDESTROY)RemovePropW(w,L"MC.Color.Font");
        return 0;};
    audit::Scope trace("ui.color_dialog","\"initial_rgb\":"+std::to_string(dialog.rgbResult));
    const auto result=ChooseColorW(&dialog);trace.finish(result?"selected":"cancelled","\"rgb\":"+std::to_string(dialog.rgbResult)+",\"dialog_error\":"+std::to_string(result?0:CommDlgExtendedError()));return result;
}
}
