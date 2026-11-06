// Actual Windows WMI VARIANT decoding with synthetic values only.
// No WMI service/subscription or hardware is opened.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "WmiEventTimeWindows.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
int main(){try{
    VARIANT value;VariantInit(&value);value.vt=VT_BSTR;value.bstrVal=SysAllocString(L"18446744073709551615");
    check(value.bstrVal&&mc::windows::wmiEventFiletime(value,CIM_UINT64)==std::numeric_limits<std::uint64_t>::max(),"valid VT_BSTR uint64 missing");
    check(!mc::windows::wmiEventFiletime(value,CIM_SINT64),"non-uint64 CIM type must be rejected");VariantClear(&value);
    value.vt=VT_BSTR;value.bstrVal=SysAllocString(L"18446744073709551616");
    check(value.bstrVal&&!mc::windows::wmiEventFiletime(value,CIM_UINT64),"overflow VT_BSTR must be rejected");VariantClear(&value);
    value.vt=VT_BSTR;value.bstrVal=nullptr;check(!mc::windows::wmiEventFiletime(value,CIM_UINT64),"null BSTR must be rejected");VariantClear(&value);
    value.vt=VT_UI8;value.ullVal=123;check(mc::windows::wmiEventFiletime(value,CIM_UINT64)==123,"VT_UI8 uint64 rejected");VariantClear(&value);
    value.vt=VT_I8;value.llVal=123;check(mc::windows::wmiEventFiletime(value,CIM_UINT64)==123,"non-negative VT_I8 rejected");
    value.llVal=-1;check(!mc::windows::wmiEventFiletime(value,CIM_UINT64),"negative VT_I8 must be rejected");VariantClear(&value);
    value.vt=VT_NULL;check(!mc::windows::wmiEventFiletime(value,CIM_UINT64),"VT_NULL must be rejected");VariantClear(&value);
    std::cout<<"PASS WMI uint64 VARIANT: BSTR, native 64-bit, overflow, wrong type and null\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL WMI timestamp VARIANT: "<<error.what()<<'\n';return 1;}}
