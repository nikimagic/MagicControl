#include "WinHelpers.h"
#include <algorithm>
namespace probe {
Json runWindowsCommand(const fs::path &executable, const std::wstring &args, DWORD timeout) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    WinHandle reader, writer, input;
    HANDLE read{}, write{};
    if (!CreatePipe(&read, &write, &security, 65536))
        return {{"error", winError()}};
    reader.value = read;
    writer.value = write;
    SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0);
    input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                              OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = input.value;
    startup.hStdOutput = startup.hStdError = writer.value;
    PROCESS_INFORMATION info{};
    auto line = L"\"" + executable.wstring() + L"\" " + args;
    if (!CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        executable.parent_path().c_str(), &startup, &info))
        return {{"error", winError()}};
    WinHandle process(info.hProcess), thread(info.hThread);
    CloseHandle(std::exchange(writer.value, INVALID_HANDLE_VALUE));
    std::string output;
    bool timedOut{}, truncated{};
    const auto drain = [&] {
        DWORD available{};
        while (PeekNamedPipe(reader.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            char buffer[4096];
            DWORD count{};
            if (!ReadFile(reader.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &count,
                          nullptr) ||
                !count)
                break;
            if (output.size() + count <= 1024 * 1024)
                output.append(buffer, count);
            else
                truncated = true;
        }
    };
    const auto deadline = GetTickCount64() + timeout;
    for (;;) {
        drain();
        if (WaitForSingleObject(process.value, 5) == WAIT_OBJECT_0)
            break;
        if (GetTickCount64() >= deadline) {
            timedOut = true;
            TerminateProcess(process.value, ERROR_TIMEOUT);
            WaitForSingleObject(process.value, 250);
            break;
        }
    }
    drain();
    DWORD exit{};
    GetExitCodeProcess(process.value, &exit);
    return {{"executable", utf8(executable.c_str())},
            {"arguments", utf8(args.c_str())},
            {"exit_code", exit},
            {"timeout", timedOut},
            {"truncated", truncated},
            {"stdout_stderr", output}};
}
} // namespace probe
