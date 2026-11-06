#include "OemEventMonitor.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/TelemetryGate.h"
#include "WmiEventTimeWindows.h"

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <chrono>
#include <format>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mc::windows {
namespace {
using Microsoft::WRL::ComPtr;
struct Apartment { HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)}; ~Apartment(){if(SUCCEEDED(result))CoUninitialize();} };
std::string utf8(const wchar_t* value, ULONG count) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(count), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(count), result.data(), size, nullptr, nullptr);
    return result;
}
}

OemEventMonitor::OemEventMonitor(Callback callback, std::function<void(std::string)> log,std::function<void(std::string)> subscribed)
    : callback_(std::move(callback)), log_(std::move(log)), subscribed_(std::move(subscribed)), worker_([this] { run(); }) {}

OemEventMonitor::~OemEventMonitor() {
    stop_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
}

void OemEventMonitor::run() {
    std::optional<TelemetryGate::Observation> initialization;
    while(!stop_.load(std::memory_order_acquire)&&!initialization){
        initialization=telemetryGate().tryObservation();
        if(!initialization)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if(!initialization)return;
    Apartment apartment;
    if (FAILED(apartment.result)) { if (log_) log_("OEM events: COM initialization failed"); return; }
    static std::once_flag security;
    std::call_once(security, [] { CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr); });
    ComPtr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)))) return;
    ComPtr<IWbemServices> service;
    BSTR name = SysAllocString(L"ROOT\\WMI");
    HRESULT connected = locator->ConnectServer(name, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &service);
    SysFreeString(name);
    if (FAILED(connected)) { if (log_) log_("OEM events: ROOT\\WMI unavailable"); return; }
    CoSetProxyBlanket(service.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    struct Subscription {
        std::string name;std::wstring query;ComPtr<IEnumWbemClassObject> events;
        FirmwareEventQueueFilter filter;bool retryLogged{};std::chrono::steady_clock::time_point retryAt{};
    };
    std::vector<Subscription> subscriptions;
    const auto profileId=deviceProfile(readBoardDescriptor()).id;
    const bool uniwill=profileId=="wu-jie-15-xpro",gm6=gmRamFanProfile(profileId);
    for (const wchar_t* provider : {L"AcpiTest_EventULong", L"AcpiTest_EventString", L"AcpiTest_EventPackage", L"HID_EVENT20", L"WmiMonitorBrightnessEvent"}) {
        if(!uniwill&&!(gm6&&wcscmp(provider,L"AcpiTest_EventULong")==0)&&wcscmp(provider,L"WmiMonitorBrightnessEvent")!=0)continue;
        std::wstring query = L"SELECT * FROM " + std::wstring(provider);
        BSTR language = SysAllocString(L"WQL"), statement = SysAllocString(query.c_str());
        ComPtr<IEnumWbemClassObject> events;
        const HRESULT result = service->ExecNotificationQuery(language, statement,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &events);
        SysFreeString(language); SysFreeString(statement);
        const auto providerName = utf8(provider, static_cast<ULONG>(wcslen(provider)));
        if (SUCCEEDED(result)) {
            subscriptions.push_back({providerName, std::move(query), std::move(events)});
            if (log_) log_("OEM events: subscribed to "+providerName);
            if(subscribed_)subscribed_(providerName);
        } else if (log_) {
            // HID_EVENT20 is an optional schema on Wujie, not a required
            // transport. An absent class must not look like failed hotkeys.
            // Keep real errors (including required AcpiTest providers) visible.
            if(wcscmp(provider,L"HID_EVENT20")==0&&result==static_cast<HRESULT>(WBEM_E_INVALID_CLASS))
                log_(std::format("OEM events: provider={} not exposed (optional WMI class); HRESULT=0x{:08X}",providerName,static_cast<unsigned>(result)));
            else log_(std::format("OEM events: provider={} subscription failed HRESULT=0x{:08X}",providerName,static_cast<unsigned>(result)));
        }
    }
    if (log_) log_(std::format("OEM events: {} WMI providers connected", subscriptions.size()));
    auto observedGeneration=initialization->generation();
    initialization.reset();
    while (!stop_.load(std::memory_order_acquire)) {
        auto observation=telemetryGate().tryObservation();
        if(!observation){std::this_thread::sleep_for(std::chrono::milliseconds(50));continue;}
        if(observation->generation()!=observedGeneration){
            observedGeneration=observation->generation();
            for(auto& subscription:subscriptions)subscription.filter.captureBoundary();
        }
        for (auto& subscription : subscriptions) {
            if(!subscription.events){
                if(std::chrono::steady_clock::now()<subscription.retryAt)continue;
                BSTR language=SysAllocString(L"WQL"),statement=SysAllocString(subscription.query.c_str());
                ComPtr<IEnumWbemClassObject> replacement;
                const auto result=service->ExecNotificationQuery(language,statement,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&replacement);
                SysFreeString(language);SysFreeString(statement);
                if(FAILED(result)){
                    if(!subscription.retryLogged&&log_)log_(std::format("OEM events: provider={} resubscribe failed HRESULT=0x{:08X}",subscription.name,static_cast<unsigned>(result)));
                    subscription.retryLogged=true;subscription.retryAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);continue;
                }
                // A new query has no old queued events. Do not call the
                // startup subscription callback again: it may write APP.
                subscription.events=std::move(replacement);subscription.filter.renewed();subscription.retryLogged=false;
                if(log_)log_("OEM events: provider="+subscription.name+" resubscribed after an unconfirmed event timestamp");
            }
            ComPtr<IWbemClassObject> item; ULONG returned{};
            const auto next=subscription.events->Next(0, 1, &item, &returned);
            if(next!=WBEM_S_NO_ERROR||returned==0){
                const bool empty=returned==0&&(next==WBEM_S_NO_ERROR||next==WBEM_S_FALSE||next==WBEM_S_TIMEDOUT);
                if(empty&&subscription.filter.renewAfterEmpty()){
                    if(log_)log_("OEM events: provider="+subscription.name+" queue empty without confirmed event timestamps; renewing subscription");
                    subscription.events.Reset();subscription.retryAt={};
                }
                continue;
            }
            VARIANT value{};
            const bool brightness = subscription.name == "WmiMonitorBrightnessEvent";
            if (SUCCEEDED(item->Get(brightness ? L"Brightness" : L"ULong", 0, &value, nullptr, nullptr))) {
                std::uint64_t code{};
                if (value.vt == VT_UI8) code = value.ullVal;
                else if (value.vt == VT_UI4) code = value.ulVal;
                else if (value.vt == VT_I8) code = static_cast<std::uint64_t>(value.llVal);
                else if (value.vt == VT_I4) code = static_cast<std::uint64_t>(value.lVal);
                else if (value.vt == VT_UI1) code = value.bVal;
                else if (value.vt == VT_UI2) code = value.uiVal;
                else if (value.vt == VT_I2 && value.iVal >= 0) code = static_cast<std::uint64_t>(value.iVal);
                if (code != 0 || brightness) {
                    if(subscription.filter.active()){
                        VARIANT created{};CIMTYPE type{};std::optional<std::uint64_t> timestamp;
                        if(SUCCEEDED(item->Get(L"TIME_CREATED",0,&created,&type,nullptr)))timestamp=wmiEventFiletime(created,type);
                        VariantClear(&created);
                        const auto age=classifyFirmwareEvent(timestamp,observation->firmwareEventBoundary(),FirmwareWallClock::now(),FirmwareSteadyClock::now());
                        // Every OEM event can affect an observer or enqueue a
                        // later action; an unknown old-queue event is never
                        // replayed. A fresh subscription resumes all codes.
                        const auto disposition=subscription.filter.decide(age,true);
                        if(disposition==FirmwareEventDisposition::DiscardStale){VariantClear(&value);continue;}
                        if(disposition==FirmwareEventDisposition::ResubscribeUnknown){
                            if(log_)log_(std::format("OEM events: provider={} code={} suppressed: event time/clock not confirmed; re-subscribing",subscription.name,code));
                            subscription.events.Reset();subscription.retryAt={};VariantClear(&value);continue;
                        }
                    }
                    if (log_) log_(std::format("OEM event: provider={} code={} (0x{:X})",subscription.name,code,code));
                    if (callback_) callback_(subscription.name, code);
                }
            }
            VariantClear(&value);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
}
