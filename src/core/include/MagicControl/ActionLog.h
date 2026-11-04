#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <chrono>
namespace mc::audit {
using Id=std::uint64_t;
// Application actions only. No input hook outside our windows, no hardware IO.
std::string quote(std::string_view);
std::string hex(std::span<const std::uint8_t>);
Id current() noexcept;
Id capture() noexcept;
void clearInput(Id) noexcept;
Id input(std::string_view source,std::string_view event,std::string_view fields={}) noexcept;
void event(std::string_view name,std::string_view fields={}) noexcept;
void related(Id,std::string_view name,std::string_view fields={}) noexcept;
void state(std::string_view source,std::string_view json,const void* observer=nullptr) noexcept;
// A pooled job has the same initially-empty audit TLS as a fresh thread.
// Explicit domain scopes bind the captured request ID inside this boundary.
class TaskContext {
    Id previous_{},input_{};
public:
    TaskContext()noexcept;
    ~TaskContext();
    TaskContext(const TaskContext&)=delete;
};
// Binds an already-captured action across worker/queue boundaries without
// mistaking dispatch completion for confirmed hardware execution.
class Scope {
    Id previous_{},id_{};std::string command_;std::chrono::steady_clock::time_point start_;
    bool enabled_{},finished_{};int exceptions_{};
public:
    Scope(std::string_view command,std::string_view fields={},Id parent=0,bool enabled=true) noexcept;
    ~Scope();
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    void finish(std::string_view outcome,std::string_view fields={}) noexcept;
    void fail(std::string_view reason) noexcept;
};
}
