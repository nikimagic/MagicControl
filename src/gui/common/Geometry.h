#pragma once

#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <limits>
#include <span>

#include "Layout.h"
#include "Fonts.h"
#include "BatteryPolicy.h"
#include "SettingsCardContract.h"
#include "KeysPolicy.h"
#include "BiosActions.h"

namespace mc::gui::ui {

struct Rect {
    int left{}, top{}, right{}, bottom{};
    constexpr int width() const { return right - left; }
    constexpr int height() const { return bottom - top; }
};

struct NumericFieldGeometry { Rect edit,spin; };
inline constexpr int kPowerEditWidth=36, kPowerArrowWidth=14, kPowerFieldGap=8;
inline constexpr int kCustomPowerWidth=60;
// The caller owns the size. Both native parts always share one top/bottom edge.
inline NumericFieldGeometry numericFieldGeometry(Rect bounds,int arrowWidth){
    const int arrows=std::clamp(arrowWidth,0,std::max(0,bounds.width()));
    return {{bounds.left,bounds.top,bounds.right-arrows,bounds.bottom},
            {bounds.right-arrows,bounds.top,bounds.right,bounds.bottom}};
}

struct TooltipRegion { Rect bounds; std::string text; };

struct Scale {
    int dpi{96};
    constexpr int operator()(int value) const { return (value * dpi + 48) / 96; }
};

struct LaptopMenuGeometry {
    int width{},height{},rowHeight{},statusWidth{},rowsExtent{};
    Rect title,backArrow,footer,viewport;
    std::vector<Rect> rows;
};
inline int menuWidthFromText(int nameWidth,int titleWidth,int footerWidth,int availableWidth,Scale s={},int statusWidth=0){
    return std::min(std::max({s(240),nameWidth+statusWidth+s(statusWidth?72:48),titleWidth+s(32),footerWidth+s(32)}),std::max(1,availableWidth));
}
// Adapters supply real font measurements. Keep one line per model and reserve
// the same selection gutter/padding even when no item is selected or hovered.
inline LaptopMenuGeometry laptopMenuGeometry(int count,int nameWidth,int titleWidth,int footerWidth,
                                             int lineHeight,int availableWidth,Scale s={},int statusWidth=0,
                                             int availableHeight=std::numeric_limits<int>::max()) {
    LaptopMenuGeometry g;
    g.width=menuWidthFromText(nameWidth,titleWidth,footerWidth,availableWidth,s,statusWidth);
    g.statusWidth=std::min(statusWidth,std::max(0,g.width-s(100)));
    g.rowHeight=std::max(s(32),lineHeight+s(14));
    g.title={s(16),s(6),g.width-s(16),s(34)};
    g.backArrow={s(20),s(6),s(36),s(34)};
    int top=s(40);
    const int visibleHeight=std::max(1,std::min(std::max(0,count)*g.rowHeight,availableHeight-s(76)));
    g.rowsExtent=std::max(0,count)*g.rowHeight;
    g.viewport={s(8),top,g.width-s(g.rowsExtent>visibleHeight?20:8),top+visibleHeight};
    for(int i=0;i<count;++i){g.rows.push_back({s(8),top,g.viewport.right,top+g.rowHeight});top+=g.rowHeight;}
    top=g.viewport.bottom;
    g.footer={s(16),top+s(8),g.width-s(16),top+s(32)};
    g.height=top+s(36);
    return g;
}

struct LanguageMenuGeometry {
    int width{},height{};
    std::array<Rect,4> languages,selectionMarks;
    Rect models,modelsTopSeparator,license,copyright,reserved,github,donations,qr,wallet,copyHint,website;
    std::array<Rect,2> separators;
};
// The main panel owns its compact width. Never pass the expanded laptop-list
// width here. Fonts, padding and the180-DIP QR are not scaled with panel width.
inline LanguageMenuGeometry languageMenuGeometry(int width,Scale s={}) {
    LanguageMenuGeometry g;g.width=width;
    const int left=s(8),right=g.width-s(8),middle=left+(right-left)/2;
    for(int i=0;i<4;++i){
        const int l=i%2?middle:left,r=i%2?right:middle;
        g.languages[i]={l,s(6+i/2*30),r,s(36+i/2*30)};
        g.selectionMarks[i]={l+s(2),g.languages[i].top,l+s(18),g.languages[i].bottom};
    }
    const auto inset=[&](int pad,int top,int bottom){return Rect{s(pad),s(top),g.width-s(pad),s(bottom)};};
    g.models=inset(8,70,98);g.modelsTopSeparator=inset(10,68,69);g.license=inset(10,108,150);
    g.copyright=inset(12,110,132);g.reserved=inset(12,132,150);
    g.github=inset(12,152,178);g.donations=inset(12,190,212);
    g.separators={inset(10,102,103),inset(10,184,185)};
    const int side=std::min(s(180),std::max(1,g.width-s(20)));
    g.qr={(g.width-side)/2,s(216),(g.width-side)/2+side,s(216)+side};
    const int walletTop=g.qr.bottom+s(8);
    g.wallet={s(10),walletTop,g.width-s(10),walletTop+s(38)};
    g.copyHint={s(10),walletTop+s(40),g.width-s(10),walletTop+s(58)};
    g.website={s(10),walletTop+s(60),g.width-s(10),walletTop+s(84)};
    g.height=walletTop+s(90);
    return g;
}

struct HeaderGeometry {
    Rect title, icon, brand, content, status;
    std::array<Rect, kTabCount> tabs{};
    std::array<Rect, 3> captionButtons{};
};

template<std::size_t N>
inline HeaderGeometry headerGeometry(int width, int height,
                                      const std::array<int, N>& textWidths, Scale s = {}, int brandWidth = 0,int tabCount=4,
                                      std::array<bool,kTabCount> visible={true,true,true,true,true,true,true}) {
    HeaderGeometry g;
    g.title = {0, 0, width, s(kTitleBarHeight)};
    const int iconTop = (g.title.height() - s(kHeaderIconSize)) / 2;
    g.icon = {s(kHeaderIconLeft), iconTop, s(kHeaderIconLeft + kHeaderIconSize), iconTop + s(kHeaderIconSize)};
    g.brand = {s(kBrandPaddingLeft), 0, s(kBrandPaddingLeft + kBrandWidth), g.title.bottom};
    g.content = {0, g.title.bottom, width, std::max(g.title.bottom, height - s(kStatusBarHeight))};
    g.status = {0, std::max(0, height - s(kStatusBarHeight)), width, height};
    int total = 0;
    tabCount=std::clamp(tabCount,0,static_cast<int>(std::min<std::size_t>(N,kTabCount)));
    for (int i=0;i<tabCount;++i) if(visible[i])total += textWidths[i] + s(kTabPaddingX) * 2;
    int x = (width - total) / 2;
    for (int i = 0; i < tabCount; ++i) {
        if(!visible[i])continue;
        const int w = textWidths[i] + s(kTabPaddingX) * 2;
        g.tabs[i] = {x, 0, x + w, g.title.bottom};
        x += w;
    }
    for (int i = 0; i < 3; ++i) {
        const int right = width - (2 - i) * s(kWindowButtonWidth);
        g.captionButtons[i] = {right - s(kWindowButtonWidth), 0, right, g.title.bottom};
    }
    const int brandRight=std::min(g.tabs[0].left-s(12),g.captionButtons[0].left-s(8));
    g.brand.right=std::max(g.brand.left,std::min(brandRight,g.brand.left+(brandWidth>0?brandWidth:s(kBrandWidth))));
    return g;
}

struct DeviceBrandGeometry { Rect manufacturer, model; int arrowX{}, arrowY{}; };
inline DeviceBrandGeometry deviceBrandGeometry(Rect r, Scale s, int manufacturerWidth, int modelWidth,
        TextMetrics manufacturerMetrics = {}, TextMetrics modelMetrics = {}) {
    if(manufacturerMetrics.height<=0)manufacturerMetrics=textMetrics(FontRole::Brand,s.dpi);
    if(modelMetrics.height<=0)modelMetrics=textMetrics(FontRole::Body,s.dpi);
    const int left=std::min(r.right,r.left+s(12)),right=std::max(left,r.right-s(20));
    const int manufacturerRight=std::min(right,left+manufacturerWidth);
    const int modelLeft=std::min(right,manufacturerRight+(modelWidth>0?s(8):0));
    const int modelRight=std::min(right,modelLeft+modelWidth);
    const int top=r.top+(r.height()-manufacturerMetrics.height)/2;
    const int baseline=top+manufacturerMetrics.ascent,modelTop=baseline-modelMetrics.ascent;
    return {{left,top,manufacturerRight,top+manufacturerMetrics.height},
            {modelLeft,modelTop,modelRight,modelTop+modelMetrics.height},
            std::min(r.right-s(10),(modelWidth>0?modelRight:manufacturerRight)+s(8)),(r.top+r.bottom)/2};
}

struct FansGeometry {
    std::array<Rect, 2> fans{}, graphs{}, editors{}, curves{};
    std::array<Rect, 5> monitors{};
    Rect note{};
    int extent{};
};

inline FansGeometry fansGeometry(int width, Scale s = {}, int fanHeight = 0,bool batteryVisible = true) {
    FansGeometry g;
    const int pad = s(kContentPadding), gap = s(kCardGap);
    const int inner = std::max(1, width - pad * 2);
    const int half = (inner - gap) / 2;
    if (fanHeight <= 0) fanHeight = s(kFanCardHeight);
    int y = s(kPageTopPadding);
    g.fans[0] = {pad, y, pad + half, y + fanHeight};
    g.fans[1] = {pad + half + gap, y, width - pad, y + fanHeight};
    y += fanHeight + gap;
    const int count=batteryVisible?5:4;
    const int tileWidth = (inner - gap * (count-1)) / count;
    const int monitorHeight = s(tileWidth < s(kMonitorInlineMinWidth) ? kMonitorWrappedHeight : kMonitorTileHeight);
    const int tileStart = pad + (inner - (tileWidth * count + gap * (count-1))) / 2;
    for (int i = batteryVisible?0:1; i < 5; ++i) {
        const int x = tileStart + (i-(batteryVisible?0:1)) * (tileWidth + gap);
        g.monitors[i] = {x, y, x + tileWidth, y + monitorHeight};
    }
    y += monitorHeight + gap;
    const int plotHeight = (s(kFanGraphPlotOriginalHeight) * kFanGraphPlotHeightPercent + 50) / 100;
    const int graphHeight = s(kFanGraphPlotTop) + plotHeight + s(kFanGraphFooterHeight);
    g.graphs[0] = {pad, y, pad + half, y + graphHeight};
    g.graphs[1] = {pad + half + gap, y, width - pad, y + graphHeight};
    y += graphHeight;
    g.editors[0] = {pad, y, pad + half, y + s(kFanEditorHeight)};
    g.editors[1] = {pad + half + gap, y, width - pad, y + s(kFanEditorHeight)};
    for (int i = 0; i < 2; ++i)
        g.curves[i] = {g.graphs[i].left, g.graphs[i].top, g.graphs[i].right, g.editors[i].bottom};
    y += s(kFanEditorHeight) + gap;
    g.note = {pad, y, width - pad, y + s(20)};
    g.extent = g.note.bottom + s(8);
    return g;
}

struct FanReadoutGeometry {
    Rect heading, information, secondary, secondaryTitle, secondaryValue, secondaryUnit, secondarySource;
    Rect secondaryExtraTitle, secondaryExtraValue, secondaryExtraUnit;
    std::array<Rect, 3> informationLines{};
    std::array<Rect, 3> values{}, units{}, valueUnits{}, sources{};
};

inline int processorInformationTop(Scale s, TextMetrics captionMetrics) {
    return s(kFanCaptionTop) + captionMetrics.height * 2 + s(2);
}

inline FanReadoutGeometry fanReadoutGeometry(Rect r, Scale s = {},
        std::array<int,3> lineHeights = {}, int secondaryTitleWidth = 0, int secondaryValueWidth = 0,
        int secondaryUnitWidth = 0, std::array<int,3> valueWidths = {}, std::array<int,3> unitWidths = {},
        TextMetrics captionMetrics = {}, TextMetrics secondaryMetrics = {},
        TextMetrics metricMetrics = {}, TextMetrics unitMetrics = {},
        int secondaryExtraTitleWidth = 0, int secondaryExtraValueWidth = 0, int secondaryExtraUnitWidth = 0) {
    if (captionMetrics.height <= 0) captionMetrics = textMetrics(FontRole::MonitorCaption, s.dpi);
    if (secondaryMetrics.height <= 0) secondaryMetrics = textMetrics(FontRole::FanSecondary, s.dpi);
    if (metricMetrics.height <= 0) metricMetrics = textMetrics(FontRole::FanMetric, s.dpi);
    if (unitMetrics.height <= 0) unitMetrics = textMetrics(FontRole::ClassicBody, s.dpi);
    FanReadoutGeometry g;
    const int pad = s(kCardPadding), inner = r.width() - pad * 2;
    const int widths[3]{inner / 3, inner / 3, inner - 2 * (inner / 3)};
    g.heading = {r.left + pad, r.top + s(8), r.right - pad, r.top + s(26)};
    int x = r.left + pad;
    for (int i = 0; i < 3; ++i) {
        const int right = x + widths[i] - s(4);
        const int unitWidth = i > 0 ? std::min(unitWidths[i], right - x) : 0;
        const int unitSpace = unitWidth > 0 ? s(6) + unitWidth : 0;
        const int valueWidth = std::min(valueWidths[i], std::max(0, right - x - unitSpace));
        const int valueLeft = x + std::max(0, (right - x - valueWidth - unitSpace) / 2);
        const int valueTop = r.top + s(kFanMetricRowTop) + (s(kFanMetricRowHeight) - metricMetrics.height) / 2;
        g.values[i] = {valueLeft, valueTop, valueLeft + valueWidth, valueTop + metricMetrics.height};
        const int captionTop = r.top + s(kFanCaptionTop);
        g.units[i] = {x, captionTop, x + widths[i] - s(4), captionTop + captionMetrics.height};
        g.sources[i] = {x, g.units[i].bottom, g.units[i].right, g.units[i].bottom + captionMetrics.height};
        if (i > 0) {
            const int unitLeft = std::min(right, g.values[i].right + s(6));
            const int unitTop = valueTop + metricMetrics.ascent - unitMetrics.ascent;
            g.valueUnits[i] = {unitLeft, unitTop, std::min(right, unitLeft + unitWidth), unitTop + unitMetrics.height};
        }
        x += widths[i];
    }
    const int third = r.left + pad + widths[0] + widths[1];
    g.information = {r.left + pad, r.top + processorInformationTop(s, captionMetrics), third - s(4), r.bottom - s(8)};
    g.secondary = {third, g.information.top, r.right - pad, g.information.bottom};
    int y = g.information.top + s(4);
    for (int i = 0; i < 3; ++i) {
        const int h = std::max(captionMetrics.height, lineHeights[i]);
        g.informationLines[i] = {g.information.left + s(8), y, g.information.right - s(8), y + h};
        y += h + (i < 2 ? s(2) : 0);
    }
    const int secondaryLeft = g.secondary.left + s(8), secondaryRight = g.secondary.right - s(8);
    const int availableWidth = secondaryRight - secondaryLeft;
    const bool hasExtraMetric = secondaryExtraTitleWidth > 0 || secondaryExtraValueWidth > 0 || secondaryExtraUnitWidth > 0;
    const int secondaryUnitSpace = secondaryUnitWidth > 0 ? s(6) + secondaryUnitWidth : 0;
    const int metricWidth = secondaryValueWidth + secondaryUnitSpace;
    const int groupWidth = secondaryTitleWidth + s(8) + metricWidth;
    const bool wrap = groupWidth > availableWidth;
    const int groupLeft = secondaryLeft + std::max(0, (availableWidth - (wrap ? metricWidth : groupWidth)) / 2);
    const int valueTop = g.secondary.top + s(2) + (wrap ? captionMetrics.height + s(2) : 0);
    const int valueLeft = wrap ? groupLeft : groupLeft + secondaryTitleWidth + s(8);
    const int titleTop = wrap ? g.secondary.top + s(2) : valueTop + secondaryMetrics.ascent - captionMetrics.ascent;
    g.secondaryTitle = {wrap ? secondaryLeft : groupLeft, titleTop,
        wrap ? secondaryRight : groupLeft + secondaryTitleWidth, titleTop + captionMetrics.height};
    const int valueRight = std::min(secondaryRight, valueLeft +
        std::min(secondaryValueWidth, std::max(0, secondaryRight - valueLeft - secondaryUnitSpace)));
    const int unitLeft = std::min(secondaryRight, valueRight + s(6));
    g.secondaryValue = {valueLeft, valueTop, valueRight, valueTop + secondaryMetrics.height};
    const int unitTop = valueTop + secondaryMetrics.ascent - captionMetrics.ascent;
    g.secondaryUnit = {unitLeft, unitTop, std::min(secondaryRight, unitLeft + secondaryUnitWidth), unitTop + captionMetrics.height};
    if (hasExtraMetric) {
        const int extraUnitSpace = secondaryExtraUnitWidth > 0 ? s(6) + secondaryExtraUnitWidth : 0;
        const int extraGroupWidth = secondaryExtraTitleWidth + s(8) + secondaryExtraValueWidth + extraUnitSpace;
        const int extraLeft = secondaryLeft + std::max(0, (availableWidth - extraGroupWidth) / 2);
        const int extraValueLeft = extraLeft + secondaryExtraTitleWidth + s(8);
        const int extraUnitLeft = extraValueLeft + secondaryExtraValueWidth + (secondaryExtraUnitWidth > 0 ? s(6) : 0);
        const int extraValueTop = g.secondaryValue.bottom + s(2);
        const int extraUnitTop = extraValueTop + secondaryMetrics.ascent - captionMetrics.ascent;
        g.secondaryExtraTitle = {extraLeft, extraUnitTop, extraLeft + secondaryExtraTitleWidth, extraUnitTop + captionMetrics.height};
        g.secondaryExtraValue = {extraValueLeft, extraValueTop, std::min(secondaryRight, extraValueLeft + secondaryExtraValueWidth), extraValueTop + secondaryMetrics.height};
        g.secondaryExtraUnit = {extraUnitLeft, extraUnitTop, std::min(secondaryRight, extraUnitLeft + secondaryExtraUnitWidth), extraUnitTop + captionMetrics.height};
    }
    const int sourceTop = hasExtraMetric ? g.secondaryExtraValue.bottom + s(1) : g.secondaryValue.bottom + s(1);
    g.secondarySource = {secondaryLeft, sourceTop, secondaryRight, g.secondary.bottom - s(2)};
    return g;
}
inline int processorCardHeight(const std::array<int,3>& informationHeights, int secondarySourceHeight,
                               bool secondaryWrap, Scale s, TextMetrics captionMetrics = {}, TextMetrics secondaryMetrics = {},
                               bool secondaryExtraRow = false) {
    if (captionMetrics.height <= 0) captionMetrics = textMetrics(FontRole::MonitorCaption, s.dpi);
    if (secondaryMetrics.height <= 0) secondaryMetrics = textMetrics(FontRole::FanSecondary, s.dpi);
    int information = s(8 + 4);
    for (const int height : informationHeights) information += std::max(captionMetrics.height, height);
    const int secondary = s(2 + 1 + 2) + secondaryMetrics.height +
        (secondaryWrap ? captionMetrics.height + s(2) : 0) +
        (secondaryExtraRow ? secondaryMetrics.height + s(2) : 0) +
        std::max(captionMetrics.height, secondarySourceHeight);
    return std::max(s(kFanCardHeight), processorInformationTop(s, captionMetrics) + s(8) + std::max(information, secondary));
}

struct MonitorGeometry { Rect title, value, unit, detail, source; };
inline MonitorGeometry monitorGeometry(Rect r, Scale s, int titleWidth, int valueWidth, int unitWidth) {
    const int left = r.left + s(8), right = r.right - s(8), top = r.top + s(5);
    const bool wrap = r.height() > s(kMonitorTileHeight);
    const int valueLeft = wrap ? left : left + titleWidth + s(8);
    const int valueTop = top + (wrap ? s(16) : 0);
    const int unitLeft = std::min(right, valueLeft + valueWidth + s(6));
    const int detailTop = valueTop + s(16);
    return {{left, top, wrap ? right : std::min(right, valueLeft - s(4)), top + s(16)},
            {valueLeft, valueTop, std::min(right, valueLeft + valueWidth), valueTop + s(16)},
            {unitLeft, valueTop, std::min(right, unitLeft + unitWidth), valueTop + s(16)},
            {left, detailTop, right, detailTop + s(14)},
            {left, detailTop + s(16), right, detailTop + s(30)}};
}

struct BatteryCardGeometry {
    std::array<Rect, 3> labels{}, values{}, rightValues{};
};
inline BatteryCardGeometry batteryCardGeometry(Rect r, Scale s,
        const std::array<int, 3>& labelWidths, const std::array<int, 3>& valueWidths,
        const std::array<int, 3>& rightWidths) {
    BatteryCardGeometry g;
    const int left = r.left + s(8), right = std::max(left, r.right - s(8));
    const int lineHeight = s(16), top = r.top + std::max(0, (r.height() - lineHeight * 3) / 2);
    for (int row = 0; row < 3; ++row) {
        const int y = top + row * lineHeight;
        const int rightLeft = std::max(left, right - rightWidths[row]);
        const int leftRight = std::max(left, rightLeft - s(6));
        const int labelWidth = std::min(labelWidths[row], std::max(0, leftRight - left - valueWidths[row] - s(4)));
        const int valueLeft = std::min(leftRight, left + labelWidth + (labelWidth > 0 ? s(4) : 0));
        g.labels[row] = {left, y, left + labelWidth, y + lineHeight};
        g.values[row] = {valueLeft, y, std::min(leftRight, valueLeft + valueWidths[row]), y + lineHeight};
        g.rightValues[row] = {rightLeft, y, right, y + lineHeight};
    }
    return g;
}

struct GraphGeometry { Rect title, axis, temperatureUnit, plot, legend; };
inline GraphGeometry graphGeometry(Rect r, Scale s = {}, int axisWidth = 0, TextMetrics labelMetrics = {}) {
    if(labelMetrics.height<=0)labelMetrics=textMetrics(FontRole::Caption,s.dpi);
    const int left=r.left+s(3);
    const int axisRight=left+std::max(s(35),axisWidth);
    const int titleInset=axisRight-r.left+s(8);
    const int textTop=r.top+s(2), textBottom=textTop+labelMetrics.height;
    const Rect plot{r.left+s(39),r.top+s(kFanGraphPlotTop),r.right-s(25),r.bottom-s(kFanGraphFooterHeight)};
    return {{r.left+titleInset, textTop, r.right-titleInset, textBottom},
            {left, textTop, axisRight, textBottom},
            {plot.right+s(11), plot.bottom+s(2), r.right-s(1), plot.bottom+s(15)},
            plot,
            {r.left + s(10), r.bottom - s(21), r.right - s(10), r.bottom - s(4)}};
}

struct EditorGeometry {
    Rect temperatureLabel, dutyLabel, previous, next, page, source, count, linkCurves;
    std::array<Rect, 4> presets{};
    std::array<Rect, 8> numbers{}, temperatures{}, duties{};
    std::array<Rect, 8> temperatureEdits{}, temperatureArrows{}, dutyEdits{}, dutyArrows{};
};

inline EditorGeometry editorGeometry(Rect r, Scale s = {}, bool withLink = false, int rowLabelWidth = 0) {
    EditorGeometry g;
    const int pad = s(8), labelWidth = std::max(s(48),rowLabelWidth+s(8));
    const int cellWidth = std::max(s(24), (r.width() - pad * 2 - labelWidth) / 8);
    const int first = r.left + pad + labelWidth;
    g.temperatureLabel = {r.left + pad, r.top + s(20), r.left + pad + labelWidth-s(6), r.top + s(42)};
    g.dutyLabel = {g.temperatureLabel.left, r.top + s(48), g.temperatureLabel.right, r.top + s(70)};
    for (int i = 0; i < 8; ++i) {
        const int x = first + i * cellWidth;
        g.numbers[i] = {x, r.top + s(1), x + cellWidth - s(2), r.top + s(17)};
        g.temperatures[i] = {x + s(2), r.top + s(20), x + cellWidth - s(2), r.top + s(20) + s(kCurveNumericHeight)};
        g.duties[i] = {x + s(2), r.top + s(48), x + cellWidth - s(2), r.top + s(48) + s(kCurveNumericHeight)};
        const int arrowWidth = std::min(s(kCurveArrowWidth), std::max(1, cellWidth / 3));
        auto split = [arrowWidth](Rect full, Rect& edit, Rect& arrows) {
            const auto field=numericFieldGeometry(full,arrowWidth);edit=field.edit;arrows=field.spin;
        };
        split(g.temperatures[i], g.temperatureEdits[i], g.temperatureArrows[i]);
        split(g.duties[i], g.dutyEdits[i], g.dutyArrows[i]);
    }
    const int footer = r.top + s(78), right = r.right - pad;
    g.previous = {right - s(24) * 2 - s(4), footer, right - s(24) - s(4), footer + s(kCurveNumericHeight)};
    g.next = {right - s(24), footer, right, footer + s(kCurveNumericHeight)};
    g.page = {g.previous.left - s(4) - s(52), footer, g.previous.left - s(4), footer + s(kCurveNumericHeight)};
    g.source = {r.left + pad, footer, g.page.left - s(8), footer + s(kCurveNumericHeight)};
    const std::array<int, 4> presetWidths{54, 52, 40, 40};
    const int buttonTop = r.bottom - s(4) - s(kButtonHeight);
    int presetLeft = r.left + pad;
    for (std::size_t i = 0; i < g.presets.size(); ++i) {
        g.presets[i] = {presetLeft, buttonTop, presetLeft + s(presetWidths[i]), r.bottom - s(4)};
        presetLeft = g.presets[i].right + s(4);
    }
    g.count = {presetLeft + s(4), buttonTop, r.right - pad, r.bottom - s(4)};
    if (withLink) {
        g.linkCurves = {g.count.left, g.count.top, g.count.left + s(76), g.count.bottom};
        g.count.left = std::min(right, g.linkCurves.right + s(8));
    }
    return g;
}

struct KeysGeometry {
    Rect quickCard, devicesCard, quickHeading, devicesHeading, result;
    std::array<Rect, keyRowCount> rows{}, titles{}, details{}, off{}, on{};
    int extent{}, rowHeight{}, buttonWidth{}, titleLeft{}, titleWidth{}, offLeft{}, onLeft{};
};

inline KeysGeometry keysGeometry(int width, int height, Scale s = {}, std::span<const bool> visible = {}) {
    KeysGeometry g;
    const auto shown=[&](std::size_t i){return visible.size()!=keyRowCount||visible[i];};
    int quickCount{},deviceCount{};
    for(std::size_t i=0;i<keyRowCount;++i)if(shown(i)){
        if(i<keyQuickRowCount)++quickCount;else ++deviceCount;
    }
    const int pad = s(kContentPadding);
    const int top = s(kPageTopPadding);
    g.rowHeight = s(40); g.buttonWidth = s(64); g.titleLeft = pad * 2;
    g.offLeft = width - pad * 2 - g.buttonWidth * 2;
    g.onLeft = g.offLeft + g.buttonWidth;
    g.titleWidth = g.offLeft - g.titleLeft - s(12);
    const int quickBottom = top + s(34) + g.rowHeight * quickCount + s(4);
    const int devicesTop = quickBottom + s(kCardGap);
    const int devicesBottom = devicesTop + s(34) + g.rowHeight * deviceCount + s(4);
    g.quickCard = {pad, top, width - pad, quickBottom};
    g.devicesCard = {pad, devicesTop, width - pad, devicesBottom};
    g.quickHeading = {g.titleLeft, top + s(10), width - s(24), top + s(38)};
    g.devicesHeading = {g.titleLeft, devicesTop + s(10), width - s(24), devicesTop + s(38)};
    const int titleHeight = textMetrics(FontRole::KeyTitle, s.dpi).height;
    const int detailHeight = textMetrics(FontRole::KeyDetail, s.dpi).height;
    const int textTop = (g.rowHeight - titleHeight - detailHeight) / 2;
    const int buttonTop = (g.rowHeight - s(kButtonHeight)) / 2;
    int quickIndex{},deviceIndex{};
    for (int i = 0; i < static_cast<int>(keyRowCount); ++i) {
        if(!shown(static_cast<std::size_t>(i)))continue;
        const int y = (i < static_cast<int>(keyQuickRowCount) ? top : devicesTop) + s(34)
            + (i < static_cast<int>(keyQuickRowCount) ? quickIndex++ : deviceIndex++) * g.rowHeight;
        g.rows[i] = {0, y, width, y + g.rowHeight};
        g.titles[i] = {g.titleLeft, y + textTop, g.titleLeft + g.titleWidth, y + textTop + titleHeight};
        g.details[i] = {g.titleLeft, g.titles[i].bottom, g.titleLeft + g.titleWidth, g.titles[i].bottom + detailHeight};
        g.off[i] = {g.offLeft, y + buttonTop, g.onLeft, y + buttonTop + s(kButtonHeight)};
        g.on[i] = {g.onLeft, y + buttonTop, g.onLeft + g.buttonWidth, y + buttonTop + s(kButtonHeight)};
    }
    const int resultTop = devicesBottom + s(8);
    g.result = {pad, resultTop, width - pad, resultTop + s(28)};
    g.extent = std::max(height, g.result.bottom + pad);
    return g;
}

struct BatteryDetailColumnGeometry {Rect card{},heading{},status{},note{};int rows{};};
struct SetsGeometry {
    Rect agingCaption{},agingOff{},agingOn{};
    Rect loggerHeading{},loggerPrefix{},loggerAddressEdit{},loggerAddressSpin{},loggerMiddle{},loggerValueEdit{},loggerValueSpin{},loggerSuffix{},loggerFormats{},loggerMeaning{},loggerKnown{},loggerMulti{};
    Rect restartCaption{},restartEdit{},restartSpin{},restartUnit{};
    Rect batteryIdentity{},batteryDeviceIdentity{},batteryVoltageSummary{};std::array<Rect,3> batteryVoltage{};
    Rect batteryComparison{};
    std::array<BatteryDetailColumnGeometry,2> batteryDetailColumns{};
    int batteryDetailRowHeight{},batteryDetailRowsTop{};
    std::array<Rect, 3> cards{}, headings{}, details{};
    std::array<Rect, 4> effects{};
    std::array<Rect, batteryThresholdCount> battery{};
    std::array<Rect, 5> performance{};
    Rect portDetail{};
    std::array<Rect,2> batteryInfoCards{},batteryInfoHeadings{},batteryInfoReadings{};
    int batteryInfoRowHeight{},batteryInfoReadingHeight{},batteryInfoRowsTop{};
    std::array<Rect, 5> performanceDetails{};
    std::array<Rect, 3> apuDiagnostics{};
    Rect manualPowerButton{}, apuPolicyCaption{}, apuPolicyOff{}, apuPolicyOn{}, apuPolicyOriginal{};
    std::array<Rect,2> usbPowerPriority{};
    std::array<Rect,3> customPowerPresets{};
    std::array<Rect, 3> powerCaptions{}, powerEdits{}, powerSpins{};
    std::array<Rect,6> powerOptionCaptions{},powerOptionEdits{},powerOptionSpins{};
    std::array<Rect,2> powerOptionHeaders{};
    std::array<Rect, 8> colors{};
    std::array<Rect,5> brightnessLevels{};
    Rect colorCaption, customColor, nativeButton, status;
    Rect keyboardTimeoutCaption, keyboardTimeoutCheck, keyboardTimeoutEdit, keyboardTimeoutSpin, keyboardTimeoutUnit;
    Rect cycleCaption,cycleEdit,cycleSpin,cycleUnit;
    int extent{};
};

inline void placePowerFields(SetsGeometry& g,Scale s,const std::array<int,3>& widths,int digitsWidth){
    const int editWidth=std::max(s(kPowerEditWidth),digitsWidth+s(8));
    const int arrows=s(kPowerArrowWidth),gap=s(kPowerFieldGap);
    int total=0;for(int width:widths)total+=width+editWidth+arrows+gap;
    int x=g.manualPowerButton.left-total;
    g.apuDiagnostics[0].right=x-s(12);
    for(int i=0;i<3;++i){
        const int top=g.powerEdits[i].top,bottom=g.powerEdits[i].bottom;
        g.powerCaptions[i]={x,top,x+widths[i],bottom};x+=widths[i];
        const auto field=numericFieldGeometry({x,top,x+editWidth+arrows,bottom},arrows);
        g.powerEdits[i]=field.edit;g.powerSpins[i]=field.spin;
        x+=editWidth+arrows+gap;
    }
}
inline void placeCombinedBatteryControls(SetsGeometry& g,Scale s,int identityWidth){
    if(g.cards[0].width()<=0)return;
    const int left=g.battery.front().left,right=g.battery.back().right,gap=s(6);
    for(int i=0;i<7;++i){
        const int start=left+(right-left+gap)*i/7;
        const int end=left+(right-left+gap)*(i+1)/7-gap;
        const Rect r{start,g.battery[0].top,end,g.battery[0].bottom};
        if(i<3)g.batteryVoltage[i]=r;else g.battery[i-3]=r;
    }
    const int availableLeft=g.headings[0].left+s(115);
    const int availableRight=std::max(availableLeft,g.batteryIdentity.left-s(12));
    const int width=std::min(identityWidth,std::max(0,availableRight-availableLeft));
    const int center=(availableLeft+availableRight)/2;
    g.batteryDeviceIdentity={center-width/2,g.headings[0].top,center-width/2+width,g.headings[0].bottom};
    g.headings[0].right=std::min(g.headings[0].right,availableLeft-s(8));
}
inline void placePowerOptionFields(SetsGeometry& g,Scale s,const std::array<int,6>& widths,int digitsWidth,int rows){
    const int arrows=s(kPowerArrowWidth),edit=std::max(s(kPowerEditWidth),digitsWidth+s(8)),gap=s(kPowerFieldGap);
    for(int row=0;row<std::min(rows,2);++row){int total=0;for(int col=0;col<3;++col)total+=widths[row*3+col]+edit+arrows+gap;
        int x=g.manualPowerButton.left-total;const int top=g.powerEdits[0].top+s(32*(row+1)),bottom=top+s(22);
        g.powerOptionHeaders[row]={g.cards[1].left+s(kCardPadding),top,x-s(8),bottom};
        for(int col=0;col<3;++col){const int i=row*3+col;g.powerOptionCaptions[i]={x,top,x+widths[i],bottom};x+=widths[i];
            const auto field=numericFieldGeometry({x,top,x+edit+arrows,bottom},arrows);g.powerOptionEdits[i]=field.edit;g.powerOptionSpins[i]=field.spin;x+=edit+arrows+gap;}
    }
}
// All rows share the same measured caption width per column. This keeps the
// native numeric controls aligned without changing their size or card height.
inline void placePowerFieldsWithOptions(SetsGeometry& g,Scale s,const std::array<int,3>& mainWidths,
                                        const std::array<int,6>& optionWidths,int digitsWidth,int rows){
    auto columns=mainWidths;
    for(int row=0;row<std::clamp(rows,0,2);++row)
        for(int col=0;col<3;++col)columns[col]=std::max(columns[col],optionWidths[row*3+col]);
    // Keep room for the current package/APU reading when a narrow window or
    // translated captions cannot fit at full width; renderers ellipsize labels.
    const int edit=std::max(s(kPowerEditWidth),digitsWidth+s(8));
    const int fixed=3*(edit+s(kPowerArrowWidth)+s(kPowerFieldGap));
    const int available=g.manualPowerButton.left-(g.cards[1].left+s(kCardPadding)+s(120));
    const int budget=std::max(0,available-fixed),total=columns[0]+columns[1]+columns[2];
    if(total>budget&&total>0)for(auto& width:columns)
        width=static_cast<int>(static_cast<long long>(width)*budget/total);
    placePowerFields(g,s,columns,digitsWidth);
    std::array<int,6> aligned{};
    for(int i=0;i<6;++i)aligned[i]=columns[i%3];
    placePowerOptionFields(g,s,aligned,digitsWidth,rows);
}
inline void placeEcPolicyCaption(SetsGeometry& g,Scale s,int textWidth){
    g.apuPolicyCaption.left=g.apuPolicyCaption.right-std::max(s(130),textWidth);
    g.headings[1].right=g.apuPolicyCaption.left-s(12);
}
inline void placeUsbPowerPriority(SetsGeometry& g,Scale s,int batteryTextWidth,int performanceTextWidth){
    const int right=g.apuPolicyOriginal.right?g.apuPolicyOriginal.right:g.apuPolicyOn.right;
    const int top=g.apuPolicyOff.top,bottom=g.apuPolicyOff.bottom;
    const int batteryWidth=std::max(s(70),batteryTextWidth+s(16));
    const int performanceWidth=std::max(s(98),performanceTextWidth+s(16));
    g.usbPowerPriority[1]={right-performanceWidth,top,right,bottom};
    g.usbPowerPriority[0]={g.usbPowerPriority[1].left-batteryWidth,top,g.usbPowerPriority[1].left,bottom};
    const int shift=batteryWidth+performanceWidth+s(8);
    const auto move=[shift](Rect& r){if(r.right>r.left){r.left-=shift;r.right-=shift;}};
    move(g.apuPolicyOff);move(g.apuPolicyOn);move(g.apuPolicyOriginal);move(g.apuPolicyCaption);
    g.headings[1].right=std::max(g.headings[1].left,g.apuPolicyCaption.left-s(8));
}
inline void placeBatteryRestart(SetsGeometry& g,Scale s,std::array<int,3> widths){
    const int right=g.cards[0].right-s(12),top=g.headings[0].top,bottom=top+s(22);
    const int unitWidth=std::max(s(12),widths[1]);
    g.restartUnit={right-unitWidth,top,right,bottom};
    const int fieldRight=g.restartUnit.left-s(6),fieldWidth=std::max(s(58),widths[2]+s(26));
    const auto field=numericFieldGeometry({fieldRight-fieldWidth,top,fieldRight,bottom},s(14));
    g.restartEdit=field.edit;g.restartSpin=field.spin;
    g.restartCaption={g.restartEdit.left-s(8)-widths[0],top,g.restartEdit.left-s(8),bottom};
    g.headings[0].right=g.restartCaption.left-s(12);
}
// Replace only the retired restart controls inside the existing header.
// Keep cards, heights, viewport width and scrollbar geometry unchanged.
inline void placeBatteryIdentity(SetsGeometry& g,Scale s,int textWidth){
    if(g.cards[0].width()<=0)return;
    const int right=g.cards[0].right-s(12),top=g.headings[0].top;
    g.batteryIdentity={right-std::max(s(190),textWidth),top,right,top+s(22)};
    g.headings[0].right=g.batteryIdentity.left-s(12);
}
inline constexpr int kBatteryInfoHeaderHeight=32;
inline void placeCombinedBatteryStatus(SetsGeometry& g,Scale s,int captionWidth,int identityWidth,int buttonTextWidth){
    if(g.cards[0].width()<=0)return;
    const int gap=s(4),button=std::max(s(40),buttonTextWidth+s(12)),left=g.headings[0].right+s(8);
    const int right=std::max(left,g.batteryIdentity.left-s(12));
    const int caption=std::min(captionWidth>0?captionWidth:s(120),std::max(0,right-left-2*button-2*gap));
    const int top=g.headings[0].top,bottom=g.headings[0].bottom;
    g.agingCaption={left,top,left+caption,bottom};
    const int offLeft=std::min(right,g.agingCaption.right+gap);
    g.agingOff={offLeft,top,std::min(right,offLeft+button),bottom};
    g.agingOn={g.agingOff.right,top,std::min(right,g.agingOff.right+button),bottom};
    g.batteryVoltageSummary={std::min(right,g.agingOn.right+gap),top,right,bottom};
    const int rowRight=g.portDetail.right;
    const int identity=std::min(std::max(identityWidth,s(260)),std::max(0,g.portDetail.width()/2));
    g.batteryDeviceIdentity={rowRight-identity,g.portDetail.top,rowRight,g.portDetail.bottom};
}
inline void placeLoggerFields(SetsGeometry& g,Scale s,std::array<int,3> widths){
    if(g.batteryComparison.width()<=0)return;
    const auto r=g.batteryComparison;const int top=r.top+s(8),bottom=top+s(22),right=r.right-s(12);
    g.loggerSuffix={right-widths[2],top,right,bottom};
    const auto value=numericFieldGeometry({g.loggerSuffix.left-s(6)-s(68),top,g.loggerSuffix.left-s(6),bottom},s(14));
    g.loggerValueEdit=value.edit;g.loggerValueSpin=value.spin;
    g.loggerMiddle={value.edit.left-s(6)-widths[1],top,value.edit.left-s(6),bottom};
    const auto address=numericFieldGeometry({g.loggerMiddle.left-s(6)-s(84),top,g.loggerMiddle.left-s(6),bottom},s(14));
    g.loggerAddressEdit=address.edit;g.loggerAddressSpin=address.spin;
    g.loggerPrefix={address.edit.left-s(6)-widths[0],top,address.edit.left-s(6),bottom};
    g.loggerHeading={r.left+s(12),top,g.loggerPrefix.left-s(12),bottom};
    g.loggerFormats={r.left+s(12),r.top+s(34),right,r.top+s(58)};
    g.loggerMeaning={r.left+s(12),r.top+s(60),right,r.top+s(102)};
    g.loggerKnown={r.left+s(12),r.top+s(104),right,r.top+s(178)};
    g.loggerMulti={r.left+s(12),r.top+s(180),right,r.top+s(320)};
}
template<std::size_t N> struct BatteryRowsGeometry {std::array<Rect,N> labels{},values{};};
// Shared inline table layout for both primary and diagnostic battery cards.
// Every native adapter supplies widths measured with its current body font.
template<std::size_t N>
inline void placeBatteryTableRows(BatteryRowsGeometry<N>& result,Rect card,Scale s,int rowsTop,int rowHeight,
        std::size_t first,std::size_t count,const std::array<int,N>& widths,int rowGap=-1,int paddingDip=kCardPadding){
    const int padding=s(paddingDip),available=std::max(0,card.width()-2*padding);
    int measured{};for(std::size_t i=first;i<first+count&&i<N;++i)measured=std::max(measured,widths[i]);
    const int maximum=available/2;
    const int valueWidth=std::clamp(measured+s(4),std::min(s(72),maximum),maximum);
    const int valueLeft=card.right-padding-valueWidth;
    const int gap=rowGap<0?s(4):rowGap;
    for(std::size_t row=0;row<count&&first+row<N;++row){
        const int top=card.top+rowsTop+static_cast<int>(row)*rowHeight;
        result.labels[first+row]={card.left+padding,top,valueLeft-s(8),top+rowHeight-gap};
        result.values[first+row]={valueLeft,top,card.right-padding,top+rowHeight-gap};
    }
}
using BatteryInfoRowsGeometry=BatteryRowsGeometry<primaryBatteryRows>;
struct BatteryPrimaryRowsGeometry {
    std::array<BatteryRowsGeometry<6>,2> current;
    BatteryInfoRowsGeometry details;
};
inline BatteryPrimaryRowsGeometry batteryPrimaryRowsGeometry(const SetsGeometry& g,Scale s,
        const std::array<std::array<int,6>,2>& currentWidths,const std::array<int,primaryBatteryRows>& detailWidths){
    BatteryPrimaryRowsGeometry result;
    constexpr int detailCount=(primaryBatteryRows+1)/2;
    for(int column=0;column<2;++column){
        std::array<int,6+detailCount> widths{};
        for(int i=0;i<6;++i)widths[i]=currentWidths[column][i];
        for(int i=0;i<detailCount;++i)widths[6+i]=detailWidths[column*detailCount+i];
        BatteryRowsGeometry<6+detailCount> rows;
        placeBatteryTableRows(rows,g.batteryInfoCards[column],s,s(kBatteryInfoHeaderHeight),
            g.batteryInfoRowHeight,0,widths.size(),widths,0);
        for(int i=0;i<6+detailCount;++i){
            // Matching rows: readings -> capacities/health -> EC time or OS policy.
            const int offset=(i>=6?s(6):0)+(i>=8?s(6):0);
            auto label=rows.labels[i],value=rows.values[i];
            label.top+=offset;label.bottom+=offset;value.top+=offset;value.bottom+=offset;
            if(i<6){result.current[column].labels[i]=label;result.current[column].values[i]=value;}
            else{result.details.labels[column*detailCount+i-6]=label;result.details.values[column*detailCount+i-6]=value;}
        }
    }
    return result;
}
inline BatteryRowsGeometry<ecBatteryDetailRows> batteryDetailRowsGeometry(const SetsGeometry& g,Scale s,int column,
        const std::array<int,ecBatteryDetailRows>& widths){
    BatteryRowsGeometry<ecBatteryDetailRows> result;
    const auto& block=g.batteryDetailColumns[column];
    placeBatteryTableRows(result,block.card,s,g.batteryDetailRowsTop,g.batteryDetailRowHeight,0,static_cast<std::size_t>(block.rows),widths,0);
    return result;
}

// Контракт SETs: width — полная ширина вкладки, не width-scrollLane.
// Требование владельца24.10.2026: НЕ сдвигать/НЕ сужать карточки ради пустой
// полосы справа. Все внешние карточки, включая Logger, имеют общие края;
// отступы вложенных карточек не являются отступом их внешнего контейнера.
// Карточки используют общие боковые отступы; правый scrollbar накладывается
// отдельным HWND на край viewport и не участвует в расчёте ширины карточек.
// Ряд APU: мощность слева, контролы Fast/Slow/STAPM прижаты к Manual справа.
// Меняя строки, согласованно обновлять их RECT, высоту карточки и g.extent.
// Геометрия остальных вкладок независима: не менять общий padding ради SETs.
inline SetsGeometry setsGeometry(int width, Scale s = {}, TextMetrics bodyMetrics = {}, int apuRows = 0,
                                 bool compactBatteryRow = false, bool manualPowerControls = false, bool batteryDiagnostics = false,
                                 std::array<int,3> cycleTextWidths = {}, std::array<int,3> brightnessTextWidths = {}, int brightnessMaximum = 4,
                                 std::array<int,2> diagnosticRows = {ecBatteryDetailRows,osBatteryDetailRows}, int powerButtonCount = 4,int policyButtonCount = 2,
                                 std::array<bool,3> visibleCards = {true,true,true},int powerOptionRows=0) {
    SetsGeometry g;
    if (bodyMetrics.height <= 0) bodyMetrics = textMetrics(FontRole::ClassicBody, s.dpi);
    const int pad = s(kContentPadding), gap = s(kCardGap);
    // Reference SETs frame: viewport width is FULL width; outer sides12 DIP,
    // content sides24 DIP (=12 outside+12 inside), first card top8, gap6.
    // These are distinct coordinate spaces. Logger is an OUTER card too.
    const auto outerCard=[&](int top,int bottom){return Rect{pad,top,width-pad,bottom};};
    const int left = 2 * pad, right = width - 2 * pad, inner = right - left;
    const int segmentWidth = (inner - s(6) * 2) / 3;
    int y = s(kPageTopPadding);
    const int infoRows=(primaryBatteryRows+1)/2;
    g.batteryInfoRowHeight=std::max(1,bodyMetrics.height);
    g.batteryInfoReadingHeight=6*g.batteryInfoRowHeight;
    g.batteryInfoRowsTop=s(kBatteryInfoHeaderHeight)+g.batteryInfoReadingHeight+s(6);
    const int nestedHeight=g.batteryInfoRowsTop+infoRows*g.batteryInfoRowHeight+s(6)+s(6);
    const int portBottom=compactBatteryRow?s(96):s(92)+bodyMetrics.height;
    const int batteryHeight=portBottom+s(16)+nestedHeight+s(12);
    powerOptionRows=std::clamp(powerOptionRows,0,2);
    const int heights[3]{0, (manualPowerControls ? 74 : 118) + 22 * apuRows+32*powerOptionRows, 110};
    // Keep logical indexes for the shared renderers; change visual order only:
    // CPU/APU power -> keyboard lighting -> battery (and its diagnostics).
    for (const int card : settingsCardOrder) {
        if(!visibleCards[card])continue;
        const int cardHeight=card==0?batteryHeight:s(heights[card]);
        g.cards[card] = outerCard(y,y+cardHeight);
        g.headings[card] = {left, y + s(8), right, y + s(30)};
        const int detailTop = card == 0 ? 72 : 90;
        if (card != 2) g.details[card] = {left, y + s(detailTop), right, y + s(detailTop + 18)};
        for (int i = 0; i < 3; ++i) {
            const int x = left + i * (segmentWidth + s(6));
            const int buttonTop = y + s(card == 0 ? 34 : 32);
            Rect r{x, buttonTop, x + segmentWidth, buttonTop + s(kButtonHeight)};
            if (card == 1) {
                g.performance[i] = r;
                g.performanceDetails[i] = {x, r.bottom + s(2), x + segmentWidth, r.bottom + s(18)};
            }
            else if (card == 2) g.effects[i] = r;
        }
        if (card == 0) {
            // Four equal columns in the existing row; card/scroll bounds stay fixed.
            const int buttonWidth = (inner - s(6) * (batteryThresholdCount - 1)) / batteryThresholdCount;
            for (int i = 0; i < batteryThresholdCount; ++i) {
                const int x = left + i * (buttonWidth + s(6));
                g.battery[i] = {x, y + s(34), i + 1 == batteryThresholdCount ? right : x + buttonWidth,
                    y + s(34) + s(kButtonHeight)};
            }
            // EC data on the left, native OS data/base policy on the right.
            // Two visible nested cards, each with inline caption/value rows.
            const int columnGap=s(12),columnWidth=(inner-columnGap)/2;
            for(int column=0;column<2;++column){
                const int start=left+column*(columnWidth+columnGap);
                const int end=column==1?right:start+columnWidth;
                const int top=y+portBottom+s(16);
                g.batteryInfoCards[column]={start,top,end,top+nestedHeight};
                g.batteryInfoHeadings[column]={start+s(kCardPadding),top+s(8),end-s(kCardPadding),top+s(30)};
                g.batteryInfoReadings[column]={start+s(kCardPadding),top+s(kBatteryInfoHeaderHeight),
                    end-s(kCardPadding),top+s(kBatteryInfoHeaderHeight)+g.batteryInfoReadingHeight};
            }
            // Charger information only. Experimental power-priority buttons removed.
            // Use full card width and retain one compact line under charge presets.
            if (compactBatteryRow) {
                g.details[0] = {};
                g.portDetail = {left, y + s(72), right, y + s(96)};
            } else {
                g.portDetail = {left, g.details[0].bottom + s(2), right,
                    g.details[0].bottom + s(2) + bodyMetrics.height};
            }
        }
        if (card == 2) {
            const int effectWidth=(right-left-s(18))/4;
            for(int i=0;i<4;++i){const int x=left+i*(effectWidth+s(6));g.effects[i]={x,y+s(32),x+effectWidth,y+s(32)+s(kButtonHeight)};}
            g.colorCaption = {left, y + s(70), left + s(42), y + s(94)};
            const int swatches = left + s(48);
            for (int i = 0; i < 8; ++i) {
                const int x = swatches + i * (s(24) + s(6));
                g.colors[i] = {x, y + s(68), x + s(24), y + s(68) + s(kButtonHeight)};
            }
            const int custom = swatches + 8 * (s(24) + s(6));
            g.customColor = {custom, y + s(68), custom + s(92), y + s(68) + s(kButtonHeight)};
            const int cycleX=g.colors[6].left,cycleY=g.colors[6].top;
            const int cycleLabelWidth=cycleTextWidths[0]>0?cycleTextWidths[0]:s(50);
            const int cycleFieldLeft=cycleX+cycleLabelWidth+s(6);
            const int cycleControlTop=cycleY+s((kButtonHeight-kKeyboardTimeoutControlSize)/2);
            const int cycleControlBottom=cycleControlTop+s(kKeyboardTimeoutControlSize);
            g.cycleCaption={cycleX,cycleY,cycleX+cycleLabelWidth,g.colors[6].bottom};
            const auto cycleField=numericFieldGeometry({cycleFieldLeft,cycleControlTop,
                cycleFieldLeft+std::max(s(44),cycleTextWidths[2]+s(8))+s(14),cycleControlBottom},s(14));
            g.cycleEdit=cycleField.edit;g.cycleSpin=cycleField.spin;
            g.cycleUnit={g.cycleSpin.right+s(6),cycleY,g.cycleSpin.right+s(6)+std::max(s(24),cycleTextWidths[1]),g.colors[6].bottom};
            constexpr int checkLeft = 76;
            constexpr int editLeft = checkLeft + kKeyboardTimeoutControlSize + 10;
            constexpr int editRight = editLeft + 44;
            constexpr int unitLeft = editRight + 14 + 6;
            const int timeoutLeft = right - s(unitLeft + 24);
            // Auto-off shares the heading row; effects keep their original row.
            const int timeoutTop = y + s(8);
            const int controlTop = timeoutTop;
            const int controlBottom = controlTop + s(kKeyboardTimeoutControlSize);
            g.keyboardTimeoutCaption = {timeoutLeft, timeoutTop, timeoutLeft + s(70), controlBottom};
            g.keyboardTimeoutCheck = {timeoutLeft + s(checkLeft), controlTop,
                timeoutLeft + s(checkLeft) + s(kKeyboardTimeoutControlSize), controlBottom};
            const auto timeoutField=numericFieldGeometry({timeoutLeft+s(editLeft),controlTop,
                timeoutLeft+s(editRight)+s(14),controlBottom},s(14));
            g.keyboardTimeoutEdit=timeoutField.edit;g.keyboardTimeoutSpin=timeoutField.spin;
            g.keyboardTimeoutUnit = {timeoutLeft + s(unitLeft), timeoutTop, right, controlBottom};
            // Auto-off UI removed for every model. Only heading's available
            // width is restored; card/control positions and extent stay fixed.
            g.headings[card].right = right;
            const int maximum=std::clamp(brightnessMaximum,0,4),brightnessGap=s(4);
            const int defaultWidth=std::max(s(68),brightnessTextWidths[0]+s(20));
            const int offWidth=std::max(s(50),brightnessTextWidths[1]+s(16));
            const int digitWidth=std::max(s(28),brightnessTextWidths[2]+s(16));
            const int total=defaultWidth+offWidth+maximum*digitWidth+(maximum+1)*brightnessGap;
            int x=right-total;const int top=y+s(68),bottom=top+s(kButtonHeight);
            g.nativeButton={x,top,x+defaultWidth,bottom};x+=defaultWidth+brightnessGap;
            for(int level=0;level<=maximum;++level){const int cell=level==0?offWidth:digitWidth;
                g.brightnessLevels[level]={x,top,x+cell,bottom};x+=cell+brightnessGap;}
        }
        if (card == 1 && apuRows > 0)
            for (std::size_t row = 0; row < g.apuDiagnostics.size(); ++row)
                g.apuDiagnostics[row] = {left, y + s((manualPowerControls ? 68 : 112) + 22 * static_cast<int>(row)),
                                        right, y + s((manualPowerControls ? 88 : 132) + 22 * static_cast<int>(row))};
        if (card == 1 && manualPowerControls) {
            // EC policy is an APU header switch, never a KEYs row.
            // Only extend this existing header group left for the third choice.
            // Card bounds/heights, viewport, gutters and scroll offsets stay exact.
            const bool three=policyButtonCount==3;
            const int first=right-s(three?192:128);
            g.apuPolicyOff = {first, y+s(4),first+s(64),y+s(30)};
            g.apuPolicyOn = {first+s(64),y+s(4),first+s(128),y+s(30)};
            if(three)g.apuPolicyOriginal={first+s(128),y+s(4),right,y+s(30)};
            g.apuPolicyCaption = {first-s(142), y+s(4),first-s(12),y+s(30)};
            g.headings[1].right = g.apuPolicyCaption.left - s(12);
            const int count=std::clamp(powerButtonCount,2,5);
            const int profileWidth = (inner - s(6) * (count-1)) / count;
            for (int i = 0; i < count; ++i) {
                const int x = left + i * (profileWidth + s(6));
                const int end = i == count-1 ? right : x + profileWidth;
                // Keep 8 DIP clear below the EC policy switch (bottom=30).
                g.performance[i] = {x, y + s(38), end, y + s(38 + kButtonHeight)};
                g.performanceDetails[i] = {x, y + s(38 + kButtonHeight + 2), end, y + s(88)};
            }
            const int rowTop = y + s(74);
            const int customRight=right-3*(s(kButtonHeight)+s(6));
            g.manualPowerButton = {customRight - s(kCustomPowerWidth), rowTop, customRight, rowTop + s(kButtonHeight)};
            for(int i=0;i<3;++i){const int x=customRight+s(6)+i*(s(kButtonHeight)+s(6));
                g.customPowerPresets[i]={x,rowTop,x+s(kButtonHeight),rowTop+s(kButtonHeight)};}
            constexpr int powerFieldWidth=60+kPowerEditWidth+kPowerArrowWidth;
            const int fieldsLeft = g.manualPowerButton.left - s(kPowerFieldGap) - s(powerFieldWidth * 3 + kPowerFieldGap * 2);
            g.apuDiagnostics[0] = {left, rowTop, fieldsLeft - s(12), rowTop + s(kButtonHeight)};
            g.apuDiagnostics[1] = {};
            g.apuDiagnostics[2] = {left, rowTop + s(kButtonHeight + 6+32*powerOptionRows), right,
                                   rowTop + s(kButtonHeight + 26+32*powerOptionRows)};
            // APU: right-aligned caption touches its editor; 8 DIP after each control.
            const int fieldWidth = s(powerFieldWidth);
            for (int i = 0; i < 3; ++i) {
                const int fieldLeft = fieldsLeft + i * (fieldWidth + s(8));
                const int fieldTop = rowTop + s((kButtonHeight - 22) / 2);
                g.powerCaptions[i] = {fieldLeft, fieldTop, fieldLeft + s(60), fieldTop + s(22)};
                const auto field=numericFieldGeometry({fieldLeft+s(60),fieldTop,fieldLeft+fieldWidth,fieldTop+s(22)},s(kPowerArrowWidth));
                g.powerEdits[i]=field.edit;g.powerSpins[i]=field.spin;
            }
        }
        y += cardHeight + gap;
        if(card==0&&batteryDiagnostics){
            // Logger shares the SAME outer12-DIP sides as power/light/battery.
            // Its two children have ONE12-DIP inset: viewport x24..width-24,
            // matching the two primary battery child cards. Gap between them12.
            // Keep the existing328-DIP editor area/vertical positions unchanged.
            //4c4a95e incorrectly used x24..width-24 for the OUTER card and then
            // inset its children again. Do not restore that extra empty gutter.
            const int loggerTop=y,nestedTop=y+s(328),nestedLeft=left,nestedRight=right;
            const int columnGap=s(12),columnWidth=(nestedRight-nestedLeft-columnGap)/2;
            g.batteryDetailRowHeight=std::max(1,bodyMetrics.height);
            g.batteryDetailRowsTop=s(30)+g.batteryDetailRowHeight+s(2);
            int bottom=nestedTop;
            for(int column=0;column<2;++column){
                auto& block=g.batteryDetailColumns[column];
                const int start=nestedLeft+column*(columnWidth+columnGap),end=column==1?nestedRight:start+columnWidth;
                block.rows=std::clamp(diagnosticRows[column],0,ecBatteryDetailRows);
                const int tail=column==0?2*g.batteryDetailRowHeight+s(8):s(6);
                const int edge=nestedTop+g.batteryDetailRowsTop+block.rows*g.batteryDetailRowHeight+tail;
                block.card={start,nestedTop,end,edge};
                block.heading={start+s(kCardPadding),nestedTop+s(6),end-s(kCardPadding),nestedTop+s(28)};
                block.status={start+s(kCardPadding),nestedTop+s(30),end-s(kCardPadding),nestedTop+s(30)+g.batteryDetailRowHeight};
                if(column==0)block.note={start+s(kCardPadding),edge-tail+s(4),end-s(kCardPadding),edge-s(4)};
                bottom=std::max(bottom,edge);
            }
            g.batteryComparison=outerCard(loggerTop,bottom+s(12));
            y=g.batteryComparison.bottom+gap;
        }
    }
    g.status = {}; // Operation messages are painted by the application footer.
    g.extent = y + pad;
    return g;
}

struct BiosHeaderGeometry {
    Rect card, heading, snapshot, update, inventory;
    std::array<Rect, kBiosActionCount> buttons{};
    int bottom{};
};

inline BiosHeaderGeometry biosHeaderGeometry(int width, int height, int snapshotHeight,
                                             int updateHeight, Scale s = {},
                                             std::array<int, 5> primaryTextWidths = {}) {
    BiosHeaderGeometry g;
    const int pad = s(12), inner = width - 4 * pad, gap = s(6);
    int y = s(kPageTopPadding) + s(8);
    g.heading = {pad * 2, y, width - pad * 2, y + s(24)}; y += s(30);
    const int third = (inner - gap * 2) / 3;
    // Owner's order: driver -> information -> EC -> BIOS -> VBIOS.
    // Repartition ONLY this existing row. Keep pad12/gap6/height30 DIP,
    // card bounds, snapshot/update/secondary rows, inventory Y and scroll
    // lane exactly as before. Never add a scrollbar gutter or another row.
    // Native adapters measure the current captions with the actual font/DPI.
    constexpr std::array weights{2, 2, 1, 1, 1};
    std::array<int, 5> minimum{};
    int minimumTotal = 0;
    for (std::size_t i = 0; i < minimum.size(); ++i) {
        minimum[i] = std::max(s(36), primaryTextWidths[i] + s(12));
        minimumTotal += minimum[i];
    }
    const int available = std::max(0, inner - gap * 4);
    int actionLeft = pad * 2;
    for (std::size_t i = 0; i < minimum.size(); ++i) {
        const int cell = available >= minimumTotal
            ? minimum[i] + (available - minimumTotal) * weights[i] / 7
            : available * minimum[i] / std::max(1, minimumTotal);
        const int right = i + 1 == minimum.size() ? width - pad * 2 : actionLeft + cell;
        g.buttons[kBiosPrimaryActions[i]] = {actionLeft, y, right, y + s(kButtonHeight)};
        actionLeft = right + gap;
    }
    y += s(kButtonHeight) + gap;
    g.snapshot = {pad * 2, y, width - pad * 2, y + snapshotHeight}; y += snapshotHeight + s(3);
    g.update = {pad * 2, y, width - pad * 2, y + updateHeight}; y += updateHeight + s(3);
    for (int i = 0; i < 3; ++i) {
        const int x = pad * 2 + i * (third + gap);
        g.buttons[i + 2] = {x, y, i == 2 ? width - pad * 2 : x + third, y + s(kButtonHeight)};
    }
    g.bottom = y + s(kButtonHeight) + pad;
    g.card = {pad, s(kPageTopPadding), width - pad, g.bottom};
    g.inventory = {pad, g.bottom + s(6), width - pad, height - pad};
    return g;
}

struct InventoryCardGeometry {
    Rect bounds, heading;
    std::vector<Rect> captions, values;
};

inline InventoryCardGeometry inventoryCardGeometry(int width, int top, int headingHeight,
                                                    const std::vector<int>& rowHeights, Scale s = {}) {
    InventoryCardGeometry g;
    const int pad = s(kContentPadding), inner = width - 2 * pad, column = inner * 28 / 100;
    int y = top + s(10);
    g.heading = {pad, y, pad + inner, y + headingHeight};
    y += headingHeight + s(8);
    for (int h : rowHeights) {
        g.captions.push_back({pad, y, pad + column - s(kInventoryColumnGap), y + h});
        g.values.push_back({pad + column, y, pad + inner, y + h});
        y += h + s(kInventoryRowGap);
    }
    y += s(10) - (rowHeights.empty() ? 0 : s(kInventoryRowGap));
    g.bounds = {0, top, width, y};
    return g;
}

}  // namespace mc::gui::ui
