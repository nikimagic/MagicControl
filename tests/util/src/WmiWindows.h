#pragma once
#include "WinHelpers.h"
#include <wbemidl.h>
namespace probe {
template <class T> struct Com {
    T *value{};
    ~Com() {
        if (value)
            value->Release();
    }
    T **out() {
        return &value;
    }
};
inline Json variantJson(const VARIANT &value) {
    if (value.vt & VT_ARRAY) {
        Json result = Json::array();
        if (!value.parray || SafeArrayGetDim(value.parray) != 1)
            return result;
        LONG first{}, last{};
        SafeArrayGetLBound(value.parray, 1, &first);
        SafeArrayGetUBound(value.parray, 1, &last);
        for (LONG i = first; i <= last && i - first < 16384; ++i) {
            VARIANT item;
            VariantInit(&item);
            item.vt = value.vt & VT_TYPEMASK;
            void *pointer =
                item.vt == VT_VARIANT ? static_cast<void *>(&item) : static_cast<void *>(&item.llVal);
            if (SUCCEEDED(SafeArrayGetElement(value.parray, &i, pointer)))
                result.push(variantJson(item));
            VariantClear(&item);
        }
        return result;
    }
    switch (value.vt) {
    case VT_EMPTY:
    case VT_NULL:
        return {};
    case VT_BSTR:
        return value.bstrVal ? Json(utf8(value.bstrVal, SysStringLen(value.bstrVal))) : Json{};
    case VT_BOOL:
        return value.boolVal != VARIANT_FALSE;
    case VT_UI1:
        return value.bVal;
    case VT_I1:
        return value.cVal;
    case VT_I2:
        return value.iVal;
    case VT_UI2:
        return value.uiVal;
    case VT_I4:
    case VT_INT:
        return value.lVal;
    case VT_UI4:
    case VT_UINT:
        return value.ulVal;
    case VT_I8:
        return value.llVal;
    case VT_UI8:
        return value.ullVal <= static_cast<unsigned long long>(INT64_MAX)
                   ? Json(value.ullVal)
                   : Json(std::to_string(value.ullVal));
    case VT_R4:
        return static_cast<double>(value.fltVal);
    case VT_R8:
        return value.dblVal;
    default:
        return Json{{"variant_type", value.vt}};
    }
}
struct WmiResult {
    std::vector<Json> rows;
    std::string error;
    bool partial{};
    Json json() const {
        Json array = Json::array();
        for (const auto &row : rows)
            array.push(row);
        return {{"rows", array}, {"error", error}, {"partial", partial}};
    }
};
class Wmi {
    Com<IWbemLocator> locator;
    std::map<std::wstring, IWbemServices *> services;
    std::map<std::wstring, std::string> failed;

  public:
    Wmi() {
        const auto rc =
            CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.out()));
        if (FAILED(rc))
            throw std::runtime_error("WMI locator HRESULT=0x" + hex(static_cast<unsigned>(rc), 8));
    }
    ~Wmi() {
        for (const auto &item : services)
            item.second->Release();
    }
    WmiResult query(const std::wstring &space, const std::wstring &sql, int budgetMs = 1500) {
        WmiResult result;
        if (failed.count(space)) {
            result.error = failed[space];
            return result;
        }
        IWbemServices *server{};
        if (auto it = services.find(space); it != services.end())
            server = it->second;
        else {
            BSTR name = SysAllocString(space.c_str());
            const auto rc =
                locator.value->ConnectServer(name, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &server);
            SysFreeString(name);
            if (FAILED(rc) || !server) {
                result.error = "Namespace unavailable HRESULT=0x" + hex(static_cast<unsigned>(rc), 8);
                failed[space] = result.error;
                return result;
            }
            CoSetProxyBlanket(server, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            services[space] = server;
        }
        Com<IEnumWbemClassObject> enumeration;
        BSTR language = SysAllocString(L"WQL"), request = SysAllocString(sql.c_str());
        const auto rc =
            server->ExecQuery(language, request, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                              nullptr, enumeration.out());
        SysFreeString(language);
        SysFreeString(request);
        if (FAILED(rc)) {
            result.error = "ExecQuery HRESULT=0x" + hex(static_cast<unsigned>(rc), 8);
            return result;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(budgetMs);
        while (result.rows.size() < 4096 && Clock::now() < deadline) {
            Com<IWbemClassObject> object;
            ULONG count{};
            const auto next = enumeration.value->Next(250, 1, object.out(), &count);
            if (next == WBEM_S_TIMEDOUT) {
                continue;
            }
            if (FAILED(next)) {
                result.error = "Next HRESULT=0x" + hex(static_cast<unsigned>(next), 8);
                break;
            }
            if (!count)
                break;
            Json row;
            const bool definition = sql.find(L"meta_class") != std::wstring::npos;
            if (!definition)
                object.value->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY);
            for (;;) {
                if (definition)
                    break;
                BSTR name{};
                VARIANT value;
                VariantInit(&value);
                CIMTYPE type{};
                LONG flavor{};
                const auto prop = object.value->Next(0, &name, &value, &type, &flavor);
                if (prop != WBEM_S_NO_ERROR) {
                    VariantClear(&value);
                    if (name)
                        SysFreeString(name);
                    break;
                }
                row[utf8(name)] = variantJson(value);
                VariantClear(&value);
                SysFreeString(name);
            }
            if (!definition)
                object.value->EndEnumeration();
            for (const auto *key : {L"__CLASS", L"__RELPATH"}) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(object.value->Get(key, 0, &value, nullptr, nullptr)))
                    row[utf8(key)] = variantJson(value);
                VariantClear(&value);
            }
            result.rows.push_back(std::move(row));
        }
        if (Clock::now() >= deadline || result.rows.size() >= 4096)
            result.partial = true;
        return result;
    }
};
inline std::string jsonField(const Json &row, const std::string &key) {
    const auto *value = row.find(key);
    return value ? value->text() : "";
}
} // namespace probe
