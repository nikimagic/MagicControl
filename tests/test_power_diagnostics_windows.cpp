#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/Logging.h"
#include "MagicControl/Monitoring.h"
#include "../src/platform/windows/PowerDiagnosticsWindows.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

class FakeBackend final : public mc::IEcBackend {
public:
    std::array<std::uint8_t, 65536> memory{};
    mc::BackendInfo description;
    unsigned writes{};
    FakeBackend() {
        description.maxAddress = 0xFFFF;
        description.writable = true;
        memory[mc::ec::kBoardEcVersion] = mc::ec::kBoardEcVersionExpected;
        memory[mc::ec::kBoardSystemId] = mc::ec::kBoardSystemIdExpected;
        memory[mc::ec::kBoardProjectId] = mc::ec::kBoardProjectIdExpected;
        memory[mc::ec::kPerformanceMode] = mc::ec::kPerformanceOffice;
        memory[mc::ec::kTypeCStatus] = 0x92;
        memory[mc::ec::kTypeCPowerA] = 100;
    }
    const mc::BackendInfo& info() const noexcept override { return description; }
    std::uint8_t read(std::uint16_t address) override { return memory[address]; }
    void write(std::uint16_t, std::uint8_t) override { ++writes; throw std::runtime_error("Diagnostic attempted a write"); }
};

// Pure formatting test: no logger, windows, system sensors, drivers or EC.
// The values include zero, 255 and missing samples to distinguish them in JSON.
static int checkLenovoDiagnosticJson() {
    const auto require=[](bool ok,const char* message) {
        if (!ok) { std::cerr << message << '\n'; throw std::runtime_error(message); }
    };
    mc::MonitoringSnapshot sample;
    require(mc::windows::lenovoPowerDiagnostic(sample)=="{}", "empty diagnostic must stay empty");
    sample.battery.onlineKnown=true; sample.battery.online=true;
    sample.intelPackagePower.emplace();
    sample.intelPackagePower->msrLimit1Watts=140.; sample.intelPackagePower->msrLimit2Watts=190.;
    sample.intelPackagePower->mmioLimit1Watts=110.; sample.intelPackagePower->mmioLimit2Watts=115.;
    sample.intelPackagePower->msrLimit1Enabled=true; sample.intelPackagePower->mmioLimit1Clamp=true;
    sample.intelPackagePower->msrPl4CurrentRaw=0x800000F0;
    sample.intelPackagePower->msrPl4CurrentAmps=30.; sample.intelPackagePower->msrPl4CurrentLocked=true;
    sample.lenovoPowerEcDiagnostic.emplace();
    auto& ec=*sample.lenovoPowerEcDiagnostic;
    ec.c2a5=0; ec.c29a=10; ec.c364=0x34; ec.c365=0x12;
    ec.c304=6; ec.c305=60; ec.c3b6=1; ec.c3b8=128;
    ec.c4ae=128; ec.c30c=1; ec.c3fe=0; ec.c2c9=255;
    ec.c4be=17; ec.c4ce=47; ec.c200=16; ec.c4c1=2; ec.c4d0=0x08; ec.c4d1=0xf3;
    ec.c22f=8; ec.c2a7=1; ec.c450=0x4a; ec.c451=1;
    ec.c2a2=0x17; ec.c2a3=0x01; ec.c452=0x2c; ec.c453=0x01; ec.c2bb=0x0b; ec.c43e=0x41;
    ec.c5fb=0x80; ec.c5e1=103; ec.c5e3=110;
    ec.startedAtMs=100; ec.sampledAtMs=125; ec.status="twice-read; not atomic";
    const auto json=mc::windows::lenovoPowerDiagnostic(sample);
    for(const auto field: {
        "\"adapter_ec_c2a5\":0", "\"adapter_ec_c29a\":10",
        "\"adapter_ec_c364\":52", "\"adapter_ec_c365\":18",
        "\"adapter_ec_c364_word_from_stable_bytes\":4660", "\"adapter_hw_f01602_bit0\":null",
        "\"adapter_ec_c304\":6", "\"adapter_ec_c305\":60",
        "\"adapter_ec_c3b6\":1", "\"adapter_ec_c3b8\":128",
        "\"adapter_ec_c4ae\":128", "\"adapter_ec_c30c\":1",
        "\"adapter_ec_c3fe\":0", "\"adapter_ec_c2c9\":255", "\"adapter_ec_c4be\":17",
        "\"adapter_ec_c4ce\":47", "\"adapter_ec_c200\":16", "\"adapter_ec_c4c1\":2",
        "\"adapter_ec_c4d0\":8", "\"adapter_ec_c4d1\":243", "\"adapter_ec_c4d0_signed_from_stable_bytes\":-3320",
        "\"adapter_ec_c22f_class\":8", "\"adapter_ec_c2a7\":1",
        "\"adapter_ec_c450\":74", "\"adapter_ec_c451\":1",
        "\"adapter_ec_c2a2\":23", "\"adapter_ec_c2a3\":1", "\"adapter_ec_c2a2_word_from_stable_bytes\":279",
        "\"adapter_ec_c452\":44", "\"adapter_ec_c453\":1", "\"adapter_ec_c452_word_from_stable_bytes\":300",
        "\"adapter_ec_c2bb\":11", "\"adapter_ec_c43e\":65",
        "\"msr_pl1_w\":140.000", "\"msr_pl2_w\":190.000", "\"cltp_mmio_w\":110.000", "\"cstp_mmio_w\":115.000",
        "\"msr_pl1_enabled\":true", "\"mmio_pl1_clamp\":true",
        "\"msr_601_raw\":\"0x800000F0\"", "\"msr_pl4_current_a\":30.000", "\"msr_pl4_current_locked\":true",
        "\"ec_c5e1\":103", "\"ec_c5e3\":110",
        "\"candidate_f9fb_bit7\":true", "\"ec_sampled_at_ms\":125"})
        require(json.find(field)!=std::string::npos,field);
    ec.c364.reset(); ec.c3b8=255; ec.c450.reset();
    const auto incomplete=mc::windows::lenovoPowerDiagnostic(sample);
    for(const auto field: {"\"adapter_ec_c364\":null", "\"adapter_ec_c365\":18",
                           "\"adapter_ec_c364_word_from_stable_bytes\":null", "\"adapter_hw_f01602_bit0\":null",
                           "\"adapter_ec_c3b8\":255", "\"adapter_ec_c450\":null"})
        require(incomplete.find(field)!=std::string::npos,field);
    ec.c364=0; ec.c365=0;
    const auto zeroWord=mc::windows::lenovoPowerDiagnostic(sample);
    require(zeroWord.find("\"adapter_ec_c364_word_from_stable_bytes\":0")!=std::string::npos,
            "a measured zero C364 word must not become null");
    ec.c2a2.reset();
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c2a2_word_from_stable_bytes\":null")!=std::string::npos,
            "missing C2A2 low byte must make word unknown");
    ec.c2a2=0x17; ec.c2a3.reset();
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c2a2_word_from_stable_bytes\":null")!=std::string::npos,
            "missing C2A2 high byte must make word unknown");
    ec.c452.reset();
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c452_word_from_stable_bytes\":null")!=std::string::npos,
            "missing C452 low byte must make word unknown");
    ec.c452=0x2c; ec.c453.reset();
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c452_word_from_stable_bytes\":null")!=std::string::npos,
            "missing C452 high byte must make word unknown");
    ec.c2a2=0; ec.c2a3=0; ec.c452=0; ec.c453=0;
    const auto zeroWords=mc::windows::lenovoPowerDiagnostic(sample);
    require(zeroWords.find("\"adapter_ec_c2a2_word_from_stable_bytes\":0")!=std::string::npos&&
            zeroWords.find("\"adapter_ec_c452_word_from_stable_bytes\":0")!=std::string::npos,
            "zero words must remain zero");
    ec.c2a2=0xff; ec.c2a3=0xff; ec.c452=0xff; ec.c453=0xff; ec.c2bb=0xff; ec.c43e=0xff;
    const auto maximumWords=mc::windows::lenovoPowerDiagnostic(sample);
    require(maximumWords.find("\"adapter_ec_c2a2_word_from_stable_bytes\":65535")!=std::string::npos&&
            maximumWords.find("\"adapter_ec_c452_word_from_stable_bytes\":65535")!=std::string::npos&&
            maximumWords.find("\"adapter_ec_c2bb\":255")!=std::string::npos&&
            maximumWords.find("\"adapter_ec_c43e\":255")!=std::string::npos,
            "FF values must remain raw instead of becoming null");
    ec.c2bb.reset(); ec.c43e.reset();
    const auto missingFlags=mc::windows::lenovoPowerDiagnostic(sample);
    require(missingFlags.find("\"adapter_ec_c2bb\":null")!=std::string::npos&&
            missingFlags.find("\"adapter_ec_c43e\":null")!=std::string::npos&&
            missingFlags.find("\"adapter_ec_c452_word_from_stable_bytes\":65535")!=std::string::npos,
            "unavailable flags must be null without losing stable word");
    ec.c4d0=0; ec.c4d1=0;
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c4d0_signed_from_stable_bytes\":0")!=std::string::npos,
            "zero current must remain zero");
    ec.c4d0=0xd2; ec.c4d1=0x04;
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c4d0_signed_from_stable_bytes\":1234")!=std::string::npos,
            "positive signed current decoded incorrectly");
    ec.c4d0=0; ec.c4d1=0x80;
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c4d0_signed_from_stable_bytes\":-32768")!=std::string::npos,
            "int16 minimum decoded incorrectly");
    ec.c4d0=0xff; ec.c4d1=0x7f;
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c4d0_signed_from_stable_bytes\":32767")!=std::string::npos,
            "int16 maximum decoded incorrectly");
    ec.c4d0.reset();
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c4d0_signed_from_stable_bytes\":null")!=std::string::npos,
            "missing low byte must make current unknown");
    ec.c4d0=0x08; ec.c4d1.reset();
    require(mc::windows::lenovoPowerDiagnostic(sample).find("\"adapter_ec_c4d0_signed_from_stable_bytes\":null")!=std::string::npos,
            "missing high byte must make current unknown");
    sample.lenovoPowerEcDiagnostic.reset(); sample.intelPackagePower.reset();
    require(mc::windows::lenovoPowerDiagnostic(sample)=="{}", "cleared Lenovo snapshot leaked fields");
    std::puts("Lenovo diagnostic JSON: values, missing/unstable bytes and clear passed; hardware accesses=0");
    return 0;
}

int main(int argc, char** argv) {
    if (argc==2 && std::string_view(argv[1])=="--lenovo-json") return checkLenovoDiagnosticJson();
    const auto folder = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("power-diagnostics-test");
    if (!mc::initializeLogging(true, folder)) return 1;
    const auto path = mc::currentLogPath();
    auto fake = std::make_unique<FakeBackend>();
    auto* memory = fake.get();
    mc::EcAccess access(std::move(fake));
    if (!access.verifyBoard().verified) return 2;
    mc::MonitoringSnapshot sample;
    sample.processors[0].celsius = 50;
    sample.processors[0].powerWatts = 7;
    sample.processors[0].temperatureSource = "synthetic test";
    sample.processors[0].powerSource = "synthetic test";
    {
        mc::windows::PowerDiagnostics diagnostics(nullptr);
        const HWND parent = CreateWindowExW(0, L"STATIC", L"Diagnostic test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        const HWND button = CreateWindowExW(0, L"BUTTON", L"Test button", WS_CHILD, 0, 0, 0, 0,
            parent, reinterpret_cast<HMENU>(100), nullptr, nullptr);
        if (!parent || !button) return 3;
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(100, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        mc::windows::recordPowerAction("performance", "requested", 0, "synthetic selection");
        mc::windows::recordPowerAction("performance", "confirmed", 0, "synthetic confirmation");
        for (int i = 0; i < 10; ++i) {
            sample.processorSampledAtMs = GetTickCount64();
            mc::windows::recordPowerSample(&access, sample);
            std::this_thread::sleep_for(std::chrono::milliseconds(1050));
        }
        if (mc::windows::powerDiagnosticBurstActive()) return 4;
        const auto quietSize = std::filesystem::file_size(path);
        mc::windows::recordPowerSample(&access, sample);
        mc::windows::recordPowerSample(&access, sample);
        if (std::filesystem::file_size(path) != quietSize) return 5;
        memory->memory[mc::ec::kPerformanceMode] = mc::ec::kPerformanceTurbo;
        mc::windows::recordPowerSample(&access, sample);
        if (!mc::windows::powerDiagnosticBurstActive()) return 6;
        access.pauseObservations();
        const auto reads = access.readCount();
        mc::windows::recordPowerSample(&access, sample);
        access.resumeObservations();
        if (access.readCount() != reads) return 7;
        const auto enabledSize = std::filesystem::file_size(path);
        mc::setLoggingEnabled(false);
        mc::windows::recordPowerSample(&access, sample);
        mc::windows::recordPowerAction("performance", "requested", 2);
        diagnostics.broadcast(PBT_APMPOWERSTATUSCHANGE, 0);
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(100, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        if (std::filesystem::file_size(path) != enabledSize) return 8;
        DestroyWindow(parent);
    }
    if (memory->writes || access.writeCount()) return 9;
    std::printf("10-sample burst, idle silence, external EC change, observation pause, logging disabled: passed; hardware writes=0\nlog=%s\n", path.string().c_str());
    return 0;
}
