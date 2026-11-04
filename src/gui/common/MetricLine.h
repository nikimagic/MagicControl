#pragma once

#include <array>
#include <string>
#include <vector>

#include "Geometry.h"

namespace mc::gui::ui {
struct InlineMetric {
    std::string label, value;
};
struct MetricText {
    std::string text;
    Rect bounds;
    bool value{};
};
struct MetricLineLayout {
    std::vector<MetricText> parts;
    int height{};
};

// Keep each value with its unit, and use the same wrapping for both renderers.
template<class Measure>
MetricLineLayout metricLineLayout(const std::array<InlineMetric,3>& metrics,
                                  Rect bounds, int lineHeight, Measure measure) {
    MetricLineLayout result;
    int x = bounds.left, y = bounds.top;
    const std::string separator = " · ";
    const int separatorWidth = measure(separator);
    const auto nextLine = [&] { x = bounds.left; y += lineHeight; };
    const auto append = [&](const std::string& text, int width, bool value) {
        if (text.empty()) return;
        result.parts.push_back({text, {x, y, std::min(bounds.right, x + width), y + lineHeight}, value});
        x += width;
    };
    for (const auto& metric : metrics) {
        const int labelWidth = measure(metric.label), valueWidth = measure(metric.value);
        if (x > bounds.left) {
            if (x + separatorWidth + labelWidth + valueWidth > bounds.right) nextLine();
            else append(separator, separatorWidth, false);
        }
        append(metric.label, labelWidth, false);
        if (x > bounds.left && x + valueWidth > bounds.right) nextLine();
        append(metric.value, valueWidth, true);
    }
    result.height = y - bounds.top + lineHeight;
    return result;
}
} // namespace mc::gui::ui
