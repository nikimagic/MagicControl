#include "MagicControl/FanTelemetry.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/DeviceOperation.h"
#include <utility>
namespace mc {
FanTelemetry readFanTelemetry(EcAccess& access) {
    FanTelemetry sample;
    if(deviceOperations().passiveEcReadsPaused())return sample;
    const auto readFan = [&](FanChannel channel, FanReadout& reading) {
        try {
            const auto feedback = access.readFanFeedback(channel, true);
            reading.rpm = feedback.rpm;
            reading.dutyPercent = feedback.dutyPercent;
            reading.rpmAvailable = reading.pwmAvailable = true;
            access.cacheFanTelemetry(channel, feedback);
        } catch (...) {
            if (const auto previous = access.cachedFanTelemetry(channel)) {
                reading.rpm = previous->rpm;
                reading.dutyPercent = previous->dutyPercent;
                reading.rpmAvailable = reading.pwmAvailable = true;
            }
        }
    };
    readFan(FanChannel::Cpu, sample.cpu);
    readFan(FanChannel::Gpu, sample.gpu);
    try{auto native=readNativeDefaultFans(access);
        sample.nativeDefaultFanObservation=native.observation;sample.nativeDefaultFans=std::move(native.fans);
        if(sample.nativeDefaultFans){sample.cpu.pwmAvailable=sample.gpu.pwmAvailable=true;
            sample.cpu.dutyPercent=sample.nativeDefaultFans->actualPercent[0];
            sample.gpu.dutyPercent=sample.nativeDefaultFans->actualPercent[1];}
    }catch(...){} // Observation failure must not affect existing telemetry/controls.
    try {
        sample.ecTemperatureCelsius = access.readTemperature(ec::kCpuTemperature);
    } catch (...) {}
    return sample;
}
}
