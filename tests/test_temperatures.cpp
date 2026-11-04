#include "TestSupport.h"
#include "MagicControl/Temperature.h"
#include "MagicControl/BoardDescriptor.h"
#include "../src/gui/common/MonitoringCards.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace mc::tests {
int runTemperatureTests(const char* samplesPath, const char* replayPath) {
    CHECK(!decodeZenTctl(0));
    CHECK(!decodeZenTctl(UINT32_MAX));
    CHECK(decodeZenTctl(0x542B0000) == 35.125);
    CHECK(decodeZenTctl(0x69EB0000) == 56.875);
    CHECK(decodeZenTctl((400u << 21) | 0x30000) == 1.0);
    CHECK(decodeZenTctl(400u << 21) == 50.0);
    CHECK(!decodeZenTctl(2047u << 21));
    MonitoringSnapshot fallback;
    fallback.processors[0].celsius = 33;
    fallback.processors[0].temperatureSource = "AMD ADL · CPU";
    applyZenCpuTemperature(fallback, {});
    CHECK(fallback.processors[0].celsius == 33);
    CHECK(fallback.processors[0].temperatureSource == "AMD ADL · CPU");
    CHECK(!fallback.processors[0].tctlCelsius);

    std::ifstream input(samplesPath);
    std::ofstream output(replayPath);
    if (!input || !output) throw std::runtime_error("Cannot open temperature replay paths");
    output << "run\tsample\tcpu_card\tgpu_card\tcpu_source\tgpu_source\tcpu_graph\tgpu_graph\n";
    std::string line;
    std::getline(input,line); // generated TSV header
    int rows{};
    while (std::getline(input,line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> fields;
        std::stringstream stream(line);
        for (std::string field; std::getline(stream,field,'\t');) fields.push_back(field);
        if (fields.size() < 13) throw std::runtime_error("Invalid temperature replay row");
        const auto number = [&](int field) -> std::optional<double> {
            return fields[field].empty() ? std::nullopt : std::optional<double>(std::stod(fields[field]));
        };
        MonitoringSnapshot sample;
        auto& cpu = sample.processors[0]; auto& gpu = sample.processors[1];
        cpu.celsius = cpu.driverCelsius = number(4);
        cpu.temperatureSource = "AMD ADL · CPU";
        gpu.celsius = number(5) ? number(5) : number(6);
        gpu.temperatureSource = number(5) ? "AMD ADL · Edge" : "AMD ADL · GFX";
        if (!fields[3].empty()) {
            const auto decoded = decodeZenTctl(static_cast<std::uint32_t>(std::stoul(fields[3],nullptr,16)));
            CHECK(decoded == number(8)); // recorded probe value, not the UI expectation
            applyZenCpuTemperature(sample,decoded);
        }
        ResourceState state;state.update(BoardDescriptor{},sample);const ResourcePresentationView resources(state);
        const auto cpuCard = gui::ui::processorCard(sample,{},0,"replay",{},&resources);
        const auto gpuCard = gui::ui::processorCard(sample,{},1,"replay",{},&resources);
        CHECK(cpuCard.values[2] == fields[10]);
        CHECK(gpuCard.values[2] == fields[11]);
        // FansPage passes the same unrounded snapshot temperatures to the graphs.
        CHECK(gui::ui::monitoringMetric(cpu.celsius,1,true) == cpuCard.values[2]);
        CHECK(gui::ui::monitoringMetric(gpu.celsius,1,true) == gpuCard.values[2]);
        CHECK(cpuCard.sources[2] == cpu.temperatureSource);
        output << fields[0] << '\t' << fields[1] << '\t' << cpuCard.values[2] << '\t'
               << gpuCard.values[2] << '\t' << cpuCard.sources[2] << '\t' << gpuCard.sources[2] << '\t'
               << cpu.celsius.value_or(-1) << '\t' << gpu.celsius.value_or(-1) << '\n';
        ++rows;
    }
    CHECK(rows > 0);
    std::printf("Temperature replay: %d recorded samples; %d checks, %d failures\n", rows,checkCount(),failureCount());
    return failureCount();
}
} // namespace mc::tests
