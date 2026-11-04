#pragma once
#include <optional>
#include <string>
namespace mc {
struct MicrophoneState {
    std::optional<bool> enabled;
    bool available{};
    std::string detail;
};
// Default capture endpoint only: Windows communications role / Linux default source.
// No audio recording, device disable, EC command or startup restoration.
MicrophoneState readDefaultMicrophone();
void setDefaultMicrophoneEnabled(bool enabled);
}
