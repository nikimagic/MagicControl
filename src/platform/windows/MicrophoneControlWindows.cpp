#include "MagicControl/ActionLog.h"
#include "MagicControl/MicrophoneControl.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <propsys.h>
#include <wrl/client.h>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <format>

namespace mc {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error(std::format("Windows Core Audio: HRESULT=0x{:08X}",static_cast<unsigned long>(hr)));}
struct Apartment {
    HRESULT hr{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    Apartment(){if(hr!=RPC_E_CHANGED_MODE)check(hr);}
    ~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}
};
std::string utf8(const wchar_t* value){
    if(!value)return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    if(size<=1)return {};
    std::string text(size,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),size,nullptr,nullptr);text.pop_back();return text;
}
struct Capture {
    Apartment apartment;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> volume;
    std::wstring id;
    std::string detail{"Микрофон по умолчанию · Windows Core Audio"};
    static std::wstring endpointId(IMMDevice* item){LPWSTR raw{};check(item->GetId(&raw));std::wstring value(raw);CoTaskMemFree(raw);return value;}
    Capture(){
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)));
        check(enumerator->GetDefaultAudioEndpoint(eCapture,eCommunications,&device));
        id=endpointId(device.Get());
        check(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(volume.GetAddressOf())));
        ComPtr<IPropertyStore> properties;
        if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))){
            PROPVARIANT value;PropVariantInit(&value);
            if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&value))&&value.vt==VT_LPWSTR){const auto name=utf8(value.pwszVal);if(!name.empty())detail+=" · "+name;}
            PropVariantClear(&value);
        }
    }
    bool enabled(){BOOL muted{};check(volume->GetMute(&muted));return muted==FALSE;}
    void requireSameDefault(){ComPtr<IMMDevice> current;check(enumerator->GetDefaultAudioEndpoint(eCapture,eCommunications,&current));
        if(endpointId(current.Get())!=id)throw std::runtime_error("Микрофон по умолчанию изменился. Повторите действие.");}
};
}
MicrophoneState readDefaultMicrophone(){
    try{Capture capture;const bool enabled=capture.enabled();capture.requireSameDefault();return {enabled,true,capture.detail};}
    catch(const std::exception& error){return {{},false,std::string("Микрофон недоступен: ")+error.what()};}
}
void setDefaultMicrophoneEnabled(bool enabled){
    audit::Scope trace("system.microphone","\"requested_enabled\":"+std::string(enabled?"true":"false"));
    Capture capture;
    capture.requireSameDefault();
    if(capture.enabled()!=enabled)check(capture.volume->SetMute(enabled?FALSE:TRUE,nullptr));
    for(int attempt=0;attempt<8;++attempt){capture.requireSameDefault();if(capture.enabled()==enabled){capture.requireSameDefault();trace.finish("readback_confirmed","\"enabled\":"+std::string(enabled?"true":"false"));return;}std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    throw std::runtime_error("ОС не подтвердила состояние микрофона.");
}
}
