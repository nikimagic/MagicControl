#pragma once

#include "MagicControl/FirmwareEventTime.h"
#include <windows.h>
#include <wbemcli.h>
#include <oleauto.h>

namespace mc::windows {
// TIME_CREATED is CIM_UINT64. WMI normally represents uint64 as a decimal
// VT_BSTR, but accept actual integer variants if a provider supplies them.
inline std::optional<std::uint64_t> wmiEventFiletime(const VARIANT& value,CIMTYPE type)noexcept{
    if(type!=CIM_UINT64)return {};
    if(value.vt==VT_BSTR){if(!value.bstrVal)return {};return parseWmiUint64(std::wstring_view(value.bstrVal,SysStringLen(value.bstrVal)));}
    if(value.vt==VT_UI8)return value.ullVal;
    if(value.vt==VT_I8&&value.llVal>=0)return static_cast<std::uint64_t>(value.llVal);
    return {};
}
}
