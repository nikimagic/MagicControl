#include "MagicControl/ResourceControl.h"
#include "MagicControl/DeferredTelemetry.h"
#include "OsdSystemMonitor.h"
#include "WindowsDevices.h"

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <dbt.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <optional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mc::windows {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT kAudio=WM_APP+1,kRebind=WM_APP+2,kTouchpad=WM_APP+3,kVolumeKey=WM_APP+4,kLock=WM_APP+5,kBrightness=WM_APP+6,kCamera=WM_APP+7;
constexpr UINT_PTR kTouchpadTimer=1,kAudioTimer=2,kDeferredTelemetryTimer=3;
constexpr GUID kBrightnessGuid{0x8ffee2c6,0x2d01,0x46be,{0xad,0xb9,0x39,0x8a,0xdd,0xc5,0xb4,0xff}};
std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Dispatch {std::atomic<HWND> window{};};
struct AudioNotice {int index,percent;bool muted;std::uint64_t at;};
class AudioCallback final:public IAudioEndpointVolumeCallback {
public:
    AudioCallback(std::shared_ptr<Dispatch> dispatch,int index):dispatch_(std::move(dispatch)),index_(index){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** value)override {
        if(!value)return E_POINTER;*value=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IAudioEndpointVolumeCallback)){*value=static_cast<IAudioEndpointVolumeCallback*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const ULONG count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data)override {
        if(!data)return E_POINTER;
        if(HWND target=dispatch_->window.load()){
            auto* notice=new AudioNotice{index_,static_cast<int>(std::lround(data->fMasterVolume*100)),data->bMuted!=FALSE,now()};
            if(!PostMessageW(target,kAudio,0,reinterpret_cast<LPARAM>(notice)))delete notice;
        }
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};std::shared_ptr<Dispatch> dispatch_;int index_;
};
class DeviceCallback final:public IMMNotificationClient {
public:
    explicit DeviceCallback(std::shared_ptr<Dispatch> dispatch):dispatch_(std::move(dispatch)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** value)override {
        if(!value)return E_POINTER;*value=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IMMNotificationClient)){*value=static_cast<IMMNotificationClient*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const ULONG count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow,ERole,LPCWSTR)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY)override{return S_OK;}
private:
    HRESULT changed(){if(HWND target=dispatch_->window.load())PostMessageW(target,kRebind,0,0);return S_OK;}
    std::atomic<ULONG> refs_{1};std::shared_ptr<Dispatch> dispatch_;
};
std::optional<bool> touchpadEnabled() {
    return readTouchpadEnabled();
}
}

struct OsdSystemMonitor::Impl {
    Callback callback;std::function<void(std::string)> log;
    std::function<void()> keyboardActivity;
    std::shared_ptr<Dispatch> dispatch=std::make_shared<Dispatch>();
    ResourceActor thread;std::atomic<bool> stop{};
    HWND window{};HHOOK hook{};HPOWERNOTIFY power{};
    HANDLE registryEvent{CreateEventW(nullptr,FALSE,FALSE,nullptr)};HKEY registryKey{};
    ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<DeviceCallback> deviceCallback;
    struct Endpoint {ComPtr<IAudioEndpointVolume> volume;ComPtr<AudioCallback> callback;std::optional<int> percent;std::optional<bool> muted;};
    std::array<Endpoint,2> endpoints;
    DeferredTelemetryQueue deferredTelemetry;
    std::array<bool,256> held{};
    std::optional<bool> touchpad;
    std::mutex cameraGate;
    std::condition_variable cameraChanged;
    ResourceActor cameraWorker;
    std::uint64_t cameraRequest{},cameraDeviceChange{},cameraAt{};
    std::optional<int> brightness;
    std::uint64_t lastNativeBrightness{},touchpadAt{},lastTouchpadKey{};
    unsigned touchpadAttempts{};
    bool touchpadFromKey{};
    bool deferredTelemetryTimerArmed{};
    unsigned audioKeyChannels{};
    static inline thread_local Impl* hookOwner{};

    ~Impl(){stop.store(true);cameraChanged.notify_all();if(HWND target=dispatch->window.load())PostMessageW(target,WM_CLOSE,0,0);if(registryEvent)SetEvent(registryEvent);if(thread.joinable())thread.join();if(registryEvent)CloseHandle(registryEvent);}
    void emit(SystemEventKind kind,int value,bool muted=false,std::uint64_t at=0){if(!stop.load()&&callback)callback({kind,value,muted,at?at:now()});}
    void unbindAudio(){
        for(auto& item:endpoints){if(item.volume&&item.callback)item.volume->UnregisterControlChangeNotify(item.callback.Get());item.callback.Reset();item.volume.Reset();item.percent.reset();item.muted.reset();}
    }
    void deferTelemetry(){if(window&&!deferredTelemetryTimerArmed){
        if(SetTimer(window,kDeferredTelemetryTimer,250,nullptr))deferredTelemetryTimerArmed=true;
        else if(log)log("OSD: failed to schedule deferred telemetry");
    }}
    void bindAudioNow(){
        unbindAudio();if(!enumerator)return;
        for(int index=0;index<2;++index){
            ComPtr<IMMDevice> device;
            if(FAILED(enumerator->GetDefaultAudioEndpoint(index?eCapture:eRender,index?eCommunications:eConsole,&device)))continue;
            auto& item=endpoints[index];
            if(FAILED(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(item.volume.GetAddressOf()))))continue;
            float scalar{};BOOL mute{};
            if(SUCCEEDED(item.volume->GetMasterVolumeLevelScalar(&scalar)))item.percent=static_cast<int>(std::lround(scalar*100));
            if(SUCCEEDED(item.volume->GetMute(&mute)))item.muted=mute!=FALSE;
            item.callback.Attach(new AudioCallback(dispatch,index));
            if(FAILED(item.volume->RegisterControlChangeNotify(item.callback.Get()))){item.callback.Reset();item.volume.Reset();}
        }
        if(log)log("OSD AUDIO: Windows volume/microphone callbacks connected; no audio recording");
    }
    void bindAudio(){if(!deferredTelemetry.submit(DeferredTelemetryItem::AudioBind,[this]{bindAudioNow();}))deferTelemetry();}
    void audio(const AudioNotice& notice,bool force=false){
        auto& item=endpoints[notice.index];
        const bool changed=notice.index?item.muted!=notice.muted:(item.percent!=notice.percent||item.muted!=notice.muted);
        item.percent=notice.percent;item.muted=notice.muted;
        if(changed||force)emit(notice.index?SystemEventKind::Microphone:SystemEventKind::Volume,notice.percent,notice.muted,notice.at);
    }
    void snapshotAudioNow(int index){
        auto& item=endpoints[index];if(!item.volume)return;
        float scalar{};BOOL mute{};
        if(SUCCEEDED(item.volume->GetMasterVolumeLevelScalar(&scalar))&&SUCCEEDED(item.volume->GetMute(&mute)))
            audio({index,static_cast<int>(std::lround(scalar*100)),mute!=FALSE,now()},true);
    }
    void snapshotAudio(int index){
        const auto item=index?DeferredTelemetryItem::AudioCapture:DeferredTelemetryItem::AudioRender;
        if(!deferredTelemetry.submit(item,[this,index]{snapshotAudioNow(index);}))deferTelemetry();
    }
    void watchTouchpad(){
        if(registryKey){RegCloseKey(registryKey);registryKey=nullptr;}
        if(RegOpenKeyExW(HKEY_CURRENT_USER,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad",0,KEY_NOTIFY,&registryKey)==ERROR_SUCCESS)
            RegNotifyChangeKeyValue(registryKey,TRUE,REG_NOTIFY_CHANGE_NAME|REG_NOTIFY_CHANGE_LAST_SET,registryEvent,TRUE);
    }
    void initializeTouchpadNow(){touchpad=touchpadEnabled();}
    void initializeTouchpad(){if(!deferredTelemetry.submit(DeferredTelemetryItem::TouchpadInitial,[this]{initializeTouchpadNow();}))deferTelemetry();}
    void checkTouchpadNow(bool fromKey){
        const auto current=touchpadEnabled();
        if(current&&((fromKey&&(*current!=touchpad||touchpadAttempts>=25))||(!fromKey&&touchpad&&*current!=*touchpad))){
            emit(SystemEventKind::Touchpad,*current?1:0,false,touchpadAt?touchpadAt:now());
            touchpad=current;touchpadFromKey=false;KillTimer(window,kTouchpadTimer);
        } else if(fromKey){
            if(++touchpadAttempts>=25){
                emit(SystemEventKind::Touchpad,current?(*current?1:0):-1,false,touchpadAt);
                touchpad=current;touchpadFromKey=false;KillTimer(window,kTouchpadTimer);
            }
        } else if(!touchpad)touchpad=current;
    }
    void checkTouchpad(bool fromKey){
        const auto item=fromKey?DeferredTelemetryItem::TouchpadKeyPoll:DeferredTelemetryItem::TouchpadPoll;
        if(!deferredTelemetry.submit(item,[this,fromKey]{checkTouchpadNow(fromKey);})){if(fromKey)KillTimer(window,kTouchpadTimer);deferTelemetry();}
    }
    void drainDeferredTelemetry(){
        deferredTelemetry.drain([this](DeferredTelemetryItem item){switch(item){
        case DeferredTelemetryItem::AudioBind:bindAudioNow();break;
        case DeferredTelemetryItem::AudioRender:snapshotAudioNow(0);break;
        case DeferredTelemetryItem::AudioCapture:snapshotAudioNow(1);break;
        case DeferredTelemetryItem::TouchpadInitial:initializeTouchpadNow();break;
        case DeferredTelemetryItem::TouchpadPoll:checkTouchpadNow(false);break;
        case DeferredTelemetryItem::TouchpadKeyPoll:checkTouchpadNow(true);break;
        }});
        if(!deferredTelemetry.pending()){
            KillTimer(window,kDeferredTelemetryTimer);
            deferredTelemetryTimerArmed=false;
            if(touchpadFromKey)SetTimer(window,kTouchpadTimer,10,nullptr);
        }
    }
    void touchpadKey(){
        const auto at=now();if(at-lastTouchpadKey<70)return;lastTouchpadKey=at;
        touchpadAt=at;touchpadAttempts=0;touchpadFromKey=true;
        emit(SystemEventKind::Touchpad,-1,false,at);SetTimer(window,kTouchpadTimer,10,nullptr);
    }
    std::optional<bool> cameraState() {
        std::optional<TelemetryGate::Observation> observation;
        while(!stop.load()&&!observation){
            observation=telemetryGate().tryObservation();
            if(!observation)std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if(!observation)return std::nullopt;
        try {return readDevice(DeviceKind::Camera).enabled;}
        catch(const std::exception& error){if(log)log(std::string("OSD CAMERA: ")+error.what());return std::nullopt;}
    }
    void watchCamera() {
        const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        auto previous=cameraState();std::uint64_t handled{},deviceChange{};
        while(!stop.load()) {
            std::uint64_t revision{},at{};
            {
                std::unique_lock lock(cameraGate);
                cameraChanged.wait(lock,[&]{return stop.load()||cameraRequest!=handled||cameraDeviceChange!=deviceChange;});
                if(stop.load())break;
                if(cameraRequest==handled){deviceChange=cameraDeviceChange;lock.unlock();previous=cameraState();continue;}
                revision=cameraRequest;handled=revision;at=cameraAt;deviceChange=cameraDeviceChange;
                cameraChanged.wait_for(lock,std::chrono::milliseconds(50),[&]{return stop.load()||cameraRequest!=revision||cameraDeviceChange!=deviceChange;});
                if(stop.load())break;
                if(cameraRequest!=revision)continue;
                deviceChange=cameraDeviceChange;
            }
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            auto current=cameraState();bool superseded=false;
            while((!current||current==previous)&&!stop.load()&&std::chrono::steady_clock::now()<deadline) {
                std::unique_lock lock(cameraGate);
                cameraChanged.wait_until(lock,deadline,[&]{return stop.load()||cameraRequest!=revision||cameraDeviceChange!=deviceChange;});
                if(stop.load()||cameraRequest!=revision){superseded=true;break;}
                deviceChange=cameraDeviceChange;lock.unlock();current=cameraState();
            }
            {std::lock_guard lock(cameraGate);superseded=superseded||cameraRequest!=revision;}
            if(!stop.load()&&!superseded){previous=current;emit(SystemEventKind::Camera,current?(*current?1:0):-2,false,at);}
        }
        if(SUCCEEDED(apartment))CoUninitialize();
    }
    void start(){thread=ResourceActor([this]{run();});}
    void run(){
        const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        WNDCLASSW klass{};klass.lpfnWndProc=procedure;klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"MagicControl.SystemEvents";RegisterClassW(&klass);
        window=CreateWindowExW(WS_EX_TOOLWINDOW,klass.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,klass.hInstance,this);
        if(!window){if(SUCCEEDED(apartment))CoUninitialize();return;}
        dispatch->window.store(window);
        if(SUCCEEDED(apartment)&&SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator)))){
            deviceCallback.Attach(new DeviceCallback(dispatch));enumerator->RegisterEndpointNotificationCallback(deviceCallback.Get());bindAudio();
        }
        initializeTouchpad();watchTouchpad();cameraWorker=ResourceActor([this]{watchCamera();});
        power=RegisterPowerSettingNotification(window,&kBrightnessGuid,DEVICE_NOTIFY_WINDOW_HANDLE);
        RAWINPUTDEVICE keyboard{1,6,RIDEV_INPUTSINK,window};RegisterRawInputDevices(&keyboard,1,sizeof(keyboard));
        hookOwner=this;hook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboardHook,GetModuleHandleW(nullptr),0);
        if(log)log("OSD INPUT: lock keys, F24, Windows brightness and touchpad notifications connected; EC-reads=0 EC-writes=0");
        bool running=true;
        while(running&&!stop.load()){
            const DWORD wait=MsgWaitForMultipleObjects(registryEvent?1:0,registryEvent?&registryEvent:nullptr,FALSE,INFINITE,QS_ALLINPUT);
            if(registryEvent&&wait==WAIT_OBJECT_0){checkTouchpad(touchpadFromKey);watchTouchpad();}
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                if(message.message==WM_QUIT){running=false;break;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }
        }
        dispatch->window.store(nullptr);
        stop.store(true);cameraChanged.notify_all();if(cameraWorker.joinable())cameraWorker.join();
        if(hook)UnhookWindowsHookEx(hook);hookOwner=nullptr;
        RAWINPUTDEVICE remove{1,6,RIDEV_REMOVE,nullptr};RegisterRawInputDevices(&remove,1,sizeof(remove));
        if(power)UnregisterPowerSettingNotification(power);
        if(registryKey){RegCloseKey(registryKey);registryKey=nullptr;}
        if(enumerator&&deviceCallback)enumerator->UnregisterEndpointNotificationCallback(deviceCallback.Get());
        unbindAudio();deviceCallback.Reset();enumerator.Reset();
        if(IsWindow(window))DestroyWindow(window);
        MSG pending{};while(PeekMessageW(&pending,nullptr,kAudio,kAudio,PM_REMOVE))delete reinterpret_cast<AudioNotice*>(pending.lParam);
        if(SUCCEEDED(apartment))CoUninitialize();
    }
    static LRESULT CALLBACK keyboardHook(int code,WPARAM message,LPARAM data){
        auto* self=hookOwner;
        if(code>=0&&self){
            const auto& key=*reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
            const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
            if(key.vkCode==VK_CAPITAL||key.vkCode==VK_NUMLOCK||key.vkCode==VK_SCROLL){
                if(!down)self->held[key.vkCode]=false;
                else if(!self->held[key.vkCode]){
                    self->held[key.vkCode]=true;
                    PostMessageW(self->window,kLock,key.vkCode,((GetKeyState(static_cast<int>(key.vkCode))&1)==0)?1:0);
                }
            } else if(down&&key.vkCode==VK_F24)PostMessageW(self->window,kTouchpad,0,0);
            else if(down&&(key.vkCode==VK_VOLUME_MUTE||key.vkCode==VK_VOLUME_UP||key.vkCode==VK_VOLUME_DOWN))PostMessageW(self->window,kVolumeKey,0,0);
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
    static LRESULT CALLBACK procedure(HWND target,UINT message,WPARAM wParam,LPARAM lParam){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(target,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);SetWindowLongPtrW(target,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(target,message,wParam,lParam);
        switch(message){
        case kAudio:{std::unique_ptr<AudioNotice> notice(reinterpret_cast<AudioNotice*>(lParam));self->audio(*notice);return 0;}
        case kRebind:self->bindAudio();return 0;
        case kTouchpad:self->touchpadKey();return 0;
        case kCamera:
        {
            const auto at=lParam?static_cast<std::uint64_t>(lParam):now();
            {std::lock_guard lock(self->cameraGate);self->cameraAt=at;++self->cameraRequest;}
            self->emit(SystemEventKind::Camera,-1,false,at);self->cameraChanged.notify_all();return 0;
        }
        case kVolumeKey:self->audioKeyChannels|=wParam?2u:1u;SetTimer(target,kAudioTimer,20,nullptr);return 0;
        case kLock:self->emit(wParam==VK_CAPITAL?SystemEventKind::CapsLock:wParam==VK_NUMLOCK?SystemEventKind::NumLock:SystemEventKind::ScrollLock,static_cast<int>(lParam));return 0;
        case kBrightness:{const auto at=now();if(at-self->lastNativeBrightness>1500&&wParam<=100)self->emit(SystemEventKind::Brightness,static_cast<int>(wParam));return 0;}
        case WM_TIMER:
            if(wParam==kTouchpadTimer)self->checkTouchpad(true);
            else if(wParam==kAudioTimer){KillTimer(target,kAudioTimer);const auto channels=std::exchange(self->audioKeyChannels,0u);if(channels&1)self->snapshotAudio(0);if(channels&2)self->snapshotAudio(1);}
            else if(wParam==kDeferredTelemetryTimer)self->drainDeferredTelemetry();
            return 0;
        case WM_DEVICECHANGE:
            if(wParam==DBT_DEVNODES_CHANGED||wParam==DBT_DEVICEARRIVAL||wParam==DBT_DEVICEREMOVECOMPLETE){
                {std::lock_guard lock(self->cameraGate);++self->cameraDeviceChange;}self->cameraChanged.notify_all();
            }
            return 0;
        case WM_SETTINGCHANGE:self->checkTouchpad(false);return 0;
        case WM_INPUT:{
            UINT size{};GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER));
            if(size&&size<65536){std::vector<BYTE> data(size);
                if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),RID_INPUT,data.data(),&size,sizeof(RAWINPUTHEADER))==size){
                    const auto* input=reinterpret_cast<const RAWINPUT*>(data.data());
                    if(input->header.dwType==RIM_TYPEKEYBOARD) {
                        if(self->keyboardActivity)self->keyboardActivity();
                        if(input->data.keyboard.VKey==VK_F24)self->touchpadKey();
                    }
                }
            }
            break;
        }
        case WM_POWERBROADCAST:
            if(wParam==PBT_POWERSETTINGCHANGE&&lParam){
                const auto* setting=reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
                if(setting->PowerSetting==kBrightnessGuid&&setting->DataLength>=sizeof(DWORD)){
                    DWORD value{};memcpy(&value,setting->Data,sizeof(value));
                    if(value<=100){if(self->brightness&&*self->brightness!=static_cast<int>(value)){
                        self->lastNativeBrightness=now();self->emit(SystemEventKind::Brightness,static_cast<int>(value));
                    }self->brightness=static_cast<int>(value);}
                }
            } else if(wParam==PBT_APMRESUMEAUTOMATIC){self->brightness.reset();self->bindAudio();self->watchTouchpad();self->initializeTouchpad();}
            return TRUE;
        case WM_CLOSE:self->stop.store(true);self->cameraChanged.notify_all();PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(target,message,wParam,lParam);
    }
};
OsdSystemMonitor::OsdSystemMonitor(Callback callback,std::function<void(std::string)> log,
                                 std::function<void()> keyboardActivity):impl_(std::make_unique<Impl>()){
    impl_->callback=std::move(callback);impl_->log=std::move(log);
    impl_->keyboardActivity=std::move(keyboardActivity);impl_->start();
}
OsdSystemMonitor::~OsdSystemMonitor()=default;
void OsdSystemMonitor::touchpadHotkey(){if(HWND target=impl_->dispatch->window.load())PostMessageW(target,kTouchpad,0,0);}
void OsdSystemMonitor::audioHotkey(bool microphone){if(HWND target=impl_->dispatch->window.load())PostMessageW(target,kVolumeKey,microphone?1:0,0);}
void OsdSystemMonitor::brightnessFallback(int percent){if(HWND target=impl_->dispatch->window.load())PostMessageW(target,kBrightness,static_cast<WPARAM>(percent),0);}
void OsdSystemMonitor::cameraHotkey(std::uint64_t receivedAt){if(HWND target=impl_->dispatch->window.load())PostMessageW(target,kCamera,0,static_cast<LPARAM>(receivedAt));}
}
