"""Recover our exact build inputs in Git's index, preserving concurrent worktree edits."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(__file__).resolve().parents[2]
session=json.loads((root/'build/product-build.json').read_text(encoding='utf-8-sig'))
changes={
'src/core/include/MagicControl/PortableSettings.h':[(
'bool loadDevicePreferences(const std::filesystem::path&, DevicePreferences&, std::string& error);\n',
'''bool loadDevicePreferences(const std::filesystem::path&, DevicePreferences&, std::string& error);
// KEYs save and lifecycle restore must resolve the same model-specific file.
std::filesystem::path keysPreferencesPath(std::string_view profileId);
// Owner request27.10.2026: opening CC or resuming must NEVER enable Bluetooth
// from an old saved snapshot. Windows owns its lifecycle state; only an explicit
// KEYs button may change it. Keep this rule common to every device adapter.
[[nodiscard]] constexpr bool restoreDevicePreferenceOnLifecycle(DeviceSetting key) noexcept {
    return key != DeviceSetting::Bluetooth;
}
''')],
'src/core/src/PortableSettings.cpp':[(
'int selectedChargeRestart(std::string_view id,int fallback){',
'''std::filesystem::path keysPreferencesPath(std::string_view id){
    if(ramFanControlProfile(id))return portableSettingsPath(std::string(gmSettings(id)));
    if(id=="huawei-hlyl-wxx9")return portableSettingsPath("hlyl-wxx9-device-settings.ini");
    return portableSettingsPath("device-settings.ini");
}
int selectedChargeRestart(std::string_view id,int fallback){''')],
'src/gui/windows/KeysPage.cpp':[(
'const auto target=uniwillQuickSwitchProfile(profileId)||profileId=="huawei-hlyl-wxx9"?batteryPreferencesPath(profileId):portableSettingsPath("device-settings.ini");',
'const auto target=keysPreferencesPath(profileId);')],
'src/gui/windows/MainWindow.cpp':[(
'''if (loadDevicePreferences(portableSettingsPath("device-settings.ini"), saved, error) &&
            (saved[DeviceSetting::Wifi] || saved[DeviceSetting::Bluetooth]))''',
'''if (loadDevicePreferences(keysPreferencesPath(deviceProfile(readBoardDescriptor()).id), saved, error) &&
            saved[DeviceSetting::Wifi])''')],
'src/devices/mechrevo/gm6ir0c/ControlWindows.cpp':[(
'loadDevicePreferences(portableSettingsPath("device-settings.ini"),devices,settingsError)',
'loadDevicePreferences(keysPreferencesPath(deviceProfile(readBoardDescriptor()).id),devices,settingsError)'),(
'''                const auto value=devices[static_cast<DeviceSetting>(static_cast<int>(DeviceSetting::Wifi)+i)];''',
'''                const auto key=static_cast<DeviceSetting>(static_cast<int>(DeviceSetting::Wifi)+i);
                if(!restoreDevicePreferenceOnLifecycle(key)){
                    logMessage("configuration","RESTORE skipped: Bluetooth; Windows lifecycle state preserved");continue;
                }
                const auto value=devices[key];''')],
'src/devices/mechrevo/x5sp4nag/windows/ConfigurationRestoreWindows.cpp':[(
'''                    const auto key = static_cast<S>(static_cast<int>(S::Wifi) + index);
                    if (!saved[key]) continue;''',
'''                    const auto key = static_cast<S>(static_cast<int>(S::Wifi) + index);
                    if (!restoreDevicePreferenceOnLifecycle(key)) {
                        log("RESTORE skipped: Bluetooth; Windows lifecycle state preserved");
                        continue;
                    }
                    if (!saved[key]) continue;'''),(
'''                        if (!actual.enabled || *actual.enabled == (*saved[key] != 0)) return;''',
'''                        if (!actual.available || !actual.enabled) throw std::runtime_error("Состояние устройства недоступно; восстановление не подтверждено");
                        if (*actual.enabled == (*saved[key] != 0)) return;''')],
'src/gui/linux/KeysPage.cpp':[(
'''    auto next=state.preferences;next.osdDisabled=disabled;std::string error;''',
'''    auto next=state.preferences;std::string error;
    // Other pages may have changed language/logging since KEYs was created.
    // Reload before changing this one field; do not overwrite their newer values.
    if(!mc::loadApplicationPreferences(mc::portableSettingsPath("app-settings.ini"),next,error)&&!error.empty()){
        state.setStatus("Не удалось прочитать настройку: "+error);return;
    }
    next.osdDisabled=disabled;''')],
}
recovered=[]
for path,replacements in changes.items():
    data=subprocess.check_output(['git','show','HEAD^:'+path],cwd=root).decode('utf-8')
    for old,new in replacements:
        if data.count(old)!=1:raise RuntimeError(f'{path}: replacement count {data.count(old)}')
        data=data.replace(old,new)
    data=data.replace('\r\n','\n')
    expected=session['application_inputs'][path]
    variants=[data.encode(),data.replace('\n','\r\n').encode()]
    if not any(hashlib.sha256(v).hexdigest()==expected for v in variants):
        raise RuntimeError(f'{path}: candidate does not match compiled source fingerprint')
    recovered.append((path,data.encode(),expected))
    print(path,'matches compiled input')
if '--stage' in sys.argv:
    for path,data,expected in recovered:
        blob=subprocess.check_output(['git','hash-object','-w','--stdin'],input=data,cwd=root).decode().strip()
        subprocess.run(['git','update-index','--cacheinfo','100644',blob,path],cwd=root,check=True)
    print('Index updated; working files were not modified.')
