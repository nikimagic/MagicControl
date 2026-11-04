#include "../../../../protocols/uniwill/x5sp4nag/BatteryContract.h"
#include "BatteryChargeWindows.h"
#include "../../../../platform/windows/BatteryEnforcementWindows.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/PortableSettings.h"
#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <exception>
#include <format>
#include <optional>
#include <stdexcept>
#include <thread>

namespace mc::windows {
namespace batteryContract=mc::uniwill::x5sp4nag;
// BATTERY CONTRACT — read BATTERY.md before editing this file.
// Exact board 82/72/26 only. Percentage control is independent of ALL fan modes.
// Writes: upper/lower/WMI plus explicit4+ROMFF /7+owned RAM ROM04 policy.
// Never write fan gates, PWM, tables, other ROM bytes,742 status or7A6 profile.
// Stored percentage != enabled enforcement != physically stopped charging.
// KNOWN SIDE EFFECT (2026-10-16 A/B): platform 7->4 changes DC Office SMU
// Fast/Slow/STAPM 30/25/25 -> 45/40/40; repeated 4/7/4/7/4, fan flags/tables stable.
// AC 65W Office/Balanced/Turbo showed no platform effect in this test.
// 0x7C3 is NOT battery-only. Evidence: tests/test_util/analysis/
// battery-platform-smu-20261016/REPORT.md. Do not conceal this with automatic APU
// override writes or change the platform policy without discussing charge behavior.
// Full DC sweep: platform 4 collapses Office/Balanced/Turbo to 45/40/40;
// platform 7 gives 30/25/25, 40/35/35, 60/54/54 (Fast/Slow/STAPM).
// Manual test 25/20/15 was retained on DC/AC at both platforms; see ALL-MODES.md.
// AC 100W also affected: platform 4 Office=35/30/30, Balanced/Turbo=55/50/50;
// platform 7 Office=30/25/25, Balanced=50/45/45, Turbo=70/65/65 (Fast/Slow/STAPM).
// Evidence: tests/test_util/analysis/charger-rating-smu-20261016/REPORT.md.
// Native Battery Class matrix near 85% (100W): platform4 charge=0W, platform7
// charge~35W in Office/Balanced/Turbo/Manual; 742.2 clears, 7B9.Reached clears.
// Historical restart80 was only readback-confirmed. User reported restart at84;
// do NOT claim working 80/85 hysteresis without observing a real restart cycle.
namespace {
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;
// 0x7B9: low 7 bits percentage (0=100%), high bit firmware Reached (preserve).
// 0x7C3: whole-byte platform, NOT a flag mask. Reference-tested 7->4 activation
// uses original ROMFF; platform7 uses explicitly owned RAM ROM04. No save trigger.
constexpr std::uint16_t limitAddress = 0x7B9, platformAddress = 0x7C3;
struct Bstr {
    BSTR value;
    explicit Bstr(const wchar_t* s) : value(SysAllocString(s)) { if (!value) throw std::bad_alloc(); }
    ~Bstr() { SysFreeString(value); }
};
struct Variant {
    VARIANT value;
    Variant() { VariantInit(&value); }
    ~Variant() { VariantClear(&value); }
};
void check(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error(std::format("Battery WMI HRESULT=0x{:08X}", static_cast<unsigned long>(hr)));
}
struct Apartment {
    HRESULT hr{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    Apartment() { if (hr != RPC_E_CHANGED_MODE) check(hr); }
    ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
class Firmware {
    Apartment apartment;
    EcAccess& ec;
    ComPtr<IWbemServices> service;
    ComPtr<IWbemClassObject> parameters;
public:
    explicit Firmware(EcAccess& access) : ec(access) {
        const auto security = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        if (security != RPC_E_TOO_LATE) check(security);
        ComPtr<IWbemLocator> locator;
        check(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)));
        Bstr space(L"ROOT\\WMI");
        check(locator->ConnectServer(space.value, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &service));
        check(CoSetProxyBlanket(service.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE));
        Bstr name(L"AcpiTest_MULong");
        ComPtr<IWbemClassObject> definition, inputs;
        check(service->GetObject(name.value, 0, nullptr, &definition, nullptr));
        check(definition->GetMethod(L"GetSetULong", 0, &inputs, nullptr));
        check(inputs->SpawnInstance(0, &parameters));
    }
    std::uint32_t invoke(std::uint64_t argument) {
        const EcAccess::Transaction transaction(ec);
        Variant data;
        data.value.vt = VT_BSTR;
        data.value.bstrVal = SysAllocString(std::to_wstring(argument).c_str());
        if (!data.value.bstrVal) throw std::bad_alloc();
        check(parameters->Put(L"Data", 0, &data.value, CIM_UINT64));
        Bstr instance(L"AcpiTest_MULong.InstanceName='ACPI\\PNP0C14\\1_1'"), method(L"GetSetULong");
        ComPtr<IWbemClassObject> output;
        check(service->ExecMethod(instance.value, method.value, 0, nullptr, parameters.Get(), &output, nullptr));
        Variant raw, converted;
        check(output->Get(L"Return", 0, &raw.value, nullptr, nullptr));
        check(VariantChangeType(&converted.value, &raw.value, 0, VT_UI4));
        const auto result = converted.value.ulVal;
        if (result == 0xFEFEFEFE) throw std::runtime_error("Battery WMI: EC transport error");
        return result;
    }
    // WMI read selector bit 40 + logical address; not ordinary EC ReadEC(0x87F).
    std::uint32_t storedWord() { return invoke(0x10000000000ULL + 0x87F); }
    std::uint8_t writeStored(std::uint8_t value, std::uint8_t mask = 0x7F,
                             std::optional<std::uint8_t> expected = {}, bool skipUnchanged = false) {
        batteryContract::requireBatteryWrite(batteryContract::BatteryWriteSpace::StoredWmi,0x87F,mask);
        for (int attempt = 0; attempt < 3; ++attempt) {
            // Read/compare/masked-write share the EC transaction. In particular,
            // rollback must preserve a later WMI threshold and the adjacent byte.
            const EcAccess::Transaction transaction(ec);
            const auto before = storedWord();
            if (expected && (before & mask) != (*expected & mask) && (before & mask) != (value & mask))
                throw std::runtime_error("Stored battery threshold changed externally; field not overwritten");
            // Forward activation may intentionally replay an equal threshold;
            // rollback does not need that side effect when the field is original.
            if (skipUnchanged && (before & mask) == (value & mask)) return static_cast<std::uint8_t>(before);
            // WKBC may write TWO bytes. Preserve the adjacent byte and every bit
            // outside the threshold mask, including during rollback.
            const auto word = batteryContract::mergeStoredBatteryWord(before,value,mask);
            invoke(0x87FULL | (static_cast<std::uint64_t>(word) << 16));
            std::this_thread::sleep_for(50ms);
            const auto after = static_cast<std::uint8_t>(storedWord());
            if ((after & mask) == (value & mask)) return after;
        }
        throw std::runtime_error("Battery WMI: stored charge limit readback mismatch");
    }
};
struct State { std::uint8_t limit, platform, rom, gate, lower; };
State read(EcAccess& ec) {
    const EcAccess::Transaction transaction(ec);
    // ROM-ID is owned by the explicit4 /7+ROM04 policy.742.2 remains status.
    return {ec.readStable(limitAddress), ec.readStable(platformAddress), ec.readStable(0x770), ec.readStable(0x742), ec.readStable(0x7D0)};
}
bool onBattery() {
    SYSTEM_POWER_STATUS status{};
    return GetSystemPowerStatus(&status) && status.ACLineStatus == 0;
}
void writePin(const std::filesystem::path& path) {
    auto temp = path; temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        out << "82/72/26 7 4\n";
        out.flush();
        if (!out) throw std::runtime_error("Cannot save original battery enforcement platform");
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot commit original battery enforcement platform");
}
}

std::string applyBatteryChargeLimit(EcAccess& ec, int percent, bool platform7,int restart,std::optional<BatteryEcPolicy> requestedPolicy,std::optional<unsigned> requiredPlatform) {
    // Legacy21.10.2026 default: lower97 for all four Wujie upper presets.
    // OEM reference Down accepts1..95;97 is an unverified experiment. Exact
    // firmware semantics (restart vs slow-charge hypothesis) are unresolved.
    // Readback proves configuration only. Never clamp97 to95 silently.
    // Preserve legacy97 until an explicit user choice has been confirmed/saved.
    if(restart<0)restart=selectedChargeRestart("wu-jie-15-xpro",ec::kRequestedBatteryLowerPercent);
    if(restart<1||restart>99)throw std::invalid_argument("Нижний порог должен быть1–99%");
    const auto lower=static_cast<std::uint8_t>(restart);
    batteryContract::requireBatteryRequest(percent,platform7);
    ec.ensureCapability(DeviceFeature::BatteryCharge, "uniwill-x5sp4nag");
    // Identity is checked again before this exact-board activation, independently of UI.
    {
        const EcAccess::Transaction initial(ec);
        batteryContract::requireBatteryIdentity(ec.boardVerified(),[&](auto address){return ec.readStable(address);});
    }
    const auto before = read(ec);
    batteryContract::requireBatteryPlatform(before.platform,before.rom);
    if(requiredPlatform&&before.platform!=*requiredPlatform)
        throw std::runtime_error("Battery platform changed before release; no settings written");
    // Ordinary GUI/CLI/restore percentages keep the actual platform. Legacy
    // ChargeAuto=2 must not leave95/85/60 merely stored behind the7/FF gate.
    // Explicit policy commands may still select their already admitted platform.
    const auto selected=batteryPolicyForChargeLimit(requestedPolicy?batteryPolicyPlatform(*requestedPolicy):before.platform,percent);
    if(!selected)throw std::runtime_error("Unsupported battery platform; no settings written");
    const auto choice=*selected;
    batteryEnforcement::Change policy(
        [&](auto field){return ec.readStable(field);},
        [&](auto field,auto mask,auto value){
            batteryContract::requireBatteryIdentity(ec.boardVerified(),[&](auto a){return ec.readStable(a);});
            batteryContract::checkedBatteryWrite(field,mask,[&]{return ec.updateBitsVerified(field,mask,value);});
        },batteryEnforcement::ownerPath(portableSettingsPath("device-settings.ini").parent_path(),"wu-jie-15-xpro"),"wu-jie-15-xpro",choice,requiredPlatform);
    const auto pinPath = settingsReadPath(portableSettingsPath("battery-enforcement-platform.txt"));
    const bool pinExists = std::filesystem::exists(pinPath);
    if (pinExists) {
        std::ifstream in(pinPath); std::string board; int original{}, active{};
        if (!(in >> board >> original >> active) || board != "82/72/26" || original != 7 || active != 4 ||
            (before.platform != 4 && before.platform != 7))
            throw std::runtime_error("Invalid original battery platform record; no settings written");
    }
    // User policy22.10.2026: keep upper/lower on4/FF or7/owned04.
    // Do not pulse4 or clear the chosen upper. Reconnect remains unmeasured.
    const auto raw = static_cast<std::uint8_t>(percent == 100 ? 0 : percent);
    const auto target = batteryPolicyPlatform(choice);
    const auto targetRom = batteryPolicyRom(choice);
    Firmware firmware(ec);
    const auto storedBefore = static_cast<std::uint8_t>(firmware.storedWord());
    // A cached 0xFF/low7>100 is not a supported percentage baseline. Reject it
    // before the first write/pin, rather than later rolling back an invalid127.
    batteryContract::requireStoredBatteryThreshold(storedBefore);
    bool changedLimit{}, changedStore{}, changedLower{}, createdPin{};
    try {
        if (target == 4 && before.platform == 7 && !pinExists) {
            writePin(pinPath); createdPin = true;
        }
        logMessage("battery", std::format("LIMIT begin: percent={} limit_before=0x{:02X} stored_before=0x{:02X} platform_before={} target={} gate_before=0x{:02X} lower_requested={} lower_before=0x{:02X}; fan writes=0",
            percent, before.limit, storedBefore, before.platform, target, before.gate, lower, before.lower));
        if ((before.limit & 0x7F) != raw) {
            const EcAccess::Transaction transaction(ec);
            if ((ec.readStable(limitAddress) & 0x7F) != (before.limit & 0x7F))
                throw std::runtime_error("Battery upper threshold changed externally before write");
            changedLimit = true;
            batteryContract::checkedBatteryWrite(limitAddress,0x7F,[&]{return ec.updateBitsVerified(limitAddress,0x7F,raw);});
        }
        // OEM SetBatteryChargingLimit_Down: low 7 bits are restart threshold,
        // high bit is preserved. The UI now supplies the lower; its physical role remains unverified.
        if ((before.lower & 0x7F) != lower) {
            const EcAccess::Transaction transaction(ec);
            if ((ec.readStable(0x7D0) & 0x7F) != (before.lower & 0x7F))
                throw std::runtime_error("Battery lower threshold changed externally before write");
            changedLower = true;
            batteryContract::checkedBatteryWrite(0x7D0,0x7F,[&]{return ec.updateBitsVerified(0x7D0,0x7F,lower);});
        }
        if ((storedBefore & 0x7F) != raw || (raw && !(before.gate & 4))) {
            changedStore = true; firmware.writeStored(raw, 0x7F, storedBefore);
        }
        {const EcAccess::Transaction transaction(ec);policy.apply();}
        // Poll without a function-wide EC transaction: online fans must keep running.
        // On battery the firmware may defer Enabled until AC is connected.
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        int confirmations{};
        State actual = before;
        bool pending{};
        do {
            actual = read(ec);
            if (actual.platform != target || actual.rom != targetRom)
                throw std::runtime_error("EC battery platform readback mismatch");
            pending = raw && !(actual.gate & 4) && onBattery();
            const bool gateMatches = !raw || (actual.gate & 4) != 0 || pending;
            const bool matches = (actual.limit & 0x7F) == raw && (actual.lower & 0x7F) == lower && gateMatches;
            confirmations = matches ? confirmations + 1 : 0;
            if (confirmations >= 3) {
                // The earlier WMI readback may have changed during settling.
                // Confirm persisted and live fields together before reporting
                // success; this is still readback, not a measured restart cycle.
                {
                    const EcAccess::Transaction confirmation(ec);
                    const auto stored = static_cast<std::uint8_t>(firmware.storedWord());
                    actual = read(ec);
                    pending = raw && !(actual.gate & 4) && onBattery();
                    const bool finalGateMatches = !raw || (actual.gate & 4) != 0 || pending;
                    if ((stored & 0x7F) != raw || (actual.limit & 0x7F) != raw ||
                        (actual.lower & 0x7F) != lower || actual.platform != target ||
                        actual.rom != targetRom || !finalGateMatches)
                        throw std::runtime_error("Battery threshold/context changed during final confirmation");
                }
                policy.commit();
                const auto result = std::format("percent={} platform={} rom=0x{:02X} gate=0x{:02X} enabled={} stored=verified pending_on_battery={} reached={} lower_configured={}% lower_role=unverified lower_execution=unverified charge_execution=readback_only; fan writes=0",
                    percent, actual.platform,actual.rom, actual.gate, (actual.gate & 4) != 0, pending, (actual.limit & 0x80) != 0, lower);
                logMessage("battery", "LIMIT confirmed: " + result);
                return result;
            }
            std::this_thread::sleep_for(matches ? 25ms : 250ms);
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("EC did not confirm battery charge-limit activation");
    } catch (...) {
        // Roll back only fields attempted by this operation and still containing
        // our masked value (or the original value after a partial failure). A
        // foreign field is left untouched; failure is returned to the caller,
        // not hidden in a log. No fan/APU/voltage-profile writes are allowed here.
        const auto failure = std::current_exception();
        std::string rollbackErrors;
        try{const EcAccess::Transaction transaction(ec);policy.rollback();}
        catch(const std::exception& error){rollbackErrors=std::string("platform/ROM-ID: ")+error.what();}
        const auto context = [&] {
            if (ec.readStable(0x770) != before.rom) throw std::runtime_error("ROM ID changed; rollback stopped");
            const auto platform = ec.readStable(platformAddress);
            if (platform != before.platform)
                throw std::runtime_error("External platform change; rollback stopped");
        };
        const auto restore = [&](const char* name, auto action) {
            try {
                const EcAccess::Transaction transaction(ec);
                context();
                action();
            } catch (const std::exception& error) {
                if (!rollbackErrors.empty()) rollbackErrors += "; ";
                rollbackErrors += std::string(name) + ": " + error.what();
            }
        };
        const auto restoreBits = [&](std::uint16_t address, std::uint8_t mask,
                                     std::uint8_t ours, std::uint8_t original) {
            batteryContract::restoreBatteryBits(address,mask,ours,original,
                [&](auto field){return ec.readStable(field);},
                [&](auto field,auto ownedMask,auto value){static_cast<void>(ec.updateBitsVerified(field,ownedMask,value));});
        };
        if (changedStore) restore("stored threshold", [&] {
            // This operation changed only low7, never restore firmware-owned
            // status bit7 merely because it was present in the old WMI byte.
            static_cast<void>(firmware.writeStored(storedBefore, 0x7F, raw, true));
        });
        if (changedLower) restore("lower threshold", [&] { restoreBits(0x7D0, 0x7F, lower, before.lower); });
        if (changedLimit) restore("upper threshold", [&] { restoreBits(limitAddress, 0x7F, raw, before.limit); });
        if (rollbackErrors.empty() && createdPin) {
            try {
                // Delete only our unchanged platform record after all firmware
                // fields were restored. Keep evidence on any uncertain rollback.
                std::ifstream in(pinPath); std::string board; int original{}, active{};
                if (!(in >> board >> original >> active) || board != "82/72/26" || original != 7 || active != 4)
                    throw std::runtime_error("Platform record changed externally; file retained");
                in.close();
                std::filesystem::remove(pinPath);
            } catch (const std::exception& error) { rollbackErrors = std::string("platform record: ") + error.what(); }
        }
        if (!rollbackErrors.empty()) {
            logMessage("battery", "LIMIT rollback unconfirmed: " + rollbackErrors);
            std::string cause = "Battery command failed";
            try { std::rethrow_exception(failure); }
            catch (const std::exception& error) { cause = error.what(); }
            catch (...) {}
            throw std::runtime_error(cause + "; откат батареи не подтверждён; фактическое состояние неизвестно: " + rollbackErrors);
        }
        logMessage("battery", "LIMIT failed: previous owned battery fields restored; fan writes=0");
        std::rethrow_exception(failure);
    }
}
std::string setBatteryAutomatic(EcAccess& ec, bool enabled) {
    return setBatteryEcPolicy(ec,enabled?BatteryEcPolicy::Platform7Percent:BatteryEcPolicy::Platform4Original);
}
std::string setBatteryEcPolicy(EcAccess& ec,BatteryEcPolicy choice) {
    IdentityHistoryScope historyOrigin("user","battery.ec_policy",true);
    if(batteryEcPolicy(static_cast<int>(choice)))recordIdentityPolicyRequest(batteryPolicyPlatform(choice),batteryPolicyRom(choice),"wujie.ec_policy");
    audit::Scope trace("battery.ec_policy","\"choice\":"+std::to_string(static_cast<int>(choice)));
    if(!batteryEcPolicy(static_cast<int>(choice)))throw std::invalid_argument("Unknown EC battery policy");
    if(choice==BatteryEcPolicy::Platform7Original){
        ec.ensureCapability(DeviceFeature::BatteryCharge,"uniwill-x5sp4nag");
        const EcAccess::Transaction transaction(ec);
        const auto guard=[&]{batteryContract::requireBatteryIdentity(ec.boardVerified(),[&](auto a){return ec.readStable(a);});};
        guard();const auto before=read(ec);const auto protection=ec.readStable(0x7A6);
        batteryEnforcement::Change policy([&](auto a){return ec.readStable(a);},[&](auto a,auto mask,auto value){
            guard();batteryContract::checkedBatteryWrite(a,mask,[&]{return ec.updateBitsVerified(a,mask,value);});
        },batteryEnforcement::ownerPath(portableSettingsPath("device-settings.ini").parent_path(),"wu-jie-15-xpro"),"wu-jie-15-xpro",choice);
        try{policy.apply();guard();const auto actual=read(ec);
            if(actual.platform!=7||actual.rom!=255||(actual.limit&0x7F)!=(before.limit&0x7F)||(actual.lower&0x7F)!=(before.lower&0x7F)||ec.readStable(0x7A6)!=protection)
                throw std::runtime_error("7/FF policy or preserved battery fields not confirmed");
            policy.commit();
        }catch(...){const auto failure=std::current_exception();guard();policy.rollback();std::rethrow_exception(failure);}
        DevicePreferences patch;patch[DeviceSetting::ChargeAuto]=2;std::string error;
        if(!rememberDevicePreferences(patch,error))throw std::runtime_error("Политика применена, но не сохранена: "+error);
        trace.finish("readback_confirmed","\"platform\":7,\"rom\":255");
        return "7/FF configured; upper/lower/protection preserved; charge enforcement unverified";
    }
    DevicePreferences saved; std::string error;
    if (!loadDevicePreferences(portableSettingsPath("device-settings.ini"), saved, error) && !error.empty())
        throw std::runtime_error(error);
    const int upper = ec.readStable(ec::kBatteryChargeControl) & ec::kBatteryChargeLimitMask;
    const int remembered = saved[DeviceSetting::ChargeLimit].value_or(upper ? upper : 100);
    const auto result = applyBatteryChargeLimit(ec, remembered, choice==BatteryEcPolicy::Platform7Percent,-1,choice);
    DevicePreferences patch;
    patch[DeviceSetting::ChargeLimit] = remembered;
    patch[DeviceSetting::ChargeAuto] = static_cast<int>(choice);
    if (!rememberDevicePreferences(patch, error))
        throw std::runtime_error("Настройка применена, но не сохранена: " + error);
    trace.finish("readback_confirmed");return result;
}

}
