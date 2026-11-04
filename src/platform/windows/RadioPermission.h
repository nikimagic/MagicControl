#pragma once
#include <atomic>
#include <functional>
#include <stdexcept>

namespace mc::windows {
// Begin runs on the caller's UI thread. Finish runs on its background worker.
// Cache successful consent only; a policy denial revokes the cached permission.
class RadioPermission {
public:
    using Completion = std::function<void()>;
    Completion request(const std::function<Completion()>& begin) {
        if (allowed_.load()) return [] {};
        auto finish = begin();
        return [this, finish = std::move(finish)] {
            finish();
            allowed_.store(true);
        };
    }
    void ensureAllowed() const {
        if (!allowed_.load()) throw std::runtime_error("Сначала разрешите приложению управлять Wi-Fi/Bluetooth.");
    }
    void revoke() { allowed_.store(false); }
private:
    std::atomic<bool> allowed_{};
};
}
