#pragma once
#include "WmiQueryWindows.h"
namespace mc::windows::wmi {
std::optional<int> runDiagnosticHelper(const std::vector<std::string>& args);
std::vector<Record> isolatedQuery(const wchar_t* sql,std::initializer_list<const wchar_t*> fields,
    const wchar_t* space,std::chrono::seconds timeout,std::stop_token stop,QueryCompletion& completion);
}
