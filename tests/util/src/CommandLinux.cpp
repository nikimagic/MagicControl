#include "Probe.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
namespace probe {
Json runLinuxCommand(const fs::path &executable, const std::vector<std::string> &args, int timeoutMs) {
    int descriptors[2]{};
    if (pipe2(descriptors, O_CLOEXEC) != 0)
        return {{"error", std::strerror(errno)}};
    std::string command = executable.string();
    std::vector<char *> argv;
    argv.push_back(command.data());
    for (const auto &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    const auto pid = fork();
    if (pid < 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        return {{"error", std::strerror(errno)}};
    }
    if (pid == 0) {
        dup2(descriptors[1], STDOUT_FILENO);
        dup2(descriptors[1], STDERR_FILENO);
        const int input = open("/dev/null", O_RDONLY);
        if (input >= 0) {
            dup2(input, STDIN_FILENO);
            close(input);
        }
        close(descriptors[0]);
        close(descriptors[1]);
        execv(command.c_str(), argv.data());
        _exit(127);
    }
    close(descriptors[1]);
    fcntl(descriptors[0], F_SETFL, fcntl(descriptors[0], F_GETFL) | O_NONBLOCK);
    std::string output;
    bool timeout{}, truncated{};
    int status{};
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    const auto drain = [&] {
        char bytes[4096];
        for (;;) {
            const auto count = read(descriptors[0], bytes, sizeof(bytes));
            if (count <= 0)
                break;
            if (output.size() + static_cast<std::size_t>(count) <= 1024 * 1024)
                output.append(bytes, static_cast<std::size_t>(count));
            else
                truncated = true;
        }
    };
    for (;;) {
        drain();
        const auto done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done < 0 && errno != EINTR)
            break;
        if (Clock::now() >= deadline) {
            timeout = true;
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            break;
        }
        pollfd watched{descriptors[0], POLLIN, 0};
        poll(&watched, 1, 10);
    }
    drain();
    close(descriptors[0]);
    Json arguments = Json::array();
    for (const auto &arg : args)
        arguments.push(arg);
    return {{"executable", executable.string()},
            {"arguments", arguments},
            {"exit_code", WIFEXITED(status) ? WEXITSTATUS(status) : -1},
            {"signal", WIFSIGNALED(status) ? WTERMSIG(status) : 0},
            {"timeout", timeout},
            {"truncated", truncated},
            {"stdout_stderr", output}};
}
} // namespace probe
