// MagicControl - shared test helpers.
//
// Both test translation units report through the same counter so a single
// binary prints one summary, and a failure in the scroll arithmetic is not
// buried among the register checks.
#pragma once

#include <cstdio>

namespace mc::tests {

/// Incremented by every check, and by every failure.
int& checkCount() noexcept;
int& failureCount() noexcept;

/// Records one check. The CHECK macro supplies the expression text and location
/// so a failure names the condition that broke rather than only a line number.
inline void check(bool condition, const char* expression, const char* file, int line) {
    ++checkCount();
    if (!condition) {
        ++failureCount();
        std::printf("FAIL %s:%d  %s\n", file, line, expression);
    }
}

/// Runs the scroll model suite. Defined in test_scroll_model.cpp.
int runScrollModelTests();
int runCoreSafetyTests();
int runLoggingTests();
int runFanPresetTests();
int runTemperatureTests(const char* samplesPath, const char* replayPath);

}  // namespace mc::tests

#define CHECK(expr) ::mc::tests::check((expr), #expr, __FILE__, __LINE__)
