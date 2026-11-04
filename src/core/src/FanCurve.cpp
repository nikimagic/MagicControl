#include "MagicControl/FanCurve.h"
#include "MagicControl/FanControlLimits.h"
#include <exception>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"

namespace mc {

bool validateFanCurve(const std::vector<FanCurvePoint>& points, std::string& error) {
    if (points.empty() || points.size() > ec::kTablePointCount) {
        error = "Кривая должна содержать от 1 до 16 точек.";
        return false;
    }
    int previous = -1;
    for (const FanCurvePoint& point : points) {
        // Legacy values3..19 remain decodable. New user fields are20..100;
        // device protection is encoded separately, never an input-field cap.
        if (point.temperature < 3 || point.temperature > fanInput::temperatureMaximum || !fanInput::validPercent(point.duty)) {
            error = "Кривая протокола: температура 3–100 °C, PWM — 5–100%.";
            return false;
        }
        if (point.temperature <= previous) {
            error = "Температуры точек должны возрастать.";
            return false;
        }
        previous = point.temperature;
    }
    return true;
}

std::array<std::uint8_t, 48> buildAutonomousFanTable(const std::vector<FanCurvePoint>& requested) {
    std::string error;
    if (!validateFanCurve(requested, error)) throw std::invalid_argument(error);
    const auto points = fanCurveWithMaximumStage(requested,85);
    if (points.size() > 14) throw std::invalid_argument("EC_manual: максимум 14 точек вместе с защитной 85 °C / 100%.");
    std::array<std::uint8_t, 48> table{};
    std::fill(table.begin(), table.begin() + 32, std::uint8_t{255});
    std::fill(table.begin() + 32, table.end(), std::uint8_t{200});
    table[0] = table[16] = table[32] = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        table[i + 1] = static_cast<std::uint8_t>(points[i].temperature);
        table[16 + i + 1] = static_cast<std::uint8_t>(points[i].temperature - 3);
        table[32 + i + 1] = static_cast<std::uint8_t>(points[i].duty * 2);
    }
    return table;
}

namespace {
constexpr std::array<std::pair<std::uint16_t, std::uint8_t>, 5> kControls{{
    {ec::kRamTableCommit, ec::kRamTableCommitBit},
    {ec::kAppPresence, ec::kAppPresenceBit},
    {ec::kControl727, ec::kControl727FanBit},
    {ec::kCustomMode, ec::kCustomModeBit},
    {ec::kFanControlRespective, ec::kSplitModeBit},
}};

struct Snapshot {
    std::vector<std::uint8_t> table;
    std::array<std::uint8_t, kControls.size()> controls{};
    explicit Snapshot(EcAccess& access)
        : table(access.readBlock(ec::kCpuTableBase, ec::kTableEnd - ec::kCpuTableBase)) {
        for (std::size_t i = 0; i < kControls.size(); ++i)
            controls[i] = access.readStable(kControls[i].first);
    }
    void restore(EcAccess& access) const {
        static_cast<void>(access.updateBitsVerified(ec::kRamTableCommit, ec::kRamTableCommitBit, 0));
        for (std::size_t i = 0; i < table.size(); ++i) {
            const auto address = static_cast<std::uint16_t>(ec::kCpuTableBase + i);
            if (access.readStable(address) != table[i]) access.writeVerified(address, table[i]);
        }
        // Commit is restored last, after both tables and all mode flags.
        for (std::size_t i = 1; i < kControls.size(); ++i)
            static_cast<void>(access.updateBitsVerified(kControls[i].first, kControls[i].second, controls[i]));
        static_cast<void>(access.updateBitsVerified(kControls[0].first, kControls[0].second, controls[0]));
    }
};

[[noreturn]] void rollbackAndThrow(EcAccess& access, const Snapshot& before, const std::string& error) {
    try { before.restore(access); }
    catch (const std::exception& rollbackError) {
        // Never commit a partially restored table. Try to return control to EC.
        for (const auto [address, mask] : {kControls[0], kControls[2], kControls[3]}) {
            try { static_cast<void>(access.updateBitsVerified(address, mask, 0)); } catch (...) {}
        }
        throw std::runtime_error(error + "; откат не подтверждён: " + rollbackError.what());
    }
    throw std::runtime_error(error + "; прежние настройки EC восстановлены.");
}

void writeTable(EcAccess& access, std::uint16_t base, const std::vector<FanCurvePoint>& points) {
    std::array<std::uint8_t, ec::kTablePointCount> temperatures{};
    std::array<std::uint8_t, ec::kTablePointCount> duties{};
    temperatures.fill(255);
    duties.fill(100);
    for (std::size_t i = 0; i < points.size(); ++i) {
        temperatures[i] = static_cast<std::uint8_t>(points[i].temperature);
        duties[i] = static_cast<std::uint8_t>(points[i].duty * 2);
    }
    for (std::uint16_t i = 0; i < ec::kTablePointCount; ++i) {
        access.writeVerified(static_cast<std::uint16_t>(base + i), temperatures[i]);
        access.writeVerified(static_cast<std::uint16_t>(base + ec::kTableDutyOffset + i), duties[i]);
    }
}

void verifyTable(EcAccess& access, std::uint16_t base, const std::vector<FanCurvePoint>& points) {
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (access.readStable(static_cast<std::uint16_t>(base + i)) != points[i].temperature ||
            access.readStable(static_cast<std::uint16_t>(base + ec::kTableDutyOffset + i)) != points[i].duty * 2)
            throw std::runtime_error("EC не подтвердил записанную таблицу вентилятора.");
    }
}
}

NativeFanState readNativeFanFlags(EcAccess& access) {
    // Один декодер для UI и контроллера. Содержимое RAM-таблиц не определяет
    // активный режим: в Default прошивка может сохранять прежние таблицы.
    // Default подтверждён только при снятых commit/custom/override/boost;
    // неполный набор флагов остаётся смешанным состоянием, а не Default.
    const EcAccess::Transaction transaction(access);
    NativeFanState state;
    state.shared = (access.readStable(ec::kFanControlRespective) & ec::kSplitModeBit) == 0;
    const bool committed = (access.readStable(ec::kRamTableCommit) & ec::kRamTableCommitBit) != 0;
    const bool custom = (access.readStable(ec::kCustomMode) & ec::kCustomModeBit) != 0;
    const bool overrideFan = (access.readStable(ec::kControl727) & ec::kControl727FanBit) != 0;
    state.boost = (access.readStable(ec::kPerformanceMode) & ec::kPerformanceBoostBit) != 0;
    state.active = committed && custom && overrideFan && !state.boost;
    state.defaultMode = !committed && !custom && !overrideFan && !state.boost;
    return state;
}
NativeFanState readNativeFanState(EcAccess& access, bool readGpuTable) {
    // Полный снимок использует ту же семантику флагов, добавляя сами кривые.
    // Idle-наблюдателю нужны только флаги, поэтому таблицы там не читаются.
    const EcAccess::Transaction transaction(access);
    NativeFanState state=readNativeFanFlags(access);
    const auto read = [&](std::uint16_t base, std::vector<FanCurvePoint>& points) {
        const auto temperatures = access.readBlock(base, ec::kTablePointCount);
        const auto duties = access.readBlock(static_cast<std::uint16_t>(base + ec::kTableDutyOffset), ec::kTablePointCount);
        for (std::size_t i = 0; i < temperatures.size(); ++i)
            if (temperatures[i] != 0 && temperatures[i] != 255)
                points.push_back({temperatures[i], duties[i] / 2});
    };
    read(ec::kCpuTableBase, state.cpu);
    if (readGpuTable) read(ec::kGpuTableBase, state.gpu);
    else state.gpu = state.cpu;
    return state;
}

void applyNativeFanCurve(EcAccess& access, const std::vector<FanCurvePoint>& cpu,
                         const std::vector<FanCurvePoint>& gpu, bool shared,
                         FanCurveTarget target) {
    std::string error;
    if (shared) {
        const std::vector<FanCurvePoint>& common = target == FanCurveTarget::Gpu ? gpu : cpu;
        if (target == FanCurveTarget::Both && cpu != gpu)
            throw std::invalid_argument("В общем режиме CPU и GPU должны иметь одну кривую.");
        if (!validateFanCurve(common, error))
            throw std::invalid_argument("Общая кривая: " + error);
    } else {
        if (target != FanCurveTarget::Gpu && !validateFanCurve(cpu, error))
            throw std::invalid_argument("CPU: " + error);
        if (target != FanCurveTarget::Cpu && !validateFanCurve(gpu, error))
            throw std::invalid_argument("GPU: " + error);
    }
    access.ensureWritable();
    const EcAccess::Transaction transaction(access);
    if ((access.readStable(ec::kRamTableCapability) & ec::kRamTableSupportedBit) == 0)
        throw std::runtime_error("EC не сообщил поддержку RAM-таблицы.");

    const bool writeCpu = shared || target != FanCurveTarget::Gpu;
    const bool writeGpu = shared || target != FanCurveTarget::Cpu;
    const std::vector<FanCurvePoint>& common = target == FanCurveTarget::Gpu ? gpu : cpu;
    const std::vector<FanCurvePoint>& cpuWrite = shared ? common : cpu;
    const std::vector<FanCurvePoint>& gpuWrite = shared ? common : gpu;

    const Snapshot before(access);
    try {
        static_cast<void>(access.updateBitsVerified(ec::kRamTableCommit, ec::kRamTableCommitBit, 0));
        static_cast<void>(access.updateBitsVerified(ec::kControl727, ec::kControl727FanBit, 0));
        static_cast<void>(access.updateBitsVerified(ec::kControl727, ec::kControl727FanBit, ec::kControl727FanBit));
        static_cast<void>(access.updateBitsVerified(ec::kCustomMode, ec::kCustomModeBit, ec::kCustomModeBit));
        // Linked curves still need separate EC channels. Hardware common mode
        // ignored the RAM target in the reference session 20261002-014348-142.
        static_cast<void>(access.updateBitsVerified(ec::kFanControlRespective, ec::kSplitModeBit, ec::kSplitModeBit));
        if (writeCpu) writeTable(access, ec::kCpuTableBase, cpuWrite);
        if (writeGpu) writeTable(access, ec::kGpuTableBase, gpuWrite);
        static_cast<void>(access.updateBitsVerified(ec::kRamTableCommit, ec::kRamTableCommitBit, ec::kRamTableCommitBit));
        static_cast<void>(access.updateBitsVerified(ec::kAppPresence, ec::kAppPresenceBit, ec::kAppPresenceBit));
        if (writeCpu) verifyTable(access, ec::kCpuTableBase, cpuWrite);
        if (writeGpu) verifyTable(access, ec::kGpuTableBase, gpuWrite);
        } catch (const std::exception& error) { rollbackAndThrow(access, before, error.what()); }
}

void restoreNativeFanMode(EcAccess& access) {
    // Narrow FAN cleanup: four masks ONLY, preserving APP and RAM contents.
    // Explicit Default additionally uses core resetApplicationControl, which
    // clears every admitted APP consumer and releases APP LAST. Internal
    // recovery/diagnostics do not disable Copilot. Never call from SETs/exit.
    access.ensureWritable();
    const EcAccess::Transaction transaction(access);
    // Default selects firmware ownership. RAM contents and channel arrangement
    // are not a mode selector and must remain untouched.
    constexpr std::array<std::pair<std::uint16_t,std::uint8_t>,4> flags{{
        {ec::kRamTableCommit,ec::kRamTableCommitBit},
        {ec::kCustomMode,ec::kCustomModeBit},
        {ec::kControl727,ec::kControl727FanBit},
        {ec::kPerformanceMode,ec::kPerformanceBoostBit}
    }};
    std::array<std::uint8_t,flags.size()> before{};
    for (std::size_t i=0;i<flags.size();++i) before[i]=access.readStable(flags[i].first);
    try {
        for (const auto& [address,mask] : flags)
            static_cast<void>(access.updateBitsVerified(address,mask,0));
    } catch (...) {
        const auto error=std::current_exception();
        for (std::size_t i=0;i<flags.size();++i)
            static_cast<void>(access.updateBitsVerified(flags[i].first,flags[i].second,before[i]));
        std::rethrow_exception(error);
    }
}

}  // namespace mc
