#pragma once

namespace mc {
// Ephemeral presentation state only. Never interpreted as device readback or
// passed to a resource worker, a persistence backend, or a hardware transport.
struct SettingsPreviewState {
    int power{-1};
    int preset{-1};
    int effect{-1};
    int color{-1};
    int brightness{-1};
    int batteryUpper{-1};
    int batteryMode{-1};
    int batteryAging{-1};
    bool customEditing{};

    void stockPower(int value) noexcept { power=value; customEditing=false; }
    void customPower(int customIndex) noexcept { power=customIndex; customEditing=true; if(preset<0)preset=0; }
    void customPreset(int value,int customIndex) noexcept { if(value>=0&&value<3){preset=value;customPower(customIndex);} }
};
}
