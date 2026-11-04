#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "MagicControl/OperationMessages.h"
namespace mc::gui {
// Shared modal input/focus lifecycle for existing project dialogs.
void runModalDialog(HWND owner, HWND dialog, HWND initialFocus = nullptr);
// Restore only the owner disabled by our modal loop BEFORE destroying the
// dialog, preventing Windows from activating another application in between.
// Modeless project dialogs without that context are simply destroyed.
void closeModalDialog(HWND dialog);
// Information uses the shared page buffer, fonts and button theme.
void showInformationDialog(HWND owner, UINT dpi, const std::wstring& message);
// Same buffered dialog/controls; close/Escape returns-1, choices are zero-based.
int showChoiceDialog(HWND owner,UINT dpi,const std::wstring& message,const std::vector<std::wstring>& choices);
// Modeless progress view of the same message window. Only the UI thread touches
// HWNDs; the worker owns the shared transcript. Close during work requests stop.
void showOperationDialog(HWND owner,UINT dpi,const std::wstring& title,std::shared_ptr<OperationMessages>,std::function<void()> cancel);
}
