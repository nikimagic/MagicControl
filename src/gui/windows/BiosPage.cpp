#include "MagicControl/ActionLog.h"
#include "MagicControl/OperationMessages.h"
#include "LocalizationWindows.h"
#include "ControlTheme.h"
#include "PageMechanics.h"
#include "BiosPage.h"
#include "OsdPopup.h"
#include "../common/DiagnosticDumpOsd.h"
#include "../common/DiagnosticPrompts.h"
#include "MagicControl/PowerInputDump.h"
#include "MessageDialog.h"
#include "../common/DeviceHeader.h"
#include "MagicControl/Logging.h"
#include "MagicControl/DiagnosticDump.h"
#include "MagicControl/BiosRomDump.h"
#include "MagicControl/VbiosRomDump.h"
#include "Layout.h"
#include "../common/Theme.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/BiosSupport.h"
#include "MagicControl/PawnIoInstall.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/RomIdHistory.h"
#include "../../platform/windows/InfoOperationsWindows.h"
#include "../common/BiosActions.h"
#include "../common/InventoryPresentation.h"
#include "MagicControl/ScrollModel.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <format>
#include <vector>
#include <stdexcept>

namespace mc::gui {
namespace {
std::wstring wide(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(std::max(0, count)), L'\0');
    if (count > 0) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                      static_cast<int>(text.size()), result.data(), count);
    return result;
}
void fill(HDC dc, RECT rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
constexpr UINT textFlags = DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;
int textHeight(HDC dc, const std::wstring& text, HFONT font, int width) {
    const auto previous = SelectObject(dc, font);
    RECT rect{0, 0, std::max(1, width), 0};
    ::mc::gui::localizedDrawText(dc, text.c_str(), -1, &rect, textFlags | DT_CALCRECT);
    SelectObject(dc, previous);
    return rect.bottom;
}
void text(HDC dc, const std::wstring& value, RECT rect, HFONT font, COLORREF color,
          UINT flags = textFlags) {
    const auto previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    ::mc::gui::localizedDrawText(dc, value.c_str(), -1, &rect, flags);
    SelectObject(dc, previous);
}
void clipboard(HWND owner, const std::wstring& value) {
    if (!OpenClipboard(owner)) return;
    const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            memcpy(target, value.c_str(), bytes);
            GlobalUnlock(memory);
            if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) memory = nullptr;
        }
        if (memory) GlobalFree(memory);
    }
    CloseClipboard();
}
bool openBrowserUrl(HWND source, const wchar_t* url) {
    const HWND owner = GetAncestor(source, GA_ROOT);
    HINSTANCE result = ShellExecuteW(owner, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) return true;
    // Some desktop shells reject a child window as the owner. Retry without an
    // owner so the user's default browser still receives the URL.
    result = ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}
// A viewport-sized buffer, reused across paints. No bitmap of the entire report.
using page::Buffer;
}

struct BiosPage::Impl {
    struct Row { RECT caption{}, value{}; std::wstring name, contents; int index{}; };
    struct Card { RECT bounds{}, heading{}; std::wstring title; std::vector<Row> rows; };
    HWND page{}, view{}, heading{}, snapshotLabel{}, updateLabel{};
    HWND buttons[ui::kBiosActionCount]{};HWND energyButton{};
    page::RegionTooltips actionTooltips;
    std::shared_ptr<mc::EcAccess> ec;
    MaximumFanProbe maximumProbe;
    FirmwareFanDefault firmwareDefault;
    std::shared_ptr<PowerInputDumpSession> powerSession;
    OsdPopup* popup{};
    UINT dpi{96};unsigned fontLanguage{~0u};
    int width{}, height{}, measuredWidth{};unsigned measuredLanguage{~0u};
    ui::Rect headerCard{};
    HFONT body{}, bold{};
    HBRUSH surfaceBrush{CreateSolidBrush(theme::kSurface)};
    HardwareInventory inventory;
    const bool uniwillIdentity=uniwillIdentityHistoryProfile(deviceProfile(readBoardDescriptor()));
    BiosSupportLinks links{biosSupportLinks(ui::presentationBoard())};
    int supportModel{-2};
    std::wstring snapshot{L"Сохранённых сведений ещё нет. Нажмите «Обновить сведения»."};
    std::wstring update{wide(ui::kPawnIoWindowsNote)};
    std::vector<Card> cards;
    std::shared_ptr<InfoOperation> job;
    std::uint64_t shownProgress{};
    ScrollModel scroll;
    Buffer buffer;
    bool loaded{}, visible{}, hot{}, dragging{};
    int grabOffset{}, selected{-1};
    static constexpr bool kShowScrollbar = true;

    int s(int logical) const { return MulDiv(logical, static_cast<int>(dpi), 96); }
    // Keep a narrow, page-coloured lane outside the card surface.  The thumb
    // is centred in this lane and never paints over inventory text.
    int scrollLane() const { return s(12); }
    explicit Impl(HWND parent,std::shared_ptr<mc::EcAccess> access,MaximumFanProbe probe,FirmwareFanDefault restore,OsdPopup* notifications):ec(std::move(access)),maximumProbe(std::move(probe)),firmwareDefault(std::move(restore)),popup(notifications) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = procedure;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MagicControl.BiosPage";
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassW(&wc);
        page = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, wide(ui::kInformationCaption).c_str(),
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, wc.hInstance, this);
        if (!page) throw std::runtime_error("Cannot create BIOS page");
        heading = label(wide(ui::kInformationCaption).c_str());
        const std::wstring titles[] = {L"Обновить сведения", wide(ui::kPawnIoInstallCaption),
            wide(links.supportCaption), L"Nahimic · обновления", L"Копировать отчёт",wide(ui::kDiagnosticDumpCaption),wide(ui::kBiosRomDumpCaption),wide(ui::kVbiosRomDumpCaption)};
        for (const int i : ui::kBiosActionTabOrder) {
            buttons[i] = CreateWindowExW(0, L"BUTTON", titles[i].c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                page, reinterpret_cast<HMENU>(static_cast<INT_PTR>(100 + i)), wc.hInstance, nullptr);
            SetWindowSubclass(buttons[i], control::buttonProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        energyButton=CreateWindowExW(0,L"BUTTON",L"Установить AcpiVpc.sys",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,
            page,reinterpret_cast<HMENU>(static_cast<INT_PTR>(108)),wc.hInstance,nullptr);
        SetWindowSubclass(energyButton,control::buttonProcedure,1,reinterpret_cast<DWORD_PTR>(this));
        EnableWindow(buttons[1],ui::biosDeviceActionsAllowed());
        EnableWindow(buttons[2],links.supportUrl!=nullptr);
        EnableWindow(buttons[7],ui::biosDeviceActionsAllowed()&&vbiosRomDumpAvailable());
        snapshotLabel = label(snapshot.c_str());
        updateLabel = label(update.c_str());
        view = CreateWindowExW(0, L"STATIC", L"Сведения о ноутбуке",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_NOTIFY, 0, 0, 0, 0, page,
            reinterpret_cast<HMENU>(200), wc.hInstance, nullptr);
        SetWindowSubclass(view, viewProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        refreshProfile();
        fonts();
    }
    ~Impl() {
        if (job) job->cancel();
        if(job)job->releaseHardware(); // Wait for fan restoration, not WMI.
        if (IsWindow(page)) DestroyWindow(page);
        if (body) DeleteObject(body);
        if (bold) DeleteObject(bold);
        DeleteObject(surfaceBrush);
    }
    HWND label(const wchar_t* title) {
        return CreateWindowExW(0, L"STATIC", title, WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
                              0, 0, 0, 0, page, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    void fonts() {
        fontLanguage=ui::languageIndex();
        if (body) DeleteObject(body);
        if (bold) DeleteObject(bold);
        body = createUiFont(ui::FontRole::ClassicBody, dpi);
        bold = createUiFont(ui::FontRole::ClassicHeading, dpi);
        for (HWND control : {snapshotLabel, updateLabel})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(bold), TRUE);
        SendMessageW(view, WM_SETFONT, reinterpret_cast<WPARAM>(body), FALSE);
        measuredWidth = 0;
    }
    void refreshPawnIoCaption() {
        auto action=dependencies::PawnIoSetupAction::UnknownVersion;
        dependencies::PawnIoDriverStatus driver;
        try{driver=dependencies::pawnIoDriverStatus(dependencies::PawnIoVersionQuery::LoadedDriver);action=dependencies::pawnIoSetupAction(driver);}catch(const std::exception&){}
        std::string caption=ui::kPawnIoInstallCaption;
        if(action==dependencies::PawnIoSetupAction::Upgrade)caption="Драйвер PawnIO "+std::string(driver.version())+" → "+dependencies::bundledPawnIoVersion;
        else if(action==dependencies::PawnIoSetupAction::Repair)caption="Восстановить драйвер PawnIO";
        else if(action==dependencies::PawnIoSetupAction::Current)caption=ui::kPawnIoInstalledCaption;
        else if(action==dependencies::PawnIoSetupAction::UnknownVersion)caption="Версия PawnIO неизвестна";
        if((action==dependencies::PawnIoSetupAction::Current||action==dependencies::PawnIoSetupAction::UnknownVersion)&&!driver.version().empty())
            caption="Драйвер PawnIO "+std::string(driver.version());
        SetWindowTextW(buttons[1],wide(caption).c_str());
        EnableWindow(buttons[1],action!=dependencies::PawnIoSetupAction::Current&&action!=dependencies::PawnIoSetupAction::UnknownVersion&&!job&&ui::biosDeviceActionsAllowed());
    }
    bool actualLenovo()const{return !ui::laptopPreview()&&deviceProfile(readBoardDescriptor()).id=="lenovo-legion-kwcn";}
    void refreshEnergyCaption(){
        bool show=false,enable=false;std::string caption="AcpiVpc.sys: версия неизвестна";
        try{show=deviceProfile(ui::presentationBoard()).id=="lenovo-legion-kwcn";if(show&&!actualLenovo())caption="AcpiVpc.sys · просмотр";
        else if(show){const auto driver=dependencies::lenovoEnergyDriverStatus();
            if(!driver.installed){caption="Установить AcpiVpc.sys";enable=true;}
            else if(!driver.fileVersion.empty())caption="AcpiVpc.sys "+driver.fileVersion;}}
        catch(const std::exception&){enable=false;}
        SetWindowTextW(energyButton,wide(caption).c_str());ShowWindow(energyButton,show?SW_SHOWNA:SW_HIDE);
        EnableWindow(energyButton,show&&enable&&!job&&ui::biosDeviceActionsAllowed());
    }
    void refreshProfile() {
        const int model = ui::selectedLaptopIndex();
        if (model == supportModel) return;
        supportModel = model;
        if (job && !job->inventory()) {
            job->cancel();
            powerSession.reset(); // Abandoned worker owns its old session; a new dump cannot share its mutable progress.
            job->releaseHardware();
            job.reset(); KillTimer(page, 1);
        }
        links = biosSupportLinks(ui::presentationBoard());
        refreshPawnIoCaption();
        refreshEnergyCaption();
        SetWindowTextW(buttons[2], wide(links.supportCaption).c_str());

        EnableWindow(buttons[0],!job);
        EnableWindow(buttons[5],!job&&ui::biosDeviceActionsAllowed());
        SetWindowTextW(buttons[6],wide(ui::kBiosRomDumpCaption).c_str());
        EnableWindow(buttons[6],!job&&ui::biosDeviceActionsAllowed()&&biosRomDumpAvailable());
        refreshVbiosButton();
        EnableWindow(buttons[2], links.supportUrl != nullptr);
        update = wide(ui::kPawnIoWindowsNote);
        if(ui::noNameModel()) update=ui::laptopPreview()?L"Unknown: установка PawnIO и дамп относятся к текущему компьютеру. Дамп — без проб вентиляторов.":L"Unknown: модель не распознана. Доступны установка PawnIO и диагностический дамп текущего компьютера.";
        else if (ui::laptopPreview()) update = L"Кнопки INFO и сведения относятся к текущему компьютеру, независимо от выбранной модели. Пробы вентиляторов отключены.";
        refreshLabels();
    }
    void load() {
        if (loaded) return;
        loaded = true;
        if (auto cached = InventoryCache::load()) {
            inventory = std::move(*cached);
            snapshot = L"Сохранённые сведения от " + wide(inventory.readAt) +
                       L". Обновление — только по кнопке.";
        }
        refreshLabels();
    }
    void refreshLabels() {
        SetWindowTextW(snapshotLabel, snapshot.c_str());
        SetWindowTextW(updateLabel, update.c_str());
        SetWindowTextW(view, report().c_str());
        InvalidateRect(view, nullptr, FALSE);
        EnableWindow(buttons[4], !inventory.empty());
    }
    void refreshVbiosButton(){
        const bool active=job&&(job->operation()==InfoResourceOperation::VbiosDump);
        SetWindowTextW(buttons[7],wide(active?"Отмена VBIOS":ui::kVbiosRomDumpCaption).c_str());
        EnableWindow(buttons[7],active||(!job&&ui::biosDeviceActionsAllowed()&&vbiosRomDumpAvailable()));
    }
    // КОНТРАКТ КАРТОЧЕК И ПРОКРУТКИ BIOS (16.10.2026).
    // Сохранить существующий рабочий механизм: верхняя карточка/кнопки неподвижны,
    // список inventory прокручивается внутри собственного view. Не превращать
    // его в прокрутку всего page и не перемещать шапку вместе со списком.
    // biosHeaderGeometry получает полную ширину; view уже содержит боковые
    // отступы страницы. В measure() не вычитать их второй раз: это сужает карточки
    // списка относительно верхней карточки. Высоты текста измеряются с реальным
    // шрифтом, DPI и доступной шириной; extent включает весь список и нижний отступ.
    // Thumb рассчитывается по видимой области списка. Сохранять существующие
    // преобразования координат view/page для рисования и hit-test полосы.
    // invalidateViewAndScroll обновляет и список, и отдельную область полосы;
    // фон рисуется также в промежутках и боковых полосах, без старых следов.
    // Не менять этот механизм при правках SETs и не автоскрывать рабочий скролл.
    void arrange() {
        if (!visible || width < s(120) || height < s(120)) return;
        refreshVbiosButton();
        HDC dc = GetDC(page);
        const int inner = width - 4 * s(12);
        std::array<int, 5> actionWidths{};
        const auto oldFont = SelectObject(dc, body);
        for (std::size_t i = 0; i < actionWidths.size(); ++i) {
            wchar_t caption[256]{};
            GetWindowTextW(buttons[ui::kBiosPrimaryActions[i]],caption,256);
            RECT measured{};
            localizedDrawText(dc,caption,-1,&measured,DT_SINGLELINE|DT_NOPREFIX|DT_CALCRECT);
            actionWidths[i] = measured.right;
        }
        if(GetWindowLongPtrW(energyButton,GWL_STYLE)&WS_VISIBLE){wchar_t caption[256]{};GetWindowTextW(energyButton,caption,256);
            RECT measured{};localizedDrawText(dc,caption,-1,&measured,DT_SINGLELINE|DT_NOPREFIX|DT_CALCRECT);actionWidths[0]+=measured.right+s(12);}
        SelectObject(dc,oldFont);
        const auto g = ui::biosHeaderGeometry(width, height,
            textHeight(dc, snapshot, body, inner), textHeight(dc, update, body, inner),
            {static_cast<int>(dpi)},actionWidths);
        ReleaseDC(page, dc);
        const auto place = [](HWND control, ui::Rect r) {
            page::place(control, r.left, r.top, r.width(), r.height());
        };
        place(heading, g.heading);
        place(snapshotLabel, g.snapshot);
        place(updateLabel, g.update);
        for (std::size_t i = 0; i < ui::kBiosActionCount; ++i) place(buttons[i], g.buttons[i]);
        if(GetWindowLongPtrW(energyButton,GWL_STYLE)&WS_VISIBLE){auto r=g.buttons[1];wchar_t caption[256]{};GetWindowTextW(energyButton,caption,256);
            HDC measureDc=GetDC(page);auto old=SelectObject(measureDc,body);RECT measured{};localizedDrawText(measureDc,caption,-1,&measured,DT_SINGLELINE|DT_NOPREFIX|DT_CALCRECT);SelectObject(measureDc,old);ReleaseDC(page,measureDc);
            const int available=r.right-r.left-s(6);const int energyWidth=std::clamp(static_cast<int>(measured.right)+s(20),available/2,available*2/3);const int mid=r.right-energyWidth-s(6);
            place(buttons[1],{r.left,r.top,mid,r.bottom});place(energyButton,{mid+s(6),r.top,r.right,r.bottom});}
        actionTooltips.update(page,{{g.buttons[7],ui::translate(ui::kVbiosRomDumpNote)}},body);
        headerCard = g.card;
        place(view, g.inventory);
        measure();
        InvalidateRect(page, nullptr, FALSE);
    }
    void measure() {
        RECT client{}; GetClientRect(view, &client);
        const int w = client.right, h = client.bottom;
        if (w < s(96) || h < 1 || !visible) return;
        scroll.setViewport(h);
        if (w == measuredWidth&&measuredLanguage==ui::languageIndex()) return;
        measuredLanguage=ui::languageIndex();
        const auto started = std::chrono::steady_clock::now();
        measuredWidth = w; cards.clear();
        HDC dc = GetDC(view);
        // `view` already has the same left/right page padding as the header.
        // Do not subtract another gutter here: that made the inventory cards
        // narrower than the correct top card.
        const int cardWidth = w;
        const int inner = cardWidth - 2 * s(12);
        const int column = inner * 28 / 100, gap = s(12);
        int y = 0, index = 0;
        const auto information=ui::informationInventory(inventory,uniwillIdentity);
        for (const auto& section : information.sections) {
            Card card; card.title = wide(section.title);
            const int headingHeight = textHeight(dc, card.title, bold, inner);
            std::vector<int> heights;
            for (const auto& entry : section.rows) {
                Row row; row.name = wide(entry.name); row.contents = wide(entry.value); row.index = index++;
                const int rowHeight = std::max(textHeight(dc, row.name, body, column - gap),
                                              textHeight(dc, row.contents, body, inner - column));
                heights.push_back(rowHeight);
                card.rows.push_back(std::move(row));
            }
            const auto geometry = ui::inventoryCardGeometry(cardWidth, y, headingHeight,
                heights, {static_cast<int>(dpi)});
            card.heading = toNativeRect(geometry.heading);
            card.bounds = toNativeRect(geometry.bounds);
            for (std::size_t i = 0; i < card.rows.size(); ++i) {
                card.rows[i].caption = toNativeRect(geometry.captions[i]);
                card.rows[i].value = toNativeRect(geometry.values[i]);
            }
            y = card.bounds.bottom + s(6);
            cards.push_back(std::move(card));
        }
        ReleaseDC(view, dc);
        scroll.setExtent(std::max(0, y - s(6)));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        OutputDebugStringW(std::format(L"MagicControl BIOS layout: {} rows, {} px, {:.2f} ms\n",
                                      index, w, ms).c_str());
    }
    RECT thumb() const {
        RECT client{}; GetClientRect(view, &client);
        return page::scrollThumb(client.right, client.bottom, scroll, dpi, hot || GetFocus() == view, dragging);
    }
    RECT pageThumb() const {
        RECT result = thumb();
        POINT origin{0, 0};
        ClientToScreen(view, &origin);
        ScreenToClient(page, &origin);
        OffsetRect(&result, origin.x, origin.y);
        RECT client{}; GetClientRect(page, &client);
        const int thickness = result.right - result.left;
        const int center = client.right - scrollLane() / 2;
        result.left = center - thickness / 2;
        result.right = result.left + thickness;
        return result;
    }
    void drawThumb(HDC dc) const {
        page::drawThumb(dc, pageThumb(), hot || GetFocus() == view, dragging);
    }
    void invalidateScroll() {
        if (!kShowScrollbar) return;
        RECT client{}; GetClientRect(page, &client);
        client.left = std::max<LONG>(0, client.right - scrollLane() - s(2));
        InvalidateRect(page, &client, FALSE);
    }
    void invalidateViewAndScroll() {
        InvalidateRect(view, nullptr, FALSE);
        invalidateScroll();
    }
    void paint() {
        page::Paint painting(view, buffer);
        if (!painting.valid()) return;
        const HDC dc = painting.dc();
        const auto& client = painting.client();
        const auto& ps = painting.info();
        measure();
        fill(dc, client, theme::kBackground); // Includes gaps and both side strips.
        if (cards.empty()) {
            RECT empty{s(12), s(16), client.right - s(28), client.bottom};
            text(dc, L"Сведения ещё не собраны. Нажмите «Обновить сведения».\n"
                     L"Сбор выполняется в фоне; настройки ноутбука не меняются.", empty, body, theme::kTextSecondary);
        }
        for (const auto& card : cards) {
            RECT bounds = card.bounds; OffsetRect(&bounds, 0, -scroll.position());
            if (bounds.bottom < ps.rcPaint.top || bounds.top > ps.rcPaint.bottom) continue;
            fill(dc, bounds, theme::kSurface);
            RECT headingRect = card.heading; OffsetRect(&headingRect, 0, -scroll.position());
            text(dc, card.title, headingRect, bold, theme::kTextPrimary);
            for (const auto& row : card.rows) {
                RECT caption = row.caption, value = row.value;
                OffsetRect(&caption, 0, -scroll.position()); OffsetRect(&value, 0, -scroll.position());
                if (value.bottom < ps.rcPaint.top || value.top > ps.rcPaint.bottom) continue;
                if (row.index == selected) {
                    RECT highlight{s(5), value.top, bounds.right - s(5), value.bottom};
                    fill(dc, highlight, theme::kHoverOverlay);
                }
                text(dc, row.name, caption, body, theme::kTextSecondary);
                text(dc, row.contents, value, body, theme::kTextPrimary);
            }
        }
        if (GetFocus() == view) {
            RECT focus = client; InflateRect(&focus, -1, -1); DrawFocusRect(dc, &focus);
        }
        painting.finish();
    }
    void wheel(int delta) {
        if (page::wheel(scroll, delta, dpi)) invalidateViewAndScroll();
    }
    std::wstring report() const {
        return wide(ui::informationReport(inventory,"\r\n",uniwillIdentity));
    }
    void copy(bool valueOnly) const {
        for (const auto& card : cards) for (const auto& row : card.rows) if (row.index == selected) {
            clipboard(page, valueOnly ? row.contents : row.name + L": " + row.contents); return;
        }
        clipboard(page, report());
    }
    void select(int x, int y) {
        selected = -1;
        for (const auto& card : cards) for (const auto& row : card.rows)
            if (x >= row.caption.left && x < row.value.right &&
                y + scroll.position() >= row.value.top && y + scroll.position() < row.value.bottom)
                selected = row.index;
        InvalidateRect(view, nullptr, FALSE);
    }
    void start(InfoResourceOperation action) {
        const bool collection=action==InfoResourceOperation::Inventory;
        const bool biosRom=action==InfoResourceOperation::BiosDump,vbiosRom=action==InfoResourceOperation::VbiosDump;
        const bool dumping=action==InfoResourceOperation::EcDump||biosRom||vbiosRom;
        if (job || (!collection && !biosRom && !ui::biosDeviceActionsAllowed())) return;
        if(biosRom&&!biosRomDumpAvailable())return;
        std::string vbiosPci;
        if(vbiosRom){
            if(!ui::biosDeviceActionsAllowed()||!vbiosRomDumpAvailable())return;
            try{
                const auto adapters=vbiosDumpAdapters();
                if(adapters.size()>1){
                    std::vector<std::wstring> choices;for(const auto& a:adapters)choices.push_back(wide(a.name+" · "+vbiosAdapterPci(a)));
                    const auto selected=showChoiceDialog(GetAncestor(page,GA_ROOT),dpi,wide(ui::translate("Выберите NVIDIA для дампа VBIOS")),choices);
                    if(selected<0||static_cast<std::size_t>(selected)>=adapters.size())return;
                    vbiosPci=vbiosAdapterPci(adapters[static_cast<std::size_t>(selected)]);
                }else if(adapters.size()==1)vbiosPci=vbiosAdapterPci(adapters.front());
            }catch(const std::exception& e){update=wide(e.what());refreshLabels();arrange();return;}
        }
        bool guided=false;
        if(dumping&&!biosRom&&!vbiosRom&&powerInputDumpSupported()){
            const auto kind=showChoiceDialog(GetAncestor(page,GA_ROOT),dpi,wide(ui::translate(powerInputChoiceText)),
                {L"Обычный дамп",L"Проверка БП",L"Отмена"});
            if(kind<0||kind==2)return;guided=kind==1;
            if(guided&&(!powerSession||powerSession->nextStage==3))powerSession=std::make_shared<PowerInputDumpSession>();
        }
        const auto dumpMode=guided?DiagnosticDumpMode::ReadOnly:ui::biosFanProbesAllowed()?DiagnosticDumpMode::WithFanProbes:DiagnosticDumpMode::ReadOnly;
        InfoOperationRequest request;request.operation=guided?InfoResourceOperation::PowerInputDump:action;
        request.auditId=audit::capture();request.powerSession=powerSession;
        request.dump.pci=std::move(vbiosPci);request.dump.access=ec;request.dump.maximumProbe=maximumProbe;
        request.dump.firmwareDefault=firmwareDefault;request.dump.mode=dumpMode;
        job=InfoOperation::start(std::move(request),windows::infoOperationServices(action));shownProgress=0;
        const auto task=job;
        if (collection) snapshot = L"Собираю сведения… Можно прокручивать страницу и переключать вкладки.";
        else if(vbiosRom)update=L"NVIDIA: VBIOS и EEPROM, по два чтения; ёмкость, SHA256 и raw compare. Результат: logs_<плата>/vbios-dumps. Отмена — после текущего чтения.";
        else if(biosRom)update=L"BIOS: проверяю оборудование и выбираю совместимый ридер. Два прохода; результат в logs_<плата>/bios-dumps. Отмена — после текущего чтения.";
        else if(dumping)update=dumpMode==DiagnosticDumpMode::ReadOnly?L"Дамп EC и минимум 60 сек датчиков без проб вентиляторов… Результат: logs_<плата>/ec-dumps.":L"Default → EC → минимум 60 сек датчиков, Max по 30 сек → возврат FANs. Результат: logs_<плата>/ec-dumps.";
        else update = action==InfoResourceOperation::LenovoEnergyPrepare?L"Подготавливаю официальный установщик Lenovo Energy и проверяю подпись…":L"Подготавливаю встроенный установщик PawnIO2.2.0 и проверяю его подпись…";
        refreshLabels(); arrange();
        showOperationDialog(GetAncestor(page,GA_ROOT),dpi,collection?L"MagicControl · Сведения":vbiosRom?L"MagicControl · VBIOS / EEPROM":biosRom?L"MagicControl · BIOS":dumping?L"MagicControl · Дамп EC":action==InfoResourceOperation::LenovoEnergyPrepare?L"MagicControl · Lenovo Energy":L"MagicControl · Установка PawnIO",task->messages(),[weak=std::weak_ptr<InfoOperation>(task)]{if(auto p=weak.lock())p->cancel();});
        EnableWindow(buttons[0], FALSE); EnableWindow(buttons[1], FALSE);EnableWindow(energyButton,FALSE);EnableWindow(buttons[5],FALSE);EnableWindow(buttons[6],biosRom);if(biosRom)SetWindowTextW(buttons[6],wide("Отмена BIOS").c_str());
        refreshVbiosButton();
        SetTimer(page, 1, 50, nullptr);
        if(dumping&&!biosRom&&!vbiosRom&&popup)popup->show(osd::diagnosticDumpStarted(),GetAncestor(page,GA_ROOT));
    }
    void complete() {
        if(!job)return;
        const auto task=job;
        if(const auto prompt=task->prompts.take()){
            const auto owner=GetAncestor(page,GA_ROOT);const auto currentDpi=dpi;
            const auto text=wide(diagnosticPromptText(prompt->instructions,prompt->reason));
            task->messages()->append(mc::windows::encoding::utf8(text));
            const auto choice=showChoiceDialog(owner,currentDpi,text,{L"Снять состояние",L"Отложить"});
            task->prompts.reply(choice==0);return; // No page access after modal owner destruction.
        }
        const auto* result=task->poll();
        if(!result){
            if(task->installing()){
                const std::wstring notice=L"Мастер установки PawnIO открыт. Завершите установку и перезапустите MagicControl.";
                if(update!=notice){update=notice;refreshLabels();arrange();}
            }
            if(task->firmwareDump()){
                const auto done=task->progress();if(done!=shownProgress){shownProgress=done;
                    // Actual bytes across reads, never an invented full-SPI percentage.
                    update=wide(task->operation()==InfoResourceOperation::VbiosDump?
                        std::format("VBIOS / EEPROM: сохранено {} байт (2+2 чтения).",done):
                        std::format("BIOS: сохранено {} байт (2 прохода). Образ и SHA256 проверяются после чтения.",done));refreshLabels();}
            }
            return;
        }
        KillTimer(page,1);
        if(task->inventory()){
            snapshot=wide(result->message);
            if(result->success){inventory=result->inventory;scroll.reset();selected=-1;measuredWidth=0;}
        }else{
            update=wide(result->message);
            if(popup&&task->dumping()&&!task->firmwareDump())
                popup->show(osd::diagnosticDumpFinished(result->dump),GetAncestor(page,GA_ROOT));
        }
        job.reset();
        EnableWindow(buttons[0],TRUE);refreshPawnIoCaption();refreshEnergyCaption();
        EnableWindow(buttons[5],ui::biosDeviceActionsAllowed());
        SetWindowTextW(buttons[6],wide(ui::kBiosRomDumpCaption).c_str());EnableWindow(buttons[6],ui::biosDeviceActionsAllowed()&&biosRomDumpAvailable());
        refreshLabels();arrange();InvalidateRect(view,nullptr,FALSE);
        // The shared operation has already finalised the transcript and audit.
    }
    void drawButton(const DRAWITEMSTRUCT& item) {
        control::ButtonStyle style;
        // All BIOS actions share the reference link-row appearance.
        // An action is not a selected policy: hover/press come from the common theme.
        style.background = theme::kSurface;
        style.foreground = theme::kTextSecondary;
        style.padding = 6;
        control::drawButton(item, body, dpi, false, style);
    }
    static LRESULT CALLBACK viewProcedure(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<Impl*>(data);
        switch (msg) {
        case WM_SETTEXT: {
            // STATIC repaints internally in WM_SETTEXT without sending WM_PAINT.
            // Keep its accessible name, then schedule the custom inventory paint.
            const LRESULT result = DefSubclassProc(window, msg, wp, lp);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE); return result;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: self.paint(); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_MOUSEWHEEL: self.wheel(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
        case WM_KEYDOWN: {
            int position = self.scroll.position();
            switch (wp) {
            case VK_UP: position -= self.s(24); break;
            case VK_DOWN: position += self.s(24); break;
            case VK_PRIOR: position += self.scroll.pageStep(false); break;
            case VK_NEXT: position += self.scroll.pageStep(true); break;
            case VK_HOME: position = 0; break;
            case VK_END: position = self.scroll.maximumPosition(); break;
            case 'C': if (GetKeyState(VK_CONTROL) & 0x8000) self.copy(true); return 0;
            default: return DefSubclassProc(window, msg, wp, lp);
            }
            self.scroll.setPosition(position); self.invalidateViewAndScroll(); return 0;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(window);
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            RECT client{}; GetClientRect(window, &client);
            if (self.kShowScrollbar && self.scroll.scrollable() && x >= client.right - self.scrollLane()) {
                const RECT thumb = self.thumb();
                page::scrollPress(window,self.scroll,thumb,y,self.dragging,self.grabOffset);
                InvalidateRect(window, nullptr, FALSE);
            } else self.select(x, y);
            return 0;
        }
        case WM_LBUTTONUP: if (self.dragging) ReleaseCapture(); return 0;
        case WM_CAPTURECHANGED: self.dragging = false; InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE: {
            RECT client{}; GetClientRect(window, &client);
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (self.dragging) {
                const RECT thumb = self.thumb();
                page::scrollDrag(self.scroll,thumb,client.bottom,self.dpi,y,self.grabOffset);
                InvalidateRect(window, nullptr, FALSE);
            }
            const bool hot = self.kShowScrollbar && x >= client.right - self.scrollLane();
            if (hot != self.hot) { self.hot = hot; InvalidateRect(window, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE: self.hot = false; InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_CONTEXTMENU: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (pt.x != -1) { POINT local = pt; ScreenToClient(window, &local); self.select(local.x, local.y); }
            else { pt = {self.s(24), self.s(24)}; ClientToScreen(window, &pt); }
            HMENU menu = CreatePopupMenu();
            const auto copyValue=localized(L"Копировать значение")+L"\tCtrl+C";
            const auto copyRow=localized(L"Копировать строку");
            const auto copyReport=localized(L"Копировать весь отчёт");
            AppendMenuW(menu, MF_STRING | (self.selected < 0 ? MF_GRAYED : 0), 1, copyValue.c_str());
            AppendMenuW(menu, MF_STRING | (self.selected < 0 ? MF_GRAYED : 0), 2, copyRow.c_str());
            AppendMenuW(menu, MF_STRING, 3, copyReport.c_str());
            const UINT action = control::trackPopup(menu,TPM_RIGHTBUTTON,pt.x,pt.y,window);
            DestroyMenu(menu);
            if (action == 1) self.copy(true); else if (action == 2) self.copy(false);
            else if (action == 3) clipboard(window, self.report());
            return 0;
        }
        }
        return DefSubclassProc(window, msg, wp, lp);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, msg, wp, lp);
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
            RECT client{}; GetClientRect(window, &client); fill(dc, client, theme::kBackground);
            fill(dc, toNativeRect(self->headerCard), theme::kSurface);
            if (self->kShowScrollbar && self->scroll.scrollable()) self->drawThumb(dc);
            EndPaint(window, &ps); return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!self->kShowScrollbar || !self->scroll.scrollable()) break;
            RECT client{}; GetClientRect(window, &client);
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (x < client.right - self->scrollLane()) break;
            const RECT thumb = self->pageThumb();
            page::scrollPress(window,self->scroll,thumb,y,self->dragging,self->grabOffset);
            self->invalidateViewAndScroll();
            return 0;
        }
        case WM_LBUTTONUP:
            if (self->dragging) { ReleaseCapture(); return 0; }
            break;
        case WM_CAPTURECHANGED:
            if (self->dragging) { self->dragging = false; self->invalidateScroll(); }
            return 0;
        case WM_MOUSEMOVE: {
            RECT client{}; GetClientRect(window, &client);
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (self->dragging) {
                POINT origin{0, 0};
                ClientToScreen(self->view, &origin); ScreenToClient(window, &origin);
                const RECT thumb = self->pageThumb();
                RECT viewClient{}; GetClientRect(self->view, &viewClient);
                const int localY = y - origin.y;
                page::scrollDrag(self->scroll,thumb,viewClient.bottom,self->dpi,localY,self->grabOffset);
                self->invalidateViewAndScroll();
            }
            const bool hot = self->kShowScrollbar && x >= client.right - self->scrollLane();
            if (hot != self->hot) { self->hot = hot; self->invalidateScroll(); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self->hot) { self->hot = false; self->invalidateScroll(); }
            return 0;
        case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp) == self->heading ? theme::kTextPrimary : theme::kTextSecondary);
            SetBkColor(reinterpret_cast<HDC>(wp), theme::kSurface);
            return reinterpret_cast<LRESULT>(self->surfaceBrush);
        case WM_DRAWITEM: self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_COMMAND:
            if (HIWORD(wp) != BN_CLICKED) break;
            switch (LOWORD(wp)) {
            case 100: self->start(InfoResourceOperation::Inventory); return 0;
            case 101: self->start(InfoResourceOperation::PawnIoPrepare); return 0;
            case 108: if(self->actualLenovo())self->start(InfoResourceOperation::LenovoEnergyPrepare);return 0;
            case 102:
                if (self->links.supportUrl&&!openBrowserUrl(window, self->links.supportUrl)) {
                    self->update = L"Не удалось открыть официальный сайт поддержки производителя.";
                    self->refreshLabels();
                }
                return 0;
            case 103:
                if (!openBrowserUrl(window, kNahimicUrl)) {
                    self->update = L"Не удалось открыть браузер для обновлений Nahimic.";
                    self->refreshLabels();
                }
                return 0;
            case 104: clipboard(window, self->report()); return 0;
            case 105: self->start(InfoResourceOperation::EcDump);return 0;
            case 106: if(self->job&&(self->job->operation()==InfoResourceOperation::BiosDump))self->job->cancel();else self->start(InfoResourceOperation::BiosDump);return 0;
            case 107: if(self->job&&(self->job->operation()==InfoResourceOperation::VbiosDump))self->job->cancel();else self->start(InfoResourceOperation::VbiosDump);return 0;
            } break;
        case WM_TIMER: self->complete(); return 0;
        case WM_MOUSEWHEEL: self->wheel(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
        case WM_DESTROY:
            if (self->job) self->job->cancel();
            KillTimer(window, 1); return 0;
        }
        return DefWindowProcW(window, msg, wp, lp);
    }
};

BiosPage::BiosPage(HWND parent,std::shared_ptr<mc::EcAccess> access,MaximumFanProbe probe,FirmwareFanDefault restore,OsdPopup* popup) : impl_(std::make_unique<Impl>(parent,std::move(access),std::move(probe),std::move(restore),popup)) {}
BiosPage::~BiosPage() = default;
void BiosPage::arrange(const RECT& area, UINT dpi, bool visible) {
    auto& p = *impl_;
    const bool entering=visible&&!p.visible;
    p.visible = visible;
    if (dpi != p.dpi || p.fontLanguage != ui::languageIndex()) { p.dpi = dpi; p.fonts(); }
    if (!visible) { ShowWindow(p.page, SW_HIDE); return; }
    p.width = area.right - area.left; p.height = area.bottom - area.top;
    page::place(p.page, area.left, area.top, p.width, p.height, true);
    p.refreshProfile(); if(entering){p.refreshPawnIoCaption();p.refreshEnergyCaption();} p.load(); p.arrange(); page::present(p.page);
}
bool BiosPage::preTranslate(MSG& message) {
    if (!impl_->visible) return false;
    if (message.message == WM_KEYDOWN && message.wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (GetKeyState(VK_SHIFT) & 0x8000) clipboard(impl_->page, impl_->report());
        else impl_->copy(true);
        return true;
    }
    return IsDialogMessageW(impl_->page, &message) != FALSE;
}
}
