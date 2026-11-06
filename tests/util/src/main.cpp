#include "Probe.h"
#include <algorithm>
#include <ctime>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace probe {
static std::string stamp(bool filename) {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream out;
    out << std::put_time(&tm, filename ? "%Y%m%d_%H%M%S" : "%Y-%m-%dT%H:%M:%S") << (filename ? '_' : '.')
        << std::setfill('0') << std::setw(3) << ms << 'Z';
    return out.str();
}
std::string utcNow() {
    return stamp(false);
}
std::string fileStamp() {
    return stamp(true);
}
std::string hex(std::uint64_t value, int width) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << value;
    return out.str();
}
std::string bytesHex(const void *bytes, std::size_t size) {
    const auto *p = static_cast<const unsigned char *>(bytes);
    std::string result;
    result.reserve(size * 2);
    constexpr char d[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < size; ++i) {
        result += d[p[i] >> 4];
        result += d[p[i] & 15];
    }
    return result;
}
std::optional<std::string> readBytes(const fs::path &path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::string value;
    char buffer[4096];
    while (input && value.size() < limit) {
        input.read(buffer, std::min<std::size_t>(sizeof(buffer), limit - value.size()));
        value.append(buffer, static_cast<std::size_t>(input.gcount()));
    }
    return value;
}
std::optional<std::string> readText(const fs::path &path, std::size_t limit) {
    auto value = readBytes(path, limit);
    if (value)
        while (!value->empty() && (value->back() == '\n' || value->back() == '\r' || value->back() == '\0'))
            value->pop_back();
    return value;
}
Log::Log(const fs::path &path, std::string runId) : file_(path, std::ios::binary), runId_(std::move(runId)) {
    if (!file_)
        throw std::runtime_error("Cannot create dump log");
}
void Log::event(const std::string &kind, Json record) {
    record["event"] = kind;
    record["schema_version"] = 1;
    record["run_id"] = runId_;
    record["sample"] = sample_;
    record["utc"] = utcNow();
    record["monotonic_us"] =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_).count();
    file_ << record.dump() << '\n';
    if (!file_)
        throw std::runtime_error("Cannot write dump log");
    ++records_;
}
void Log::reading(const std::string &source, const std::string &id, const std::string &name, Json raw,
                  Json value, const std::string &unit, const std::string &comment, const std::string &status,
                  const std::string &error) {
    event("sensor", {{"source", source},
                     {"id", id},
                     {"name", name},
                     {"raw", std::move(raw)},
                     {"value", std::move(value)},
                     {"unit", unit},
                     {"comment", comment},
                     {"status", status},
                     {"error", error}});
}
void Log::unavailable(const std::string &source, const std::string &error) {
    event("source_unavailable", {{"source", source}, {"error", error}});
}
void Log::source(const std::string &name, const std::function<void()> &collect) {
    const auto start = Clock::now();
    const auto before = records_;
    event("source_start", {{"source", name}});
    flush(); // Preserve the active source even if a vendor call stalls or crashes.
    bool success = true;
    try {
        collect();
    } catch (const std::exception &e) {
        success = false;
        unavailable(name, e.what());
    } catch (...) {
        success = false;
        unavailable(name, "Unknown exception");
    }
    event("source_end",
          {{"source", name},
           {"completed", success},
           {"duration_ms", std::chrono::duration<double, std::milli>(Clock::now() - start).count()},
           {"records", records_ - before - 1}});
    flush();
}
void Log::flush() {
    file_.flush();
    if (!file_)
        throw std::runtime_error("Cannot flush dump log");
}
} // namespace probe
int main(int argc, char **argv) {
    using namespace probe;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() -> std::string {
                if (++i >= argc)
                    throw std::runtime_error("Missing argument: " + arg);
                return argv[i];
            };
            if (arg == "--help" || arg == "-h") {
                std::cout << "SensorProbe: read-only portable sensor collector\nDefault: 10 samples, starts "
                             "5 seconds apart; new technical JSON + dump JSONL per run.\n--count N "
                             "--interval SECONDS --output DIRECTORY --inventory-only\n--no-ec --no-pawnio "
                             "--no-amd --no-nvidia\n--ec-only (Windows: EC RAM, input idle time and power policy only)\n";
                return 0;
            } else if (arg == "--count")
                options.count = std::stoi(next());
            else if (arg == "--interval")
                options.interval = std::stod(next());
            else if (arg == "--output") {
                const auto path = next();
                options.output =
                    fs::path(std::u8string(reinterpret_cast<const char8_t *>(path.data()), path.size()));
            } else if (arg == "--inventory-only")
                options.inventoryOnly = true;
            else if (arg == "--no-ec")
                options.ec = false;
            else if (arg == "--no-pawnio")
                options.pawn = false;
            else if (arg == "--no-amd")
                options.amd = false;
            else if (arg == "--no-nvidia")
                options.nvidia = false;
            else if (arg == "--ec-only") {
#ifdef _WIN32
                options.ecOnly = true;
                options.ec = true;
                options.pawn = options.amd = options.nvidia = false;
#else
                throw std::runtime_error("--ec-only is currently supported on Windows only");
#endif
            }
            else
                throw std::runtime_error("Unknown argument: " + arg);
        }
        if (options.count < 1 || options.count > 10000 || !std::isfinite(options.interval) ||
            options.interval < 1 || options.interval > 86400)
            throw std::runtime_error("Count must be 1..10000; interval 1..86400 seconds");
        const auto binaryDir = executablePath().parent_path();
        const auto portableDir = (binaryDir.filename() == "windows" || binaryDir.filename() == "linux")
                                     ? binaryDir.parent_path()
                                     : binaryDir;
        const auto output = options.output.empty() ? portableDir / "logs" : options.output;
        fs::create_directories(output);
        std::string host = hostName();
        for (auto &c : host)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                  c == '_'))
                c = '_';
        if (host.empty())
            host = "unknown";
        const auto stamp = fileStamp();
        std::string runId = host + "_" +
#ifdef _WIN32
                            "windows_" +
#else
                            "linux_" +
#endif
                            stamp;
        fs::path directory;
        for (unsigned suffix = 0;; ++suffix) {
            directory = output / (runId + (suffix ? "_" + std::to_string(suffix) : ""));
            std::error_code error;
            if (fs::create_directory(directory, error)) {
                runId = directory.filename().string();
                break;
            }
            if (error)
                throw std::runtime_error("Cannot create unique output directory: " + error.message());
        }
        const auto technicalPath = directory / "technical.json", dumpPath = directory / "dump.jsonl";
        std::cout << "Output: " << directory.string() << "\nCollecting machine inventory...\n" << std::flush;
        const auto inventoryStarted = utcNow();
        const auto inventoryClock = Clock::now();
        auto platform = createPlatform(options, binaryDir);
        Json technical = platform->inventory();
        technical["schema_version"] = 1;
        technical["tool"] = "SensorProbe";
        technical["version"] = "1.0";
        technical["collector_build"] = std::string(__DATE__) + " " + __TIME__;
        technical["pointer_bits"] = sizeof(void *) * 8;
        technical["inventory_only"] = options.inventoryOnly;
        technical["run_id"] = runId;
        technical["utc"] = utcNow();
        technical["settings_writes"] = false;
        technical["count"] = options.count;
        technical["interval_seconds"] = options.interval;
        technical["source_options"] = {
            {"EC", options.ec}, {"PawnIO", options.pawn}, {"AMD", options.amd}, {"NVIDIA", options.nvidia}, {"EC_only",options.ecOnly}};
        technical["dump_file"] = dumpPath.filename().string();
        technical["clock_note"] = "UTC wall clock plus steady-clock offsets; sequential sources and EC bytes "
                                  "are not an atomic snapshot";
        technical["inventory_started_utc"] = inventoryStarted;
        technical["inventory_finished_utc"] = utcNow();
        technical["inventory_duration_ms"] =
            std::chrono::duration<double, std::milli>(Clock::now() - inventoryClock).count();
        {
            std::ofstream file(technicalPath, std::ios::binary);
            file << technical.dump() << '\n';
            if (!file)
                throw std::runtime_error("Cannot write technical JSON");
        }
        Log log(dumpPath, runId);
        log.event("run_start", {{"count", options.count},
                                {"interval_seconds", options.interval},
                                {"technical_file", technicalPath.filename().string()},
                                {"settings_writes", false}});
        const auto start = Clock::now();
        if (!options.inventoryOnly)
            for (int sample = 1; sample <= options.count; ++sample) {
                const auto scheduled =
                    start + std::chrono::duration_cast<Clock::duration>(
                                std::chrono::duration<double>((sample - 1) * options.interval));
                std::this_thread::sleep_until(scheduled);
                log.sample(sample);
                const auto begin = Clock::now();
                const double late = std::chrono::duration<double, std::milli>(begin - scheduled).count();
                std::cout << "[" << sample << '/' << options.count << "] " << utcNow() << "\n" << std::flush;
                log.event("sample_start", {{"scheduled_offset_seconds", (sample - 1) * options.interval},
                                           {"start_lateness_ms", late}});
                platform->sample(log);
                const auto duration = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
                log.event("sample_end", {{"duration_ms", duration},
                                         {"interval_overrun", duration > options.interval * 1000}});
                log.flush();
                if (duration > options.interval * 1000)
                    std::cout << "  Source overrun: " << duration << " ms; no overlapping sampling.\n";
            }
        log.event("run_end", {{"completed", true},
                              {"samples", options.inventoryOnly ? 0 : options.count},
                              {"records", log.records() + 1}});
        log.flush();
        std::cout << "Completed.\n" << technicalPath.string() << "\n" << dumpPath.string() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "SensorProbe error: " << e.what() << '\n';
        return 1;
    }
}
