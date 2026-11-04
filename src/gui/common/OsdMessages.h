#pragma once
#include "OsdModel.h"
#include "MagicControl/SystemEvent.h"
#include "MagicControl/LightingZones.h"
#include "KeysPolicy.h"

namespace mc::gui::osd {
inline Message keyboard(int level,std::string detail={},std::uint64_t at=0,int maximum=4) {
    if(maximum<=0||level<0||level>maximum)
        return {"keyboard","Подсветка клавиатуры",detail.empty()?"Состояние недоступно":detail+" · Состояние недоступно",Icon::Keyboard,{},false,false,at};
    return {"keyboard",level?"Подсветка "+std::to_string(level)+" / "+std::to_string(maximum):"Подсветка выключена",
        std::move(detail),Icon::Keyboard,level*100/maximum,false,false,at};
}
inline Message keySwitch(int index,bool on){
    if(index<0||index>=static_cast<int>(ui::keyRowCount))return {"settings","Настройки","Применяю…",Icon::Information};
    static constexpr const char* keys[]{"switch-Windows","osd","switch-Fn","switch-USB","switch-AC","switch-Copilot","switch-Wifi","switch-Bluetooth","switch-Camera","switch-Touchpad","microphone"};
    const Icon icons[]{Icon::Windows,Icon::Notifications,Icon::Keyboard,Icon::Usb,Icon::Power,Icon::Keyboard,Icon::Wifi,Icon::Bluetooth,Icon::Camera,Icon::Touchpad,Icon::Microphone};
    const char* enabled[]{"Windows заблокирована","Экранные уведомления","Fn заблокирована","Питание USB включено","Автовключение включено","Copilot заблокирован","Wi-Fi включён","Bluetooth включён","Камера включена","Тачпад включён","Микрофон включён"};
    const char* disabled[]{"Windows разблокирована","Экранные уведомления","Fn разблокирована","Питание USB выключено","Автовключение выключено","Copilot разблокирован","Wi-Fi выключен","Bluetooth выключен","Камера выключена","Тачпад выключен","Микрофон выключен"};
    return {keys[index],on?enabled[index]:disabled[index],index==1?(on?"Выключаю…":"Включаю…"):"Применяю…",icons[index]};
}
inline Message gpuPower(std::string id,std::string vendor,std::string device,std::optional<int> watts,bool reset){
    return {"gpu-power:"+id,"GPU "+vendor+" · "+(reset?std::string("Штатный лимит"):watts?std::to_string(*watts)+" Вт":"Лимит мощности"),std::move(device),Icon::Power};
}
inline Message system(const mc::SystemEvent& event,const char* platform) {
    Message message;message.receivedAt=event.receivedAt;
    switch(event.kind) {
    case SystemEventKind::Volume:
        if(event.value<0||event.value>100){message={"sound","Громкость","Состояние недоступно",Icon::Sound};message.receivedAt=event.receivedAt;break;}
        message.key="sound";message.title=event.muted?"Звук выключен":"Громкость "+std::to_string(event.value)+"%";
        message.detail=std::string("Громкость ")+platform;message.icon=event.muted?Icon::Muted:Icon::Sound;
        message.percent=event.muted?0:std::clamp(event.value,0,100);break;
    case SystemEventKind::Microphone:
        message.key="microphone";message.title=event.muted?"Микрофон выключен":"Микрофон включён";
        message.detail=std::string("Микрофон ")+platform;message.icon=Icon::Microphone;break;
    case SystemEventKind::Brightness:
        if(event.value<0||event.value>100){message={"brightness","Яркость экрана","Состояние недоступно",Icon::Brightness};message.receivedAt=event.receivedAt;break;}
        message.key="brightness";message.title="Яркость "+std::to_string(event.value)+"%";
        message.detail="Яркость экрана";message.icon=Icon::Brightness;message.percent=std::clamp(event.value,0,100);break;
    case SystemEventKind::KeyboardBacklight:
        message=keyboard(event.value,std::string("Клавиатура ")+platform,event.receivedAt,event.maximum);
        if(event.maximum<=0)message.percent.reset();
        break;
    case SystemEventKind::Touchpad:
        message.key="switch-Touchpad";message.title=event.value<0?"Сенсорная панель":event.value?"Тачпад включён":"Тачпад выключен";
        message.detail=event.value==-1?"Проверяю…":event.value<0?"Состояние устройства не подтверждено":std::string("Текущее состояние ")+platform;
        message.icon=Icon::Touchpad;message.pending=event.value==-1;message.error=event.value < -1;break;
    case SystemEventKind::Camera:
        message.key="switch-Camera";message.title=event.value<0?"Веб-камера":event.value?"Камера включена":"Камера выключена";
        message.detail=event.value==-1?"Проверяю состояние камеры…":event.value<0?"Состояние устройства не подтверждено":std::string("Состояние устройства в ")+platform;
        message.icon=Icon::Camera;message.pending=event.value==-1;message.error=event.value < -1;break;
    default:
        message.key=event.kind==SystemEventKind::CapsLock?"Caps Lock":event.kind==SystemEventKind::NumLock?"Num Lock":"Scroll Lock";
        if(event.value<0){message.title=message.key;message.detail="Состояние недоступно";message.icon=Icon::Lock;break;}
        message.title=message.key+(event.value?" включён":" выключен");message.detail="Состояние клавиши";message.icon=Icon::Keyboard;break;
    }
    return message;
}
}
