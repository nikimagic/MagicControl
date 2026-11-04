#pragma once

#include <algorithm>
#include <array>

namespace mc::gui::ui {

enum class Face { Text, Display, Classic, Symbols };
enum class FontRole { Body, Caption, Heading, Value, SmallValue, KeyTitle, KeyDetail,
                      ClassicBody, ClassicHeading, Brand, Tab, TabActive, Glyph,
                      OsdTitle, OsdDetail, MonitorCaption,
                      FanTitle, FanMetric, FanSecondary, FanSecondaryCompact, FanDetail, Count };
enum class Language { Russian, English, SimplifiedChinese, TraditionalChinese };

inline constexpr const char* kEmbeddedFontFamily = "Noto Sans SC";
inline constexpr const wchar_t* kEmbeddedFontFamilyWindows = L"Noto Sans SC";
inline constexpr const char* kTraditionalFontFamily = "Noto Sans TC";
inline constexpr const wchar_t* kTraditionalFontFamilyWindows = L"Noto Sans TC";

// Keep the reference roles/geometry; only Han regional forms follow UI language.
constexpr const char* selectedFontFamily(Language language) noexcept {
    return language == Language::TraditionalChinese ? kTraditionalFontFamily : kEmbeddedFontFamily;
}
constexpr const wchar_t* selectedFontFamilyWindows(Language language) noexcept {
    return language == Language::TraditionalChinese ? kTraditionalFontFamilyWindows : kEmbeddedFontFamilyWindows;
}

struct FontSpec {
    int halfPoints;
    int weight;
    Face face;
};

// Half points retain the Windows KEYs detail size of 8.5 pt without rounding.
inline constexpr std::array<FontSpec, static_cast<int>(FontRole::Count)> kFonts{{
    {18, 400, Face::Text}, {16, 400, Face::Text},
    {22, 700, Face::Display}, {50, 600, Face::Display},
    {28, 600, Face::Display}, {20, 400, Face::Classic},
    {17, 400, Face::Classic}, {18, 400, Face::Classic},
    {22, 700, Face::Classic}, {24, 700, Face::Display},
    {22, 600, Face::Display}, {22, 700, Face::Display},
    {20, 400, Face::Symbols}, {30, 700, Face::Classic}, {22, 400, Face::Classic},
    {16, 400, Face::Classic},
    {19, 700, Face::Classic}, {32, 700, Face::Classic},
    {22, 700, Face::Classic}, {18, 700, Face::Classic}, {17, 400, Face::Classic}
}};

constexpr FontSpec font(FontRole role) { return kFonts[static_cast<int>(role)]; }

constexpr int fontHeight(FontRole role, int dpi) {
    if (role == FontRole::OsdDetail) return std::max(1, (15 * dpi + 48) / 96);
    return std::max(1, (font(role).halfPoints * dpi + 72) / 144);
}

struct TextMetrics { int height, ascent; };
// Reference line boxes preserve the established geometry; actual text is
// measured with the bundled font for the selected language.
inline constexpr std::array<TextMetrics, static_cast<int>(FontRole::Count)> kTextMetrics192{{
    {32,26},{28,23},{38,31},{89,72},{49,40},{37,30},{31,25},
    {32,26},{40,32},{43,35},{38,31},{38,31},{27,27},{53,43},{40,32},{28,23},
    {34,27},{52,42},{40,32},{32,26},{31,25}
}};
inline constexpr std::array<TextMetrics, static_cast<int>(FontRole::Count)> kTextMetrics96{{
    {16,13},{15,12},{20,16},{44,36},{26,21},{17,14},{13,11},
    {15,12},{20,16},{21,17},{20,16},{20,16},{13,13},{27,22},{20,16},{15,12},
    {17,14},{27,22},{20,16},{16,13},{16,13}
}};
constexpr TextMetrics textMetrics(FontRole role, int dpi) {
    if (dpi == 96) return kTextMetrics96[static_cast<int>(role)];
    const auto value = kTextMetrics192[static_cast<int>(role)];
    return {(value.height * dpi + 96) / 192, (value.ascent * dpi + 96) / 192};
}

inline std::array<int,7> tabTextWidths(int dpi) {
    constexpr std::array<int,7> low{39,34,35,35,84,84,61}, high{71,62,64,65,168,168,122};
    std::array<int,7> result{};
    for (int i=0;i<7;++i) result[i] = dpi <= 192
        ? low[i] + ((high[i]-low[i]) * (dpi-96) + 48) / 96
        : (high[i] * dpi + 96) / 192;
    return result;
}
inline int brandTextWidth(int dpi) {
    return dpi <= 192 ? 87 + (91 * (dpi-96) + 48) / 96 : (178 * dpi + 96) / 192;
}

constexpr const char* languageTag(Language language) {
    switch (language) {
    case Language::Russian: return "ru";
    case Language::English: return "en";
    case Language::SimplifiedChinese: return "zh-CN";
    case Language::TraditionalChinese: return "zh-TW";
    }
    return "ru";
}

}  // namespace mc::gui::ui
