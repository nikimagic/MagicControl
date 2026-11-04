#pragma once

#include <windows.h>
#include <filesystem>

// Windows is the reference; both frontends compile this geometry.
#include "../common/Layout.h"
#include "../common/Fonts.h"
#include "../common/Localization.h"
#include "../common/Geometry.h"

namespace mc::gui {
inline bool registerBundledUiFonts(const std::filesystem::path& directory){
    struct Fonts {
        std::filesystem::path sc,tc;bool scAdded{},tcAdded{};
        explicit Fonts(const std::filesystem::path& dir):sc(dir/L"NotoSansSC.ttf"),tc(dir/L"NotoSansTC.ttf"){
            scAdded=AddFontResourceExW(sc.c_str(),FR_PRIVATE,nullptr)>0;tcAdded=AddFontResourceExW(tc.c_str(),FR_PRIVATE,nullptr)>0;}
        ~Fonts(){if(scAdded)RemoveFontResourceExW(sc.c_str(),FR_PRIVATE,nullptr);if(tcAdded)RemoveFontResourceExW(tc.c_str(),FR_PRIVATE,nullptr);}
    };
    static Fonts fonts(directory);return fonts.scAdded&&fonts.tcAdded;
}
inline bool& embeddedUiFontSelection() noexcept {
    static bool enabled{true};
    return enabled;
}
inline void setEmbeddedUiFontEnabled(bool) noexcept {
    embeddedUiFontSelection() = true; // Legacy preference cannot enable OS fonts.
}
inline bool embeddedUiFontEnabled() noexcept {
    return embeddedUiFontSelection();
}

inline RECT toNativeRect(ui::Rect value) {
    return {value.left, value.top, value.right, value.bottom};
}
inline ui::Rect fromNativeRect(RECT value) {
    return {static_cast<int>(value.left), static_cast<int>(value.top),
            static_cast<int>(value.right), static_cast<int>(value.bottom)};
}
inline HFONT createUiFontFamily(ui::FontRole role, UINT dpi, const wchar_t* family) {
    const auto spec = ui::font(role);
    return CreateFontW(-ui::fontHeight(role, static_cast<int>(dpi)), 0, 0, 0,
        spec.weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
}
inline HFONT createUiFont(ui::FontRole role, UINT dpi) {
    const wchar_t* family = ui::selectedFontFamilyWindows(static_cast<ui::Language>(ui::languageIndex()));
    return createUiFontFamily(role, dpi, family);
}
}  // namespace mc::gui
