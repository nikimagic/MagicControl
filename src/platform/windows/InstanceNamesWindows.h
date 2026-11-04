#pragma once
namespace mc::windows {
// One cooperative owner for GUI and SSH control commands. Read-only CLI
// requests do not claim this lease. CLI never requests GUI handoff/forced exit.
inline constexpr wchar_t applicationInstanceMutex[]=L"Global\\MagicControl.SingleInstance.v2";
}
