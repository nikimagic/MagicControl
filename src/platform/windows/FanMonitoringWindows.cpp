#include "FanMonitoringWindows.h"
#include "PowerDiagnosticsWindows.h"
#include "MagicControl/DeviceOperation.h"
#include <exception>
#include <utility>

namespace mc::windows {
FanSensorSnapshot readFanMonitoring(const FanMonitoringContext& c){
    FanSensorSnapshot snapshot;snapshot.typedGeneration=c.generation;
    // Slow generic sources run first. The native EC timestamp must describe
    // the last read of this frame, not expire while another provider is busy.
    try{snapshot.monitoring=c.source?c.source():c.reader.read();}
    catch(const std::exception& error){snapshot.monitoring.processorStatus=std::string("Мониторинг: ")+error.what();}
    if(c.ec){
        const auto reading=readFanTelemetry(*c.ec);
        snapshot.cpu=reading.cpu;snapshot.gpu=reading.gpu;
        snapshot.ecTemperature=reading.ecTemperatureCelsius;
        snapshot.monitoring.nativeDefaultFans=reading.nativeDefaultFans;
        snapshot.monitoring.nativeDefaultFanObservation=reading.nativeDefaultFanObservation;
    }
    // A paused passive EC read does not replace the last typed observation.
    snapshot.typedObservationSkipped=deviceOperations().passiveEcReadsPaused();
    if(c.nativeFans&&!snapshot.typedObservationSkipped)try{
        snapshot.hlyl=c.nativeFans->read();
        snapshot.monitoring.lenovoPowerEcDiagnostic=snapshot.hlyl->lenovoPowerEcDiagnostic;
        if(!c.hpbsd){
            snapshot.monitoring.nativeDefaultFanObservation=snapshot.hlyl->defaultFans.observation;
            snapshot.monitoring.nativeDefaultFans=snapshot.hlyl->defaultFans.fans;
            if(snapshot.monitoring.nativeDefaultFans)for(int ch=0;ch<2;++ch){
                snapshot.monitoring.processors[ch].fanRpm=snapshot.monitoring.nativeDefaultFans->channels[ch].actualRpm;
            }
        }
        for(int channel=0;channel<2;++channel){
            if(snapshot.hlyl->observedRpm[channel]){
                snapshot.monitoring.processors[channel].fanRpm=*snapshot.hlyl->observedRpm[channel];
                snapshot.monitoring.processors[channel].fanSource=snapshot.hlyl->source;
            }
            if(snapshot.hlyl->rawPwm[channel]){
                const auto raw=static_cast<double>(*snapshot.hlyl->rawPwm[channel]);
                snapshot.monitoring.additionalSensors.push_back({channel==0?"Lenovo CPU fan":"Lenovo GPU fan","pwm_raw","raw","Lenovo IT5507 EC · DFE1/DFE2",
                    "PWM mirror; period not available, percent unknown",raw,raw,snapshot.hlyl->rawPwmSampledAtMs,SensorTimestampOrigin::SoftwareObservation});
            }
        }
        if(snapshot.hlyl->actualPercentKnown)for(int channel=0;channel<2;++channel){
            if(c.hpbsd){if(!snapshot.monitoring.processors[channel].fanPwm)snapshot.monitoring.processors[channel].fanPwm=snapshot.hlyl->actualPercent[channel];}
            else{snapshot.monitoring.processors[channel].fanPwm=snapshot.hlyl->actualPercent[channel];snapshot.monitoring.processors[channel].fanSource=snapshot.hlyl->sharedCurve?snapshot.hlyl->source:"Huawei I2EC / HWMI";}
        }
    }catch(const std::exception& error){snapshot.monitoring.processorStatus+=" · ";snapshot.monitoring.processorStatus+=error.what();}
    if(c.gm6&&!snapshot.typedObservationSkipped){snapshot.gm6=c.gm6->read(c.readGmTables);for(int ch=0;ch<2;++ch){auto& p=snapshot.monitoring.processors[ch];
        if(snapshot.gm6->rpm[ch])p.fanRpm=*snapshot.gm6->rpm[ch];if(snapshot.gm6->pwm[ch])p.fanPwm=*snapshot.gm6->pwm[ch];p.fanSource="UWACPIDriver · GM6IR0C EC";}}
    if(snapshot.gm6&&snapshot.gm6->defaultFansConsidered){
        snapshot.monitoring.nativeDefaultFanObservation=snapshot.gm6->defaultFans.observation;
        snapshot.monitoring.nativeDefaultFans=snapshot.gm6->defaultFans.fans;
    }
    for(int channel=0;channel<2;++channel){
        auto& reading=channel==0?snapshot.cpu:snapshot.gpu;
        const auto& temperature=snapshot.monitoring.processors[channel].celsius;
        reading.temperatureAvailable=temperature.has_value();reading.temperatureCelsius=temperature.value_or(0);
        if(channel==1&&c.hpbsd){reading.temperatureAvailable=false;
            for(const auto& sensor:snapshot.monitoring.additionalSensors)if(sensor.device=="HPBSD system"&&sensor.metric=="temperature"&&sensor.value){reading.temperatureAvailable=true;reading.temperatureCelsius=*sensor.value;break;}}
        auto& processor=snapshot.monitoring.processors[channel];
        if(!c.ec){reading.rpmAvailable=processor.fanRpm.has_value();reading.rpm=static_cast<int>(processor.fanRpm.value_or(0));
            reading.pwmAvailable=processor.fanPwm.has_value();reading.dutyPercent=processor.fanPwm.value_or(0);}
        if(reading.rpmAvailable)processor.fanRpm=reading.rpm;
        if(reading.pwmAvailable)processor.fanPwm=reading.dutyPercent;
    }
    recordPowerSample(c.ec,snapshot.monitoring);
    return snapshot;
}
}
