#pragma once
#include "MagicControl/HardwareInventory.h"
#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <stdexcept>
namespace mc::windows::wmi {
using Microsoft::WRL::ComPtr;
inline std::string utf8(const std::wstring& value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(0, size)), '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
inline std::string failure(HRESULT hr) { return std::format("Windows: 0x{:08X}", static_cast<unsigned long>(hr)); }
inline void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error(failure(hr)); }
struct Bstr {
    BSTR value{};
    explicit Bstr(const wchar_t* text) : value(SysAllocString(text)) { if (!value) throw std::bad_alloc(); }
    ~Bstr() { SysFreeString(value); }
};
struct Variant { VARIANT value{}; ~Variant() { VariantClear(&value); } };
struct Record {
    std::map<std::wstring, std::string> fields;
    std::map<std::wstring, double> numericFields;
    std::string get(const wchar_t* name) const {
        const auto it = fields.find(name);
        return it == fields.end() || it->second.empty() ? kInventoryUnknown : it->second;
    }
    std::optional<double> number(const wchar_t* name) const {
        if(const auto it=numericFields.find(name);it!=numericFields.end())return it->second;
        try { const auto value = get(name); size_t n{}; double result = std::stod(value, &n);
              if (n == value.size() && std::isfinite(result)) return result; } catch (...) {}
        return std::nullopt;
    }
    int integer(const wchar_t* name) const { const auto n = number(name); return n ? static_cast<int>(*n) : -1; }
    std::string yes(const wchar_t* name) const {
        const auto value = get(name);
        if (value == "True" || value == "true" || value == "-1" || value == "1") return "Да";
        if (value == "False" || value == "false" || value == "0") return "Нет";
        return kInventoryUnknown;
    }
};
// Diagnostic callers retain partial results and an explicit termination reason.
// Inventory callers keep exception semantics instead of accepting silent truncation.
inline constexpr std::size_t kMaximumQueryRecords = 65'536;
struct QueryCompletion {
    bool complete{true};
    std::size_t rows{};
    std::string status;
};
struct ComApartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ComApartment() { check(result); }
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
std::vector<Record> isolatedQuery(const wchar_t* sql,std::initializer_list<const wchar_t*> fields,
    const wchar_t* space,std::chrono::seconds timeout,std::stop_token stop,QueryCompletion& completion);
class Wmi {
    ComApartment apartment_;
    ComPtr<IWbemLocator> locator_;
    std::map<std::wstring, ComPtr<IWbemServices>> services_;
    std::stop_token cancellation_;bool direct_{};
public:
    explicit Wmi(std::stop_token cancellation,bool direct=false) : cancellation_(cancellation),direct_(direct) {
        if(!direct_)return; // Parent never connects to a potentially stuck provider.
        static std::once_flag security;
        std::call_once(security, [] { CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr); });
        check(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator_)));
    }
    ~Wmi() = default;
    std::vector<Record> query(const wchar_t* sql, std::initializer_list<const wchar_t*> fields,
                              const wchar_t* space = L"ROOT\\CIMV2", std::chrono::seconds timeout=std::chrono::seconds(8),
                              QueryCompletion* completion=nullptr,
                              const std::function<void(const Record&)>& onRecord={}) {
        if(!direct_){
            QueryCompletion finished;
            auto records=isolatedQuery(sql,fields,space,timeout,cancellation_,finished);
            if(completion)*completion=finished;else if(!finished.complete)throw std::runtime_error(finished.status);
            if(onRecord)for(const auto& record:records)onRecord(record);
            return records;
        }
        if(completion)*completion={};
        if (cancellation_.stop_requested()) throw std::runtime_error("Сбор отменён");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        auto& service = services_[space];
        if (!service) {
            Bstr ns(space); check(locator_->ConnectServer(ns.value, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &service));
            check(CoSetProxyBlanket(service.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE));
        }
        Bstr language(L"WQL"), command(sql); ComPtr<IEnumWbemClassObject> results;
        check(service->ExecQuery(language.value, command.value,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &results));
        std::vector<Record> output;
        const auto incomplete=[&](std::string reason){
            if(!completion)throw std::runtime_error(reason);
            completion->complete=false;completion->status=std::move(reason);
        };
        for (;;) {
            if (cancellation_.stop_requested()) {incomplete("Сбор отменён");break;}
            if (std::chrono::steady_clock::now() > deadline) {incomplete("Превышено время ответа WMI");break;}
            ComPtr<IWbemClassObject> object; ULONG returned{};
            const HRESULT hr = results->Next(200, 1, &object, &returned);
            if(FAILED(hr)){incomplete(failure(hr));break;}
            if (!returned) { if (hr == WBEM_S_TIMEDOUT) continue; break; }
            // Probe the next row first: exactly 65,536 rows is a complete query.
            if(output.size() == kMaximumQueryRecords){
                incomplete("WMI record limit reached: "+std::to_string(kMaximumQueryRecords));break;
            }
            Record record;
            for (const auto* name : fields) {
                Variant original, converted, numeric;
                if (SUCCEEDED(object->Get(name, 0, &original.value, nullptr, nullptr)) &&
                    original.value.vt != VT_NULL && original.value.vt != VT_EMPTY) {
                    // Preserve a typed float before localized BSTR formatting.
                    // Russian decimal commas must not turn 36.5C into null.
                    if(SUCCEEDED(VariantChangeTypeEx(&numeric.value,&original.value,LOCALE_INVARIANT,0,VT_R8))&&std::isfinite(numeric.value.dblVal))
                        record.numericFields.emplace(name,numeric.value.dblVal);
                    if(FAILED(VariantChangeType(&converted.value, &original.value, 0, VT_BSTR)))continue;
                    auto value = utf8(std::wstring(converted.value.bstrVal, SysStringLen(converted.value.bstrVal)));
                    const auto end = value.find_last_not_of(" \t\r\n\0", std::string::npos, 5);
                    value = end == std::string::npos ? "" : value.substr(0, end + 1);
                    record.fields.emplace(name, std::move(value));
                }
            }
            if(onRecord)onRecord(record);
            output.push_back(std::move(record));

        }
        if(completion)completion->rows=output.size();
        return output;
    }
};
}
