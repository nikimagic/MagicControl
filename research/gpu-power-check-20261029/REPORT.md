# Wujie Radeon 880M power — read-only observation

The screenshot shows CPU cores 1.57 W, APU package 7.30 W and Radeon 10 W.
The CPU and package fields originate in separate PawnIO energy counters.
The displayed Radeon field originates in ADLX GPUPower, a generic domain.

Six sequential samples from the explicit read-only probe produced:

| Sample | ADLX GPUPower W | ADL PMLog/23 ASIC W | ADL PMLog/30 GFX W |
|---|---:|---:|---:|
| 0 | 13 | 13 | 1 |
| 1 | 10 | 10 | 1 |
| 2 | 8 | 8 | 1 |
| 3 | 7 | 7 | 1 |
| 4 | 7 | 7 | 1 |
| 5 | 6 | 7 | 1 |

ADLX GPUPower support/getter returned success and supported=true. Board power
reported supported=false, with getter result 12 and no value. Multiple ADL
logical handles in the raw output are not multiple physical GPUs and are not
summed. Getters are sequential, explaining why readings need not be identical
at the end of a changing interval.

This supports treating GPUPower here as a broad ASIC-like observation, not
proven graphics-core-only power. GFX is a separate domain; silently relabelling
1 W as total GPU power would also be unjustified. The exact ASIC boundary on
this APU and its relation to package energy still require verification.
The missing support check in the newer Radeon provider is a separate code
deficiency; it is not the explanation here because support is true.

AMD documents [GPUPower](https://gpuopen.com/manuals/adlx/adlx-sdk-references/adlx-interfaces/performance-monitoring/iadlxgpumetrics/gpupower/)
in watts. It separately defines [GPUTotalBoardPower](https://gpuopen.com/manuals/adlx/adlx-sdk-references/adlx-interfaces/performance-monitoring/iadlxgpumetrics/gputotalboardpower/)
as including board components such as VRAM. The documentation does not establish
the integrated 880M's specific GPUPower boundary. No factor-of-1000 error was
found in the current W→mW→W path.

CPU/package/SMU power was unavailable in the standalone probe, so it does not
validate the screenshot's simultaneous package budget. Missing values were
kept null, not converted to zero. CPU load was about 3.8–5.6% after the first
initialization sample; no stress workload, tuning setter, EC access or startup
restore was invoked. Raw JSON is measurements.jsonl; console source strings
show encoding loss, while numeric values and API codes remain readable.

Reproduction target: magiccontrol_gpu_power_probe, source
tests/gpu_power_readonly_probe.cpp. It is intentionally not a CTest and is not
included in portable/release archives. This investigation changes neither
production telemetry selection nor fan/power settings.
