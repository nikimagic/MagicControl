#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace mc::hydroc16g2 {
// Exact Eluktronics ControlCenter5.23.51.56 / recovered GCUService:
// ITE_SPEC.cs:10–24, LM_ITE_RGB.cs:4115–4299, HIDKeyboardFactory.cs:13–83.
// This describes descriptor candidates ONLY, including on Unknown hardware.
// No device is opened and no Feature Report, firmware query or write is sent.
inline constexpr std::uint16_t keyboardHidVendor=0x048D;
inline constexpr std::uint16_t keyboardHidMinimumFeatureBytes=9;
inline constexpr std::array<std::uint16_t,10> keyboardHidPids{
    0xCE00,0x6000,0x6001,0x6002,0x6003,0x6004,0x6006,0x6007,0x600A,0x600B};
inline constexpr std::array<std::uint16_t,5> lightbarHidPids{
    0x7001,0x7000,0x6005,0x6008,0x6010};

struct KeyboardHidDescriptor {
    std::uint16_t vid{},pid{},revision{},usagePage{},usage{},featureBytes{};
    // revision is HIDD_ATTRIBUTES.VersionNumber (USB bcdDevice). It is NEVER
    // substituted for the four firmware-version bytes in native report80H.
};
enum class KeyboardHidFamily {Rejected,FourZone,PerKeyFirst,PerKeyNew,Lightbar};
struct KeyboardHidCandidate {
    KeyboardHidFamily family{KeyboardHidFamily::Rejected};
    bool candidate{},keyboard{},recognizedVendor{},recognizedPid{},knownKeyboardPid{};
    bool needsFw80{},needsEc73c{},needsProjectId{},calibrationUnknown{true};
    bool permission{}; // Always false: neither model recognition nor evidence grants a writer.
    std::string_view typeProvisional{"unknown"},status{"Not a reviewed ITE descriptor"};
};
template<std::size_t N>
constexpr bool keyboardHidContains(const std::array<std::uint16_t,N>& values,std::uint16_t value) noexcept {
    for(const auto entry:values)if(entry==value)return true;
    return false;
}
constexpr KeyboardHidCandidate keyboardHidCandidate(const KeyboardHidDescriptor& descriptor) noexcept {
    KeyboardHidCandidate result;
    if(descriptor.vid!=keyboardHidVendor)return result;
    result.recognizedVendor=true;
    if(keyboardHidContains(lightbarHidPids,descriptor.pid)){
        result.recognizedPid=true;result.family=KeyboardHidFamily::Lightbar;
        result.typeProvisional="separate native lightbar PID; not a keyboard";
        result.status="Rejected for keyboard control: lightbar has its own protocol/type routing";
        return result;
    }
    if(!keyboardHidContains(keyboardHidPids,descriptor.pid)){
        result.status="Rejected: ITE PID is outside the exact OEM keyboard allowlist";return result;
    }
    result.recognizedPid=true;result.knownKeyboardPid=true;
    if(descriptor.usage!=1){result.status="Rejected: native keyboard Usage must be0x0001";return result;}
    // Nine meaningful bytes are used by80/88/08/09/14. The actual HID feature
    // size is not proved to be only9 or65: do not invent such a size allowlist.
    // A future typed transport must allocate the descriptor's complete size.
    if(descriptor.featureBytes<keyboardHidMinimumFeatureBytes){
        result.status="Rejected: FeatureReportByteLength is smaller than the native9-byte packet";return result;
    }
    switch(descriptor.usagePage){
    case 0xFF12:
        result.family=KeyboardHidFamily::FourZone;result.needsProjectId=true;
        result.typeProvisional="FourZone; Project17 exception FourZoneSingleColor is unresolved";
        break;
    case 0xFF02:
        result.family=KeyboardHidFamily::PerKeyFirst;
        result.typeProvisional="MEZone_1st or FWmajor32 MEZone_3nd_98; requires report80H";
        break;
    case 0xFF03:
        result.family=KeyboardHidFamily::PerKeyNew;result.needsEc73c=true;
        result.typeProvisional="MEZone2nd/2p1/2p2/3nd/3p1 unresolved until report80H plus EC73C";
        break;
    default:
        result.status="Rejected: UsagePage is outside exact OEM FF12/FF02/FF03 keyboard routing";return result;
    }
    result.candidate=true;result.keyboard=true;result.needsFw80=true;
    result.status="Descriptor candidate only; firmware/type/calibration and physical response remain unconfirmed";
    return result;
}
constexpr std::string_view keyboardHidFamilyName(KeyboardHidFamily family) noexcept {
    switch(family){
    case KeyboardHidFamily::FourZone:return "four-zone";
    case KeyboardHidFamily::PerKeyFirst:return "per-key-first-page";
    case KeyboardHidFamily::PerKeyNew:return "per-key-new-page";
    case KeyboardHidFamily::Lightbar:return "lightbar-not-keyboard";
    default:return "rejected";
    }
}
inline std::string keyboardHidCandidateText(const KeyboardHidCandidate& result){
    // Unrelated HID devices already have their ordinary descriptor rows; avoid
    // a noisy extra annotation for every mouse/touchpad from another vendor.
    if(!result.recognizedVendor)return {};
    const auto boolean=[](bool value){return value?"true":"false";};
    std::string out="HYDROC exact OEM descriptor evidence: family=";
    out+=keyboardHidFamilyName(result.family);
    out+="; candidate=";out+=boolean(result.candidate);
    out+="; keyboard=";out+=boolean(result.keyboard);
    out+="; type_provisional=";out+=result.typeProvisional;
    out+="; needs_FW80=";out+=boolean(result.needsFw80);
    out+="; needs_EC73C=";out+=boolean(result.needsEc73c);
    out+="; needs_ProjectID=";out+=boolean(result.needsProjectId);
    out+="; calibration_unknown=";out+=boolean(result.calibrationUnknown);
    out+="; permission=false; bcdDevice_revision_is_not_FW80; ";out+=result.status;
    return out;
}
}
