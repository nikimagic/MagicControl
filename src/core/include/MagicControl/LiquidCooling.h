#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace mc {
enum class PumpVoltage:int {V7=7,V8=8,V11=11,V12=12};
struct LiquidPumpCommand {bool enabled{};int duty{60};PumpVoltage voltage{PumpVoltage::V8};friend bool operator==(const LiquidPumpCommand&,const LiquidPumpCommand&)=default;};
struct LiquidFanCommand {bool enabled{};int duty{50};friend bool operator==(const LiquidFanCommand&,const LiquidFanCommand&)=default;};
struct LiquidCoolingDevice {std::string id,name;std::optional<bool> paired;friend bool operator==(const LiquidCoolingDevice&,const LiquidCoolingDevice&)=default;};
struct LiquidCoolingLink {
    bool connected{},transportConnected{},notificationsReady{};
    std::string deviceId,deviceName,firmware,firmwareReply,lastNotificationHex,status;
    std::optional<int> meterState;std::uint64_t meterAtMs{};
};
struct LiquidCoolingTargets {LiquidPumpCommand pump;LiquidFanCommand fan;std::uint8_t pumpRaw{},fanRaw{};};
struct LiquidCoolingState {
    LiquidCoolingLink link;
    bool automatic{},nativePolicyOwned{},suspended{},commandUncertain{};
    std::optional<LiquidPumpCommand> acceptedPump;
    std::optional<LiquidFanCommand> acceptedFan;
    std::string status;
    // Command acceptance and meter state are not RPM/duty readback.
};
// OS adapter owns BLE/GATT and exact-board native policy; core owns protocol,
// sequence, automatic scheduling, uncertainty and lifecycle. No GUI dependency.
class ILiquidCoolingBackend {
public:
    virtual ~ILiquidCoolingBackend()=default;
    virtual std::vector<LiquidCoolingDevice> discover(std::stop_token)=0;
    virtual void connect(const std::string& exactDeviceId,std::stop_token)=0;
    virtual LiquidCoolingLink link()=0;
    virtual void write(std::span<const std::uint8_t>,std::stop_token)=0;
    virtual void close() noexcept=0; // Release transport only, never asserts pump off.
    virtual LiquidCoolingTargets automaticTargets()=0;
    virtual void prepareAutomatic()=0; // Read-only admission before any priming commands.
    virtual void acquireNativePolicy()=0;
    virtual void releaseNativePolicy()=0;
    virtual bool nativePolicyOwned()const noexcept=0;
};
class LiquidCoolingController {
public:
    explicit LiquidCoolingController(std::unique_ptr<ILiquidCoolingBackend>);
    ~LiquidCoolingController();
    std::vector<LiquidCoolingDevice> discover(std::stop_token={});
    void connect(const std::string& deviceId,std::stop_token={});
    LiquidCoolingState state(); // Cached BLE notifications; no implicit connect/apply.
    std::optional<LiquidCoolingState> tryState(); // Non-blocking UI/dump snapshot while a command is pending.
    void setPump(const LiquidPumpCommand&,std::stop_token={});
    void setFan(const LiquidFanCommand&,std::stop_token={});
    void requestMeter(std::stop_token={});
    void enableAutomatic(std::stop_token={}); // Explicit start; requires fresh normal flow before native LC policy.
    void disableAutomatic(); // Release owned policy, leave last external outputs.
    void stopCooling(std::stop_token={});
    void disconnect(bool stopFirst,std::stop_token={});
    void sleep(std::stop_token={}); // Explicit native sleep command, then close; no automatic wake.
    void suspend(); // Cancel automatic control, release own LC policy, no restart on resume.
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Creates an inert controller; no discovery/connect/write/restore in factory.
// nullptr on unsupported actual boards/OS or --no-ec. Caller owns its lifetime.
std::shared_ptr<LiquidCoolingController> createLiquidCoolingController();
// Operates only on an already-created session, never constructs a backend.
void suspendLiquidCooling();
std::string liquidCoolingDiagnosticsJson();
}
