#include "ControlWindows.h"
#include "WmiWindows.h"
#include "I2EcWindows.h"
#include "../../../protocols/lenovo/LegionFirmware.h"
#include "../../../protocols/lenovo/LegionPowerObservation.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/CustomPresets.h"
#include "MagicControl/Logging.h"
#include "MagicControl/SensorDecoders.h"
#include "MagicControl/BatteryIdentityHistory.h"
#include "MagicControl/PawnIoInstall.h"
#include "../../../platform/windows/BinaryHashWindows.h"
#include "../../../platform/windows/ConfigurationRestoreWindows.h"
#include "../../../platform/windows/FanDeviceActionsWindows.h"
#include "../../../platform/windows/PowerDiagnosticsWindows.h"
#include "MagicControl/DeviceOperation.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <sstream>
#include <atomic>
#include <charconv>
#include <format>
namespace mc::windows {
namespace {
constexpr char preferenceFile[]="lenovo-legion-kwcn-power.ini";
constexpr char leaseFile[]="lenovo-legion-kwcn-fan-lease.txt";
constexpr char energyPreferences[]="lenovo-energy-settings.ini";
// Lenovo AcpiVpc EnergyDrv ABI, matched against this device's BIOS KBLC and
// GBMD/SBMC and the vendor-signed driver. No direct EC or FAN writes here.
class EnergySession {
    HANDLE handle_{INVALID_HANDLE_VALUE};
public:
    EnergySession(){const auto status=dependencies::lenovoEnergyDriverStatus();
        if(!status.installed||!status.running||status.resolvedBinaryPath.empty())throw std::runtime_error("Lenovo Energy: драйвер не установлен или не запущен");
        const auto path=std::filesystem::path(status.resolvedBinaryPath);const auto size=std::filesystem::file_size(path);
        if(size!=48896)throw std::runtime_error("Lenovo Energy: неподтверждённая версия драйвера");
        std::ifstream in(path,std::ios::binary);std::vector<char> binary(static_cast<std::size_t>(size));
        if(!in.read(binary.data(),static_cast<std::streamsize>(binary.size()))||detail::binarySha256(binary)!="6bc61af7806a6482754af7757ae7dd013d345bcc92e18901f6474fefc3e07c8d")
            throw std::runtime_error("Lenovo Energy: неподтверждённый файл драйвера");
        handle_=CreateFileW(L"\\\\.\\EnergyDrv",FILE_READ_DATA|FILE_WRITE_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle_==INVALID_HANDLE_VALUE)throw std::runtime_error("Lenovo Energy: драйвер недоступен ("+std::to_string(GetLastError())+")");}
    ~EnergySession(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
    EnergySession(const EnergySession&)=delete;EnergySession& operator=(const EnergySession&)=delete;
    std::uint32_t call(DWORD ioctl,std::uint32_t input,bool expectOutput=true){std::uint32_t output{};DWORD bytes{};
        if(!DeviceIoControl(handle_,ioctl,&input,sizeof(input),&output,sizeof(output),&bytes,nullptr)||(expectOutput&&bytes<sizeof(output)))
            throw std::runtime_error("Lenovo Energy: IOCTL не подтверждён ("+std::to_string(GetLastError())+")");return output;}
    int keyboard(){if((call(0x83102144,1)>>1)!=2)throw std::runtime_error("Lenovo: белая подсветка не подтверждена драйвером");
        switch(call(0x83102144,0x22)){case 1:return 0;case 3:return 1;case 5:return 2;default:throw std::runtime_error("Lenovo: неизвестный уровень подсветки");}}
    int charge(){const auto v=call(0x831020f8,0xff);const auto bits=((v&0xff)<<24)|((v&0xff00)<<8)|((v>>8)&0xff00)|((v>>24)&0xff);
        if(bits&(1u<<17))return bits&(1u<<26)?2:0;if(bits&(1u<<29))return 1;
        throw std::runtime_error("Lenovo: GBMD не сообщил состояние зарядки");}
    void keyboardSet(int value){call(0x83102144,0x23u|(static_cast<std::uint32_t>(value)<<16));}
    void chargeCommand(std::uint32_t command){call(0x831020f8,command,false);}
};
void rememberEnergy(DeviceSetting key,int value){DevicePreferences patch;patch[key]=value;std::string error;
    if(!rememberDevicePreferences(patch,error,portableSettingsPath(energyPreferences)))throw std::runtime_error("Lenovo: настройка применена, но не сохранена: "+error);}
std::string machineKey(){HKEY key{};if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Cryptography",0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key)!=ERROR_SUCCESS)throw std::runtime_error("Lenovo: machine identity unavailable");
    wchar_t value[256]{};DWORD n=sizeof(value),type{};const auto r=RegQueryValueExW(key,L"MachineGuid",nullptr,&type,reinterpret_cast<BYTE*>(value),&n);RegCloseKey(key);
    if(r!=ERROR_SUCCESS||type!=REG_SZ)throw std::runtime_error("Lenovo: machine identity unreadable");std::string result;for(const auto c:std::wstring_view(value)){if(c>127||c<=32)throw std::runtime_error("Lenovo: invalid machine identity");result+=static_cast<char>(c);}if(result.empty())throw std::runtime_error("Lenovo: empty machine identity");return result;}
struct FanLease{bool committed{},returning{};int kind{1},beforeCount{},afterCount{};std::uint8_t flags{};std::string machine;lenovo::FanPacket before{},after{};std::array<int,10> beforeMap{},afterMap{};NativeFanCurves requested{};};
std::string packetHex(const lenovo::FanPacket& p){constexpr char h[]="0123456789abcdef";std::string s;for(auto b:p){s+=h[b>>4];s+=h[b&15];}return s;}
lenovo::FanPacket parsePacket(const std::string& s){lenovo::FanPacket p;if(s.size()!=p.size()*2)throw std::runtime_error("Lenovo: invalid lease packet size");
    for(std::size_t i=0;i<p.size();++i){unsigned x{};const char* first=s.data()+i*2;const auto [end,error]=std::from_chars(first,first+2,x,16);if(error!=std::errc{}||end!=first+2||x>255)throw std::runtime_error("Lenovo: invalid lease hex");p[i]=static_cast<std::uint8_t>(x);}return p;}
std::optional<FanLease> loadLease(){const auto p=settingsReadPath(portableSettingsPath(leaseFile));if(!std::filesystem::exists(p))return {};
    if(std::filesystem::file_size(p)>8192)throw std::runtime_error("Lenovo: oversized FAN lease");std::ifstream in(p);std::string tag,a,b,extra;int version{},flags{},committed{},returning{};FanLease l;
    if(!(in>>tag>>version>>l.machine>>committed>>returning>>l.kind>>l.beforeCount>>l.afterCount>>flags>>a>>b)||tag!="LENOVO_KWCN_FAN"||version!=3||committed<0||committed>1||returning<0||returning>1||l.kind<1||l.kind>3||l.beforeCount<1||l.beforeCount>13||l.afterCount<1||l.afterCount>13||flags<0||flags>255||(flags&3))throw std::runtime_error("Lenovo: malformed FAN lease");
    l.committed=committed!=0;l.returning=returning!=0;l.flags=static_cast<std::uint8_t>(flags);l.before=parsePacket(a);l.after=parsePacket(b);
    for(auto& v:l.beforeMap)if(!(in>>v)||v<0||v>255)throw std::runtime_error("Lenovo: invalid original FAN level map");
    for(auto& v:l.afterMap)if(!(in>>v)||v<0||v>255)throw std::runtime_error("Lenovo: invalid applied FAN level map");
    for(auto& c:l.requested)for(auto& v:c)if(!(in>>v.temperature>>v.duty))throw std::runtime_error("Lenovo: incomplete FAN draft in lease");
    if(in>>extra)throw std::runtime_error("Lenovo: trailing FAN lease data");return l;
}
void saveLease(const FanLease& l){std::ostringstream out;out<<"LENOVO_KWCN_FAN 3 "<<l.machine<<' '<<l.committed<<' '<<l.returning<<' '<<l.kind<<' '<<l.beforeCount<<' '<<l.afterCount<<' '<<int(l.flags)<<'\n'<<packetHex(l.before)<<'\n'<<packetHex(l.after)<<'\n';
    for(auto v:l.beforeMap)out<<v<<' ';out<<'\n';for(auto v:l.afterMap)out<<v<<' ';out<<'\n';
    for(const auto& c:l.requested)for(auto v:c)out<<v.temperature<<' '<<v.duty<<'\n';std::string error;if(!savePortableText(portableSettingsPath(leaseFile),out.str(),error))throw std::runtime_error("Lenovo: FAN journal not saved: "+error);}
void clearLease(){const auto p=portableSettingsPath(leaseFile);if(std::filesystem::exists(p))std::filesystem::remove(p);}
struct RamState{lenovo::FanPacket table{};int count{},selected{},flags{},overrideFlags{},stage{},custom{},fullSpeed{},cpuRaw{},sensors{},fans{};std::array<int,10> map{};std::array<int,2> rpm{},target{},rawPwm{};std::array<int,6> temperatures{},stages{};};
RamState readRam(legion::FanRam& ram){
    for(int attempt=0;attempt<2;++attempt){RamState s;s.count=ram.read(0xdfef);s.flags=ram.read(0xdfe0);s.selected=ram.read(0xdfeb);s.overrideFlags=ram.read(0xdfdd);
        if(s.count<1||s.count>13||s.selected>=28||ram.read(0xc200)!=0x10)throw std::runtime_error("Lenovo: invalid live FAN context/non-S0");
        s.custom=ram.read(0xc41d)&1;s.fullSpeed=ram.read(0xc41d)&4;s.cpuRaw=ram.read(0xc4b0);s.sensors=ram.read(0xdff1);s.fans=ram.read(0xdff0)&3;s.table=ram.table();
        for(int i=0;i<10;++i)s.map[i]=ram.read(static_cast<std::uint16_t>(0xc5f0+i));
        for(int ch=0;ch<2;++ch){s.rpm[ch]=int(ram.read(ch?0xc4fe:0xc406))*100;s.target[ch]=int(ram.read(static_cast<std::uint16_t>(0xdfd4+ch)))*100;
            s.rawPwm[ch]=ram.read(static_cast<std::uint16_t>(0xdfe1+ch));}
        for(int i=0;i<6;++i){s.temperatures[i]=ram.read(static_cast<std::uint16_t>(0xdffa+i));s.stages[i]=ram.read(static_cast<std::uint16_t>(0xdfe4+i));}
        s.stage=ram.read(0xdfea);
        const auto state=ram.read(0xc41d);
        if(s.custom==(state&1)&&s.fullSpeed==(state&4)&&s.count==ram.read(0xdfef)&&s.flags==ram.read(0xdfe0)&&s.selected==ram.read(0xdfeb)&&s.overrideFlags==ram.read(0xdfdd))return s;
    }throw std::runtime_error("Lenovo: FAN snapshot changed while reading");
}
void resetStages(legion::FanRam& r){for(int i=0;i<7;++i)r.write(static_cast<std::uint16_t>(0xdfe4+i),0);}
bool admitted(){
    if(wcsstr(GetCommandLineW(),L"--no-ec"))return false;
    const auto board=readBoardDescriptor();
    if(deviceProfile(board).id!=lenovo::profileId)return false;
    HKEY key{};if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;
    wchar_t version[256]{};DWORD size=sizeof(version),type{};const auto result=RegQueryValueExW(key,L"BIOSVersion",nullptr,&type,reinterpret_cast<BYTE*>(version),&size);RegCloseKey(key);
    // Admission belongs to this reviewed ABI, not to a model substring/CPU/GPU.
    return result==ERROR_SUCCESS&&type==REG_SZ&&std::wstring_view(version)==L"KWCN54WW";
}
std::optional<CpuPowerChoice> loadChoice(){
    const auto path=settingsReadPath(portableSettingsPath(preferenceFile));if(!std::filesystem::exists(path))return {};
    if(std::filesystem::file_size(path)>4096)throw std::runtime_error("Lenovo: oversized power preference");
    std::ifstream in(path);std::string tag,kind,extra;int version{};CpuPowerChoice choice;std::size_t count{};
    if(!(in>>tag>>version>>kind>>choice.baseProfile)||tag!="LENOVO_KWCN_POWER"||version!=1||(kind!="stock"&&kind!="custom")||choice.baseProfile<0||choice.baseProfile>2)throw std::runtime_error("Lenovo: malformed power preference");
    choice.custom=kind=="custom";
    if(choice.custom){if(!(in>>choice.values[0]>>choice.values[1]>>choice.values[2]>>count)||count>32)throw std::runtime_error("Lenovo: malformed Custom preference");
        for(std::size_t i=0;i<count;++i){std::string key;int value{};if(!(in>>key>>value)||key.size()>64||value<0||value>100000||!choice.options.emplace(key,value).second)throw std::runtime_error("Lenovo: malformed power option");}}
    if(in>>extra)throw std::runtime_error("Lenovo: trailing power preference");return choice;
}
void saveChoice(const CpuPowerChoice& c){
    std::ostringstream out;out<<"LENOVO_KWCN_POWER 1 "<<(c.custom?"custom":"stock")<<' '<<c.baseProfile<<'\n';
    if(c.custom){out<<c.values[0]<<' '<<c.values[1]<<' '<<c.values[2]<<' '<<c.options.size()<<'\n';for(const auto& [k,v]:c.options)out<<k<<' '<<v<<'\n';}
    std::string error;if(!savePortableText(portableSettingsPath(preferenceFile),out.str(),error))throw std::runtime_error("Lenovo: power preference not saved: "+error);
}
class Controller final:public NativeFixedFans {
public:
    std::recursive_mutex gate;
    LaptopControlSnapshot snapshot;
    int modeCode{};std::uint64_t metadataAt{};
    lenovo::Catalog metadata;
    std::vector<lenovo::FanRow> fanMetadata;
    std::atomic_bool paused{},stopped{};
    NativeFanCurves draft{};bool draftLoaded{};
    EcBatteryReading batteryCache;std::uint64_t batterySampleAt{};
    std::optional<unsigned> adapterWattCache;
    std::optional<std::uint8_t> adapterMuxCache;
    std::vector<EcBatteryRegisterReading> adapterRegisterCache;
    std::uint64_t adapterAttemptAt{},adapterCandidateAtMs{};
    std::optional<LenovoPowerEcDiagnostic> powerEcDiagnostic;
    std::uint64_t powerEcReadAt{};
    std::unique_ptr<ConfigurationRestore> restore;
    void synchronizeOwnedFanMap(){
        auto lease=loadLease();if(!lease||lease->machine!=machineKey()||!lease->committed)return;
        legion::FanRam r;const auto actual=readRam(r);if(!actual.custom||actual.table!=lease->after||(actual.flags&3)!=1)return;
        if(actual.map==lease->afterMap)return;
        if(actual.overrideFlags||actual.count!=lease->afterCount||!lenovo::activeLevelMapValid(actual.map))throw std::runtime_error("Lenovo: FAN ownership changed while entering Custom");
        // Entering Custom reinitializes the OEM index indirection. Capture its
        // new default before installing identity, otherwise edited stages7..10
        // would secretly reuse lower stages. Power/temperature limits stay intact.
        lease->beforeMap=actual.map;
        if(actual.fullSpeed){
            // Full Speed bypasses the Manual map. Capture the OEM mode change
            // without rewriting any FAN bytes; Manual re-applies its own map.
            lease->afterMap=actual.map;saveLease(*lease);return;
        }
        for(int i=0;i<10;++i)lease->afterMap[i]=i+1;lease->committed=false;saveLease(*lease);
        try{r.write(lenovo::tableFlags,static_cast<std::uint8_t>(actual.flags|3));
            for(int i=0;i<10;++i)r.write(static_cast<std::uint16_t>(0xc5f0+i),static_cast<std::uint8_t>(i+1));resetStages(r);
            r.write(lenovo::tableFlags,static_cast<std::uint8_t>((r.read(lenovo::tableFlags)&0xfc)|1));r.finish();lease->committed=true;saveLease(*lease);
        }catch(...){try{r.finish();}catch(...){}
            try{legion::FanRam recovery;const auto now=readRam(recovery);
                if(now.table!=actual.table||now.overrideFlags||now.fullSpeed)throw std::runtime_error("Lenovo: FAN ownership changed during index-map recovery");
                recovery.write(lenovo::tableFlags,static_cast<std::uint8_t>(now.flags|3));
                for(int i=0;i<10;++i)recovery.write(static_cast<std::uint16_t>(0xc5f0+i),static_cast<std::uint8_t>(actual.map[i]));resetStages(recovery);
                recovery.write(lenovo::tableFlags,static_cast<std::uint8_t>((recovery.read(lenovo::tableFlags)&0xfc)|1));recovery.finish();
                lease->afterMap=actual.map;lease->committed=true;saveLease(*lease);
            }catch(...){logMessage("lenovo_fans","Index-map recovery not confirmed; FAN journal retained");}throw;}
    }
    // The same device owner is used by CPU, GPU and FAN resource operations.
    void require(){if(!admitted()||paused||stopped)throw std::runtime_error("Lenovo: actual device/BIOS unavailable or suspended");}
    void populateEnergy(LaptopControlSnapshot& next){
        try{EnergySession energy;next.chargeMode=energy.charge();next.chargeModeWritable=true;}
        catch(const std::exception& e){next.chargeMode.reset();next.chargeModeWritable=false;next.status+=std::string("; Lenovo Energy battery: ")+e.what();}
        try{EnergySession energy;next.keyboardLevel=energy.keyboard();next.keyboardWritable=true;next.keyboardStatus="Lenovo Energy · BIOS KBLC";}
        catch(const std::exception& e){next.keyboardLevel.reset();next.keyboardWritable=false;next.keyboardStatus=e.what();}
    }
    void energyMode(int target){std::lock_guard lock(gate);require();if(target<0||target>2)throw std::invalid_argument("Lenovo: неизвестный режим зарядки");
        EnergySession energy;int current=energy.charge();
        const auto change=[&](int expected,std::uint32_t command,int after){require();if(energy.charge()!=expected)throw std::runtime_error("Lenovo: режим изменён другой программой");energy.chargeCommand(command);
            for(int attempt=0;attempt<10;++attempt){require();if(energy.charge()==after)return;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
            throw std::runtime_error("Lenovo: EC не подтвердил режим зарядки");};
        if(current!=target){if(current==1){change(1,5,0);current=0;}else if(current==2){change(2,8,0);current=0;}
            if(target==1)change(current,3,1);else if(target==2)change(current,7,2);}
        if(energy.charge()!=target)throw std::runtime_error("Lenovo: итоговый режим зарядки не подтверждён");
        rememberEnergy(DeviceSetting::BatteryProtection,target);
    }
    void energyLight(int target){std::lock_guard lock(gate);require();if(target<0||target>2)throw std::invalid_argument("Lenovo: уровень подсветки вне 0–2");
        EnergySession energy;const auto before=energy.keyboard();if(before!=target){energy.keyboardSet(target);
            bool confirmed=false;for(int attempt=0;attempt<10;++attempt){require();if(energy.keyboard()==target){confirmed=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
            if(!confirmed)throw std::runtime_error("Lenovo: яркость не подтверждена чтением");}
        rememberEnergy(DeviceSetting::KeyboardLevel,target);
    }
    void refresh(legion::Session& session,bool power=true,bool forceMetadata=false){
        require();const auto mode=session.mode();modeCode=mode;
        const auto now=GetTickCount64();
        if(forceMetadata||fanMetadata.empty()||now-metadataAt>=3000){
            metadata=session.catalog();fanMetadata=lenovo::fanRows(session.query("LENOVO_FAN_TABLE_DATA"));metadataAt=now;
        }
        LaptopControlSnapshot next=power?LaptopControlSnapshot{}:snapshot;next.connected=true;next.source="Lenovo · native WMI · KWCN54WW";
        next.stockCpuProfile.reset();next.customCpuPowerActive=mode==255;if(mode>=1&&mode<=3)next.stockCpuProfile=mode-1;
        // Firmware, not Windows ACLineStatus or cached SKU, admits each mode.
        next.stockCpuProfileWritable=true;
        for(int stock=1;stock<=3;++stock)try{for(const auto id:lenovo::cpuIds)static_cast<void>(metadata.setting(id,stock,"","",""));}
            catch(...){next.stockCpuProfileWritable=false;}
        if(power){
        std::array<int,3> cpu{};
        constexpr const char* cpuKeys[]{"cpu.cross","cpu.pl1","cpu.pl2"};constexpr const char* labels[]{"Cross Loading","PL1","PL2"};
        bool writable=true;
        for(int i=0;i<3;++i){auto field=metadata.setting(lenovo::cpuIds[i],255,cpuKeys[i],labels[i],"W");field.actual=session.feature(lenovo::cpuIds[i]);cpu[i]=*field.actual;writable=writable&&field.writable;next.customCpuPowerSettings[i]=std::move(field);}
        next.customCpuPowerLimits=cpu;
        for(const auto& entry:lenovo::optionIds){auto field=metadata.setting(entry.id,255,entry.key,entry.label,entry.unit);field.actual=session.feature(entry.id);next.customPowerOptions.push_back(std::move(field));}
        next.customCpuPowerWritable=writable;
        }
        for(int ch=0;ch<2;++ch)next.fanRpm[ch]=session.feature(ch?0x04030002:0x04030001);
        next.cpuCelsius=session.feature(0x05040000);
        if(power&&mode==255)if(const auto chosen=loadChoice();chosen&&chosen->custom){
            next.customCpuPowerActive=chosen->values==next.customCpuPowerLimits;
            for(const auto& [key,value]:chosen->options){const auto found=std::find_if(next.customPowerOptions.begin(),next.customPowerOptions.end(),[&](const auto& f){return f.key==key;});
                if(found==next.customPowerOptions.end()||found->actual!=value)next.customCpuPowerActive=false;}
        }
        next.status="SKU, ranges, discrete values and RPM ladders read from firmware WMI";if(power)populateEnergy(next);
        snapshot=std::move(next);
    }
    std::array<int,3> recipe(int index){
        std::lock_guard lock(gate);require();if(index<0||index>2)throw std::runtime_error("Lenovo: invalid stock recipe");
        std::array<int,3> values;
        for(int i=0;i<3;++i)values[i]=*metadata.setting(lenovo::cpuIds[i],index+1,"","","").defaultValue;return values;
    }
    PowerOptions optionRecipe(int index){
        std::lock_guard lock(gate);require();if(index<0||index>2)throw std::runtime_error("Lenovo: invalid option recipe");
        PowerOptions result;
        for(const auto& o:lenovo::optionIds)result[o.key]=*metadata.setting(o.id,index+1,o.key,o.label,o.unit).defaultValue;return result;
    }
    void validate(const std::array<int,3>& cpu,const PowerOptions& options)const{
        for(int i=0;i<3;++i)if(!snapshot.customCpuPowerSettings[i]||!snapshot.customCpuPowerSettings[i]->accepts(cpu[i]))throw std::runtime_error("Lenovo: CPU limit is outside the current WMI capability");
        for(const auto& [key,value]:options){const auto f=std::find_if(snapshot.customPowerOptions.begin(),snapshot.customPowerOptions.end(),[&](const auto& p){return p.key==key;});
            if(f==snapshot.customPowerOptions.end()||!f->accepts(value))throw std::runtime_error("Lenovo: unsupported/disallowed power option: "+key);}
    }
    void stock(int index){
        std::lock_guard lock(gate);require();if(index<0||index>2)throw std::runtime_error("Lenovo: invalid stock mode");
        legion::Session s;refresh(s,true,true);if(!snapshot.stockCpuProfileWritable)throw std::runtime_error("Lenovo: stock modes not declared by WMI");
        const int before=s.mode();s.mode(index+1);
        try{if(s.mode()!=index+1)throw std::runtime_error("Lenovo: firmware did not enter requested stock mode (check adapter)");saveChoice({index,false,{},{}});}
        catch(...){try{s.mode(before);}catch(...){}throw;}
        refresh(s);
    }
    std::array<int,3> custom(const std::array<int,3>& cpu,const PowerOptions& options){
        std::lock_guard lock(gate);require();legion::Session s;refresh(s,true,true);validate(cpu,options);
        const int beforeMode=s.mode();std::map<std::uint32_t,int> before,desired;
        for(int i=0;i<3;++i){before[lenovo::cpuIds[i]]=s.feature(lenovo::cpuIds[i]);desired[lenovo::cpuIds[i]]=cpu[i];}
        PowerOptions applied;
        for(const auto& o:lenovo::optionIds){const int old=s.feature(o.id);before[o.id]=old;
            const auto requested=options.find(o.key);desired[o.id]=requested==options.end()?old:requested->second;}
        bool started=false;
        try{
            if(beforeMode!=255){s.mode(255);started=true;if(s.mode()!=255)throw std::runtime_error("Lenovo: Custom requires firmware-qualified external supply");}
            for(const auto& [id,value]:desired){started=true;s.feature(id,value);}
            // KWCN54WW: offset = 100 - request; the offset==100 fallback is
            // for request zero, not 100°C. Always confirm the requested value.
            for(const auto& [id,value]:desired)if(s.feature(id)!=value)throw std::runtime_error("Lenovo: power option readback mismatch");
            if(s.mode()!=255)throw std::runtime_error("Lenovo: mode changed during Custom application");
            for(const auto& o:lenovo::optionIds)applied[o.key]=s.feature(o.id);
            if(beforeMode!=255)synchronizeOwnedFanMap();saveChoice({0,true,cpu,applied});
        }catch(...){if(started){try{for(const auto& [id,value]:before)s.feature(id,value);if(beforeMode!=255)s.mode(beforeMode);}catch(...){logMessage("lenovo_wmi","Power rollback could not be confirmed; fresh observation required");}}throw;}
        refresh(s);return cpu;
    }
    lenovo::FanBounds bounds()const{lenovo::FanBounds result;
        for(int ch=0;ch<2;++ch){const int sensor=ch?5:4;const lenovo::FanRow* row{};
            for(const auto& f:fanMetadata)if(f.fan==ch+1&&f.sensor==sensor){if(!row||f.mode==255)row=&f;}
            if(!row||row->rpm.size()!=10)throw std::runtime_error("Lenovo: exact WMI CPU/GPU RPM ladders required");result.minimumRpm[ch]=row->minimumRpm;result.maximumRpm[ch]=row->maximumRpm;}
        return result;
    }
    void ensureDraft(){if(draftLoaded)return;FanCurveProfiles p;std::string error;auto path=portableSettingsPath(std::string(gmFanCurveFile(lenovo::profileId)));
        if(!std::filesystem::exists(settingsReadPath(path)))path=portableSettingsPath("fan-curves.csv");
        if(!loadFanCurveProfiles(path,p,error))throw std::runtime_error("Lenovo: shared Manual draft unavailable: "+error);
        const bool own=path.filename()==gmFanCurveFile(lenovo::profileId);for(int ch=0;ch<2;++ch)draft[ch]=own?p.curves[0][ch]:p.ecManualCurve;
        // CPU=GPU is explicit owner intent. Preserve unused16-cell storage.
        for(int i=0;i<lenovo::userPoints;++i)draft[1][i]=draft[0][i];draftLoaded=true;
    }
    NativeFixedFanState read()override;
    void setMaximum()override;
    void setFixed(const std::array<int,2>&)override;
    NativeFanAutoResult setAutomatic()override;
    void setCurves(const NativeFanCurves&)override;
    void saveCurveDraft(const NativeFanCurves&)override;
};
NativeFixedFanState Controller::read(){
    std::lock_guard lock(gate);require();legion::Session s;refresh(s,false);const auto b=bounds();ensureDraft();legion::FanRam ram;const auto actual=readRam(ram);ram.finish();
    const bool fullSpeed=lenovo::readFullSpeedFlag(s);
    if(fullSpeed!=(actual.fullSpeed!=0))throw std::runtime_error("Lenovo: Full Speed changed during FAN snapshot");
    if(snapshot.cpuCelsius&&std::abs(*snapshot.cpuCelsius-actual.cpuRaw)>3)throw std::runtime_error("Lenovo: WMI/EC sensor alias not confirmed");
    auto lease=loadLease();if(lease&&lease->machine!=machineKey())lease.reset();
    const bool ours=lease&&actual.overrideFlags==0&&((actual.flags&3)==1)&&actual.table==lease->after&&actual.count==lease->afterCount&&(!actual.custom||actual.map==lease->afterMap);
    const bool recover=lease&&!lease->committed&&actual.overrideFlags==0&&lenovo::transitionPrefix(lease->returning?lease->after:lease->before,lease->returning?lease->before:lease->after,actual.table);
    NativeFixedFanState out;out.source="Lenovo · EC IT5507 + WMI";out.sharedCurve=true;out.editablePointCounts={10,10};out.minimumSupported=true;out.targetRpm=true;out.targetRamped=true;
    // Diagnostic reads have their own I2EC lease: an unsupported/failed alias
    // must never invalidate the already completed FAN observation or write RAM.
    if(loggingEnabled()){
        const auto now=GetTickCount64();const auto period=powerDiagnosticBurstActive()?1000ULL:10000ULL;
        if(!powerEcDiagnostic||now<powerEcReadAt||now-powerEcReadAt>=period){
            powerEcReadAt=now;LenovoPowerEcDiagnostic value;value.startedAtMs=now;
            try{
                if(deviceOperations().passiveEcReadsPaused()||paused||stopped)throw std::runtime_error("EC observations paused");
                legion::FanRam diagnostic;
                constexpr auto& addresses=lenovo::powerEcDiagnosticAddresses;
                lenovo::PowerEcDiagnosticBytes first{},second{};
                for(std::size_t i=0;i<addresses.size();++i){if(deviceOperations().passiveEcReadsPaused()||paused||stopped)throw std::runtime_error("EC observations paused");first[i]=diagnostic.read(addresses[i]);}
                for(std::size_t i=0;i<addresses.size();++i){if(deviceOperations().passiveEcReadsPaused()||paused||stopped)throw std::runtime_error("EC observations paused");second[i]=diagnostic.read(addresses[i]);}
                diagnostic.finish();
                auto observed=lenovo::decodePowerEcDiagnostic(first,second,actual.custom);
                observed.startedAtMs=value.startedAtMs;value=std::move(observed);
            }catch(const std::exception& error){value.status=std::string("I2EC diagnostic unavailable: ")+error.what();}
            value.sampledAtMs=GetTickCount64();powerEcDiagnostic=std::move(value);
        }
        out.lenovoPowerEcDiagnostic=powerEcDiagnostic;
    }else{powerEcDiagnostic.reset();powerEcReadAt=0;}
    const bool knownFactory=lenovo::factoryPacketKnown(actual.selected,actual.count,actual.table,actual.sensors&63,actual.fans);
    const bool mapValid=!actual.custom||lenovo::activeLevelMapValid(actual.map);
    // Only FNST confirms Max. A legacy kind2 receipt is an old RAM Manual
    // packet, never proof that the native Full Speed flag is enabled.
    out.mode=fullSpeed?2:ours?(lease->kind==2?1:lease->kind):0;out.manual=fullSpeed||ours;
    out.writable=mapValid&&actual.overrideFlags==0&&(actual.sensors&3)==3&&(((actual.flags&3)==0&&knownFactory)||ours||recover);out.needsRestore=recover||(!out.writable);
    // Firmware exposes PWM mirror bytes, but the PWM-period register is not
    // accessible in this SRAM snapshot. Keep RPM% for the RPM curve only;
    // never publish it as a measured PWM duty cycle.
    out.actualPercentKnown=false;out.rawPwmSampledAtMs=GetTickCount64();
    int governing=0;for(int i=0;i<6;++i)if((actual.sensors&(1<<i))&&(!(actual.sensors&(1<<governing))||actual.stages[i]>actual.stages[governing]))governing=i;
    NativeFanCurveState curves;curves.draft=draft;curves.actualPointCounts={actual.count,actual.count};curves.appliedCurve=ours&&!fullSpeed&&lease->committed&&lease->kind==1&&lease->requested==draft;
    for(int ch=0;ch<2;++ch){out.observedRpm[ch]=actual.rpm[ch];out.rawPwm[ch]=actual.rawPwm[ch];out.actualPercent[ch]=std::clamp(actual.rpm[ch]*100/b.maximumRpm[ch],0,100);out.targetPercent[ch]=fullSpeed?100:std::clamp(actual.target[ch]*100/b.maximumRpm[ch],0,100);out.minimumPercent[ch]=(b.minimumRpm[ch]*100+b.maximumRpm[ch]-1)/b.maximumRpm[ch];
        curves.temperatures[ch]=actual.temperatures[governing];for(auto& p:curves.actual[ch])p={255,100};
        for(int i=0;i<std::min(16,actual.count);++i)curves.actual[ch][i]={actual.table[i*16+4+governing*2],int(actual.table[i*16+ch])*10000/b.maximumRpm[ch]};}
    out.curves=curves;out.status=fullSpeed?"Lenovo: native Full Speed flag confirmed; actual RPM are telemetry":ours?"Lenovo: shared autonomous Manual EC curve confirmed":"Lenovo: firmware Default table read";
    if(fullSpeed&&!actual.custom)out.status+="; OEM Full Speed is gated by Custom mode; power mode retained";
    if(out.needsRestore)out.status="Lenovo: foreign/interrupted FAN transaction; explicit recovery required";
    if(!out.manual&&!out.needsRestore){NativeDefaultFans fans;const auto now=GetTickCount64();const char* names[]{"EC CPU","EC GPU (filtered)","EC CPU socket","EC input4","EC input5","EC input6"};
        int selected=0;for(int i=0;i<6;++i)if((actual.sensors&(1<<i))&&(!(actual.sensors&(1<<selected))||actual.stages[i]>actual.stages[selected]))selected=i;
        for(int ch=0;ch<2;++ch){auto& f=fans.channels[ch];f.rpm=true;f.profile="Default EC";f.temperatureSource=names[selected];f.temperatureCelsius=actual.temperatures[selected];f.temperatureAddress=static_cast<std::uint16_t>(0xdffa+selected);f.table=lenovo::tableAddress;f.sampledAtMs=now;f.requestedRpm=actual.target[ch];f.actualRpm=actual.rpm[ch];f.stage=actual.stage;f.curveSource="Active EC SRAM table; not an app preset";f.requestSource="EC target 81FD4/81FD5";
            for(int sensor=0;sensor<6;++sensor){NativeDefaultFanInput input;input.source=names[sensor];input.temperature=actual.temperatures[sensor];input.stage=actual.stages[sensor];input.address=static_cast<std::uint16_t>(0xdffa+sensor);
                double left=20;for(int i=0;i<actual.count;++i){const auto at=i*16;const int up=actual.table[at+4+sensor*2],down=actual.table[at+5+sensor*2];const double rpm=actual.table[at+ch]*100;
                    const double right=std::clamp(up,20,100);input.rising.push_back({left,rpm});input.rising.push_back({std::max(left,right),rpm});input.falling.push_back({double(std::clamp(down,20,100)),rpm});left=std::max(left,right);if(up>=100)break;}
                if(sensor==selected)f.points=input.rising;f.inputs.push_back(std::move(input));}
            fans.actualPercent[ch]=out.actualPercent[ch];}
        out.defaultFans={NativeDefaultFanObservation::Available,std::move(fans)};
    }else out.defaultFans.observation=NativeDefaultFanObservation::Inactive;
    return out;
}
void Controller::saveCurveDraft(const NativeFanCurves& c){std::lock_guard lock(gate);static_cast<void>(lenovo::curvePoints(c));draft=c;draftLoaded=true;}
void Controller::setMaximum(){
    std::lock_guard lock(gate);require();legion::Session session;
    // Like Wujie Fan Boost: ONE native flag, independent of Manual storage.
    // Never call setFixed/setCurves, enter Custom, edit its limits or create a
    // RAM-table lease here. WMAE owns the masked FNST write under its ACPI lock.
    lenovo::setFullSpeedFlag(session,true);
    require();
    logMessage("lenovo_fans","Native Full Speed flag enabled; Manual tables/draft, power mode and presets retained; RPM not asserted");
}
void Controller::setCurves(const NativeFanCurves& c){
    std::lock_guard lock(gate);require();const int points=lenovo::curvePoints(c);legion::Session s;refresh(s,false,true);const auto b=bounds();
    const auto machine=machineKey();auto oldLease=loadLease();if(oldLease&&oldLease->machine!=machine)oldLease.reset();
    legion::FanRam ram;const auto before=readRam(ram);
    if(lenovo::readFullSpeedFlag(s)!=(before.fullSpeed!=0))throw std::runtime_error("Lenovo: Full Speed changed before Manual");
    if(!snapshot.cpuCelsius||std::abs(*snapshot.cpuCelsius-before.cpuRaw)>3)throw std::runtime_error("Lenovo: WMI/EC SRAM alias not confirmed before FAN write");
    const bool ours=oldLease&&oldLease->committed&&(before.flags&3)==1&&before.table==oldLease->after&&before.count==oldLease->afterCount;
    if((before.custom&&!lenovo::activeLevelMapValid(before.map))||before.overrideFlags!=0||(before.sensors&3)!=3||((before.flags&3)!=0&&!ours)||(!ours&&!lenovo::factoryPacketKnown(before.selected,before.count,before.table,before.sensors&63,before.fans)))throw std::runtime_error("Lenovo: refusing unreviewed/foreign/manual FAN state");
    bool identityMap=true;for(int i=0;i<10;++i)identityMap=identityMap&&before.map[i]==i+1;
    if(before.fullSpeed&&ours&&oldLease->committed&&oldLease->kind==1&&oldLease->requested==c&&
       (!before.custom||identityMap)){
        // Max never changed this applied Manual packet. Returning to the same
        // curve only clears FNST; no update-hold, table, stage or map write.
        lenovo::setFullSpeedFlag(s,false);draft=c;draftLoaded=true;return;
    }
    FanLease lease;lease.machine=machine;lease.before=ours?oldLease->before:before.table;lease.beforeCount=ours?oldLease->beforeCount:before.count;lease.flags=ours?oldLease->flags:static_cast<std::uint8_t>(before.flags);
    lease.beforeMap=ours?oldLease->beforeMap:before.map;lease.afterMap=before.map;if(before.custom)for(int i=0;i<10;++i)lease.afterMap[i]=i+1;
    lease.after=lenovo::encodeSharedCurve(c,lease.before,lease.beforeCount,b);lease.afterCount=points+1;lease.requested=c;saveLease(lease);
    bool tableStarted=false;
    try{if(before.fullSpeed)lenovo::setFullSpeedFlag(s,false);
        require();tableStarted=true;ram.write(lenovo::tableFlags,static_cast<std::uint8_t>(before.flags|3));
        for(std::size_t i=0;i<lease.after.size();++i)if(before.table[i]!=lease.after[i])ram.write(static_cast<std::uint16_t>(lenovo::tableAddress+i),lease.after[i]);
        if(before.custom)for(int i=0;i<10;++i)ram.write(static_cast<std::uint16_t>(0xc5f0+i),static_cast<std::uint8_t>(lease.afterMap[i]));
        ram.write(lenovo::tableCount,static_cast<std::uint8_t>(lease.afterCount));resetStages(ram);
        if(ram.table()!=lease.after)throw std::runtime_error("Lenovo: complete FAN packet not confirmed");
        ram.write(lenovo::tableFlags,static_cast<std::uint8_t>((ram.read(lenovo::tableFlags)&0xfc)|1));ram.finish();lease.committed=true;saveLease(lease);draft=c;draftLoaded=true;
    }catch(...){try{ram.finish();}catch(...){}
        bool recovered=false;
        try{if(!tableStarted){if(oldLease)saveLease(*oldLease);else clearLease();recovered=true;}
        else{legion::FanRam recovery;const auto live=readRam(recovery);
            if(live.overrideFlags||live.fullSpeed||!lenovo::transitionPrefix(before.table,lease.after,live.table))throw std::runtime_error("Lenovo: FAN ownership changed during rollback");
            recovery.write(lenovo::tableFlags,static_cast<std::uint8_t>(live.flags|3));
            for(std::size_t i=0;i<before.table.size();++i)recovery.write(static_cast<std::uint16_t>(lenovo::tableAddress+i),before.table[i]);
            for(int i=0;i<10;++i)recovery.write(static_cast<std::uint16_t>(0xc5f0+i),static_cast<std::uint8_t>(before.map[i]));
            recovery.write(lenovo::tableCount,static_cast<std::uint8_t>(before.count));resetStages(recovery);
            recovery.write(lenovo::tableFlags,static_cast<std::uint8_t>((recovery.read(lenovo::tableFlags)&0xfc)|(before.flags&3)));recovery.finish();if(oldLease)saveLease(*oldLease);else clearLease();recovered=true;}
        }catch(...){logMessage("lenovo_fans","FAN prefix journal retained; recovery not confirmed");}
        if(recovered&&before.fullSpeed)try{require();lenovo::setFullSpeedFlag(s,true);}catch(...){logMessage("lenovo_fans","Full Speed rollback not confirmed");}
        throw;}
}
void Controller::setFixed(const std::array<int,2>& target){
    std::lock_guard lock(gate);require();legion::Session session;refresh(session,false,true);const auto b=bounds();
    const std::array<int,2> minimum{(b.minimumRpm[0]*100+b.maximumRpm[0]-1)/b.maximumRpm[0],(b.minimumRpm[1]*100+b.maximumRpm[1]-1)/b.maximumRpm[1]};
    const bool minimumMode=target==minimum;if(target[0]!=target[1]&&!minimumMode)throw std::runtime_error("Lenovo: independent FAN mode is not enabled");ensureDraft();auto c=draft;
    const int points=lenovo::curvePoints(c);for(int ch=0;ch<2;++ch)for(int i=0;i<points;++i)c[ch][i].duty=minimumMode?fanInput::percentMinimum:fanInput::percent(target[0]);
    const auto savedDraft=draft;setCurves(c);draft=savedDraft;if(auto l=loadLease()){l->kind=minimumMode?3:1;l->requested=minimumMode?savedDraft:c;saveLease(*l);}
}
NativeFanAutoResult Controller::setAutomatic(){
    std::lock_guard lock(gate);require();auto lease=loadLease();legion::Session session;legion::FanRam ram;const auto actual=readRam(ram);
    if(lenovo::readFullSpeedFlag(session)!=(actual.fullSpeed!=0))throw std::runtime_error("Lenovo: Full Speed changed before Default");
    if(lease&&lease->machine==machineKey()&&!(actual.flags&3)&&!actual.overrideFlags&&lenovo::factoryPacketKnown(actual.selected,actual.count,actual.table,actual.sensors&63,actual.fans)){
        ram.finish();lenovo::setFullSpeedFlag(session,false);clearLease();return actual.fullSpeed?NativeFanAutoResult::Applied:NativeFanAutoResult::AlreadyAuto;
    }
    if(!lease){if((actual.flags&3)||actual.overrideFlags||!lenovo::factoryPacketKnown(actual.selected,actual.count,actual.table,actual.sensors&63,actual.fans))return NativeFanAutoResult::OwnershipLost;ram.finish();lenovo::setFullSpeedFlag(session,false);return actual.fullSpeed?NativeFanAutoResult::Applied:NativeFanAutoResult::AlreadyAuto;}
    if(lease->machine!=machineKey()||(actual.custom&&!lenovo::activeLevelMapValid(lease->beforeMap))||actual.overrideFlags||!lenovo::factoryPacketKnown(actual.selected,lease->beforeCount,lease->before,actual.sensors&63,actual.fans)||!(actual.table==lease->after||(!lease->committed&&lenovo::transitionPrefix(lease->returning?lease->after:lease->before,lease->returning?lease->before:lease->after,actual.table))))return NativeFanAutoResult::OwnershipLost;
    if(lease->committed&&actual.custom&&actual.map!=lease->afterMap)return NativeFanAutoResult::OwnershipLost;
    lenovo::setFullSpeedFlag(session,false);
    lease->committed=false;lease->returning=true;saveLease(*lease);
    ram.write(lenovo::tableFlags,static_cast<std::uint8_t>(actual.flags|3));
    for(std::size_t i=0;i<lease->before.size();++i)ram.write(static_cast<std::uint16_t>(lenovo::tableAddress+i),lease->before[i]);
    ram.write(lenovo::tableCount,static_cast<std::uint8_t>(lease->beforeCount));for(int i=0;i<10;++i)ram.write(static_cast<std::uint16_t>(0xc5f0+i),static_cast<std::uint8_t>(lease->beforeMap[i]));resetStages(ram);
    if(ram.table()!=lease->before||ram.read(lenovo::tableCount)!=lease->beforeCount)throw std::runtime_error("Lenovo: Default FAN rollback packet not confirmed");
    ram.write(0xdff3,static_cast<std::uint8_t>(ram.read(0xdff3)|0x80));ram.write(lenovo::tableFlags,static_cast<std::uint8_t>(ram.read(lenovo::tableFlags)&0xfc));ram.finish();clearLease();
    return NativeFanAutoResult::Applied;
}
std::mutex instanceGate;std::shared_ptr<Controller> instance;
std::shared_ptr<Controller> controller(){if(!admitted())return {};std::lock_guard lock(instanceGate);if(!instance)instance=std::make_shared<Controller>();return instance;}
class Typed final:public LaptopControl {
    std::shared_ptr<Controller> c_;
public:
    explicit Typed(std::shared_ptr<Controller> c):c_(std::move(c)){}
    std::string_view chargePolicyId()const noexcept override{return "lenovo-legion-kwcn-energy";}
    void withChargePolicyTransaction(const std::function<void()>& action)override{std::lock_guard lock(c_->gate);c_->require();action();}
    std::string_view customPowerPolicyId()const noexcept override{return "lenovo-legion-kwcn-wmi";}
    std::optional<CpuPowerChoice> savedCpuPowerChoice()const override{return loadChoice();}
    LaptopControlSnapshot read()override{std::lock_guard lock(c_->gate);legion::Session s;c_->refresh(s);return c_->snapshot;}
    LaptopControlSnapshot readTelemetry()override{std::lock_guard lock(c_->gate);legion::Session s;c_->refresh(s,false);auto observed=c_->snapshot;c_->populateEnergy(observed);
        observed.customCpuPowerLimits.reset();observed.customPowerOptions.clear();return observed;}
    std::array<int,3> customCpuPowerMaximum()const override{std::lock_guard lock(c_->gate);std::array<int,3> values;for(int i=0;i<3;++i){if(!c_->snapshot.customCpuPowerSettings[i])throw std::runtime_error("Lenovo: power ranges not read yet");values[i]=c_->snapshot.customCpuPowerSettings[i]->maximum;}return values;}
    std::array<int,3> customCpuPowerMinimum()const override{std::lock_guard lock(c_->gate);std::array<int,3> values;for(int i=0;i<3;++i){if(!c_->snapshot.customCpuPowerSettings[i])throw std::runtime_error("Lenovo: power ranges not read yet");values[i]=c_->snapshot.customCpuPowerSettings[i]->minimum;}return values;}
    std::array<int,3> customCpuPowerRecipe(int slot)const override{return c_->recipe(slot);}
    PowerOptions customPowerOptionRecipe(int slot)const override{return c_->optionRecipe(slot);}
    void validateCustomPower(const std::array<int,3>& cpu,const PowerOptions& o)const override{std::lock_guard lock(c_->gate);c_->validate(cpu,o);}
    std::array<int,3> setCustomCpuPower(const std::array<int,3>& cpu)override{return c_->custom(cpu,{});}
    std::array<int,3> setCustomPower(const std::array<int,3>& cpu,const PowerOptions& o)override{return c_->custom(cpu,o);}
    void setStockCpuProfile(int p)override{c_->stock(p);}
    void setChargeThresholds(int,int)override{throw std::runtime_error("Lenovo battery control is not connected");}
    void setChargeMode(int value)override{c_->energyMode(value);}
    void setKeyboardLevel(int value)override{c_->energyLight(value);}
    void setFnLock(bool)override{throw std::runtime_error("Lenovo Fn lock is not connected");}
};
}
std::unique_ptr<LaptopControl> createLegionControl(){auto c=controller();return c?std::make_unique<Typed>(std::move(c)):nullptr;}
bool legionHostSupported(){return admitted();}
bool legionFanWritesAllowed(){std::lock_guard lock(instanceGate);return instance&&!instance->paused&&!instance->stopped;}
std::shared_ptr<NativeFixedFans> legionFanController(){return controller();}
EcBatteryReading readLegionEcBatteryOnly(){
    EcBatteryReading result;
    if(deviceProfile(readBoardDescriptor()).id!=lenovo::profileId||wcsstr(GetCommandLineW(),L"--no-ec"))return result;
    result.supported=true;result.profileId=std::string(lenovo::profileId);
    auto owner=controller();if(!owner){result.status="Lenovo: BIOS/устройство EC не подтверждено";return result;}
    std::lock_guard lock(owner->gate);const auto now=batteryMonotonicMs();
    if(deviceOperations().passiveEcReadsPaused()||owner->paused||owner->stopped){result.status="Опрос EC временно приостановлен";return result;}
    if(owner->batteryCache.coherent&&now>=owner->batterySampleAt&&now-owner->batterySampleAt<800)return owner->batteryCache;
    try{
        owner->require();legion::FanRam ram;
        const auto byte=[&](std::uint16_t address){if(deviceOperations().passiveEcReadsPaused()||owner->paused||owner->stopped)throw std::runtime_error("Lenovo: чтение батареи приостановлено");return ram.read(address);};
        const auto powerState=byte(0xc200);if(powerState!=0x10)throw std::runtime_error("Lenovo: EC не в режиме S0");
        const auto present=byte(0xc4be);if(present==0xff)throw std::runtime_error("Lenovo: недействительный статус батареи EC");auto& b=result.battery;
        b.deviceId="EC/Lenovo-KWCN/BAT0";b.source="EC · Lenovo IT5507 / BMS cache";b.present=(present&1)!=0;
        if(!*b.present){if((byte(0xc4be)&1)!=0)throw std::runtime_error("Lenovo: состояние батареи изменилось");ram.finish();result.coherent=true;result.status="EC: батарея отсутствует";result.sampledAtMs=now;owner->batteryCache=result;owner->batterySampleAt=now;return result;}
        const auto stableWord=[&](std::uint16_t address)->std::optional<std::uint16_t>{
            const auto sample=[&]{const auto lo=byte(address),hi=byte(static_cast<std::uint16_t>(address+1));return static_cast<std::uint16_t>(lo|(unsigned(hi)<<8));};
            for(int i=0;i<3;++i){const auto a=sample(),b=sample();if(a==b)return b;}return {};
        };
        const auto addWord=[&](std::uint16_t address,const char* name){const auto value=stableWord(address);EcBatteryRegisterReading f;
            f.address=address;f.width=2;f.name=name;f.confirmed=address!=0xc250;f.sampledTelemetry=true;f.signedValue=address==0xc4d0||address==0xc4d2||address==0xc250;f.value=value;f.status=value?"stable EC cache; BMS freshness unknown":"unstable";result.registers.push_back(std::move(f));return value;};
        const auto addByte=[&](std::uint16_t address,const char* name,std::uint8_t value){EcBatteryRegisterReading f;
            f.address=address;f.name=name;f.value=value;f.sampledTelemetry=address==0xc252;f.status="raw EC byte; snapshot not atomic";result.registers.push_back(std::move(f));};
        const auto mode=stableWord(0xc22c),temperature=addWord(0xc214,"sbs_temperature_tenths_kelvin"),date=addWord(0xc4f4,"sbs_manufacture_date");
        const auto voltage=addWord(0xc4c6,"sbs_voltage_mv"),current=addWord(0xc4d0,"sbs_current_signed_ma"),cycles=addWord(0xc212,"sbs_cycle_count");
        addWord(0xc4d2,"bios_b1ac_signed_raw");addWord(0xc250,"ec_aux_0250_signed_raw");
        const auto design=addWord(0xc4ca,"sbs_design_capacity"),full=addWord(0xc4cc,"sbs_full_capacity"),remaining=addWord(0xc28e,"sbs_remaining_capacity");
        const auto requestCurrent=addWord(0xc4d8,"sbs_requested_charge_current_ma"),requestVoltage=addWord(0xc4f6,"sbs_requested_charge_voltage_mv");
        const auto soc=byte(0xc4ce),state=byte(0xc4c1),flags=byte(0xc40a),light=byte(0xc48c);
        addByte(0xc4c1,"bios_b1st_status",state);addByte(0xc488,"bios_fbfg_bit5_and_fbdc_bit4",byte(0xc488));
        addByte(0xc252,"ec_current_mux_counter_raw",byte(0xc252));
        const auto muxFlags=byte(0xc4a3);addByte(0xc4a3,"ec_current_mux_flags_raw",muxFlags);
        addByte(0xc4be,"ec_battery_presence_flags",present);addByte(0xc200,"ec_power_state",powerState);
        const auto flagsAgain=byte(0xc40a),lightAgain=byte(0xc48c);
        if((byte(0xc4be)&1)==0||byte(0xc200)!=0x10)throw std::runtime_error("Lenovo: батарея или состояние питания изменились");
        ram.finish();
        // Cache only the optional adapter diagnosis. A mux change invalidates
        // it immediately at the next core poll; failures never clear battery.
        const auto adapterNow=batteryMonotonicMs();
        const bool adapterDue=!owner->adapterMuxCache||*owner->adapterMuxCache!=muxFlags||adapterNow<owner->adapterAttemptAt||adapterNow-owner->adapterAttemptAt>=10000;
        if(adapterDue){
          owner->adapterAttemptAt=adapterNow;owner->adapterMuxCache=muxFlags;
          owner->adapterWattCache.reset();owner->adapterCandidateAtMs=0;owner->adapterRegisterCache.clear();
          try{
            if(deviceOperations().passiveEcReadsPaused()||owner->paused||owner->stopped)throw std::runtime_error("Lenovo: чтение адаптера приостановлено");
            legion::FanRam candidate;
            const auto cbyte=[&](std::uint16_t address){if(deviceOperations().passiveEcReadsPaused()||owner->paused||owner->stopped)throw std::runtime_error("Lenovo: чтение адаптера приостановлено");return candidate.read(address);};
            const auto stableByte=[&](std::uint16_t address)->std::optional<std::uint8_t>{for(int i=0;i<3;++i){const auto a=cbyte(address),b=cbyte(address);if(a==b)return b;}return {};};
            const auto word=[&]{const auto lo=cbyte(0xc450),hi=cbyte(0xc451);return static_cast<std::uint16_t>(lo|(unsigned(hi)<<8));};
            std::optional<std::uint16_t> adapterRaw;for(int i=0;i<3;++i){const auto a=word(),b=word();if(a==b){adapterRaw=b;break;}}
            const auto adapterClass=stableByte(0xc22f),adapterGate=stableByte(0xc2a7),adapterFlags=stableByte(0xc4a3);
            candidate.finish();
            const auto adapterSampleAt=batteryMonotonicMs();
            const auto addCandidate=[&](std::uint16_t address,std::uint8_t width,const char* name,std::optional<std::uint16_t> value){EcBatteryRegisterReading f;
                f.address=address;f.width=width;f.name=name;f.value=value;f.sampledTelemetry=true;f.status=value?"stable raw adapter candidate; snapshot not atomic":"unstable";owner->adapterRegisterCache.push_back(std::move(f));};
            addCandidate(0xc450,2,"adapter_rating_candidate_raw",adapterRaw);
            addCandidate(0xc22f,1,"adapter_class_candidate_raw",adapterClass?std::optional<std::uint16_t>(*adapterClass):std::nullopt);
            addCandidate(0xc2a7,1,"adapter_gate_candidate_raw",adapterGate?std::optional<std::uint16_t>(*adapterGate):std::nullopt);
            // C4A3 keeps the original journal name; require agreement with the
            // independently stable candidate before decoding its table branch.
            if(const auto rating=lenovo::adapterWattCandidate(adapterRaw,adapterClass,adapterGate,adapterFlags,muxFlags)){
                owner->adapterWattCache=*rating;owner->adapterCandidateAtMs=adapterSampleAt;
            }
          }catch(const std::exception& error){owner->adapterRegisterCache.clear();EcBatteryRegisterReading f;f.address=0xc450;f.width=2;f.name="adapter_rating_candidate_raw";f.sampledTelemetry=true;f.status=std::string("candidate unavailable: ")+error.what();owner->adapterRegisterCache.push_back(std::move(f));}
        }
        result.adapterWattCandidate=owner->adapterWattCache;result.adapterCandidateAtMs=owner->adapterCandidateAtMs;
        result.registers.insert(result.registers.end(),owner->adapterRegisterCache.begin(),owner->adapterRegisterCache.end());
        if(soc<=100)b.percent=soc;
        switch(state&7){case 0:b.flow=BatteryFlow::Idle;break;case 1:b.flow=BatteryFlow::Discharging;break;case 2:b.flow=BatteryFlow::Charging;break;default:break;}
        if(voltage&&*voltage>=5000&&*voltage<=25000)b.volts=*voltage/1000.;
        if(const auto value=lenovoBatteryCurrent(current))b.milliamps=*value;
        if(b.volts&&b.milliamps)b.watts=(*b.volts)*(*b.milliamps)/1000.;
        if(temperature&&*temperature&&*temperature!=0xffff){b.celsius=batteryTemperatureCelsius(*temperature);if(b.celsius){b.temperatureRaw=*temperature;b.temperatureSource="EC · Lenovo IT5507 · SBS 0x08";b.temperatureStatus="EC cache; BMS freshness not exposed";}}
        if(cycles&&*cycles!=0xffff)b.cycles=*cycles;
        if(date&&*date&&*date!=0xffff){const std::chrono::year_month_day d{std::chrono::year{1980+int(*date>>9)},std::chrono::month{unsigned((*date>>5)&15)},std::chrono::day{unsigned(*date&31)}};
            if(d.ok()){auto text=std::format("{:04}-{:02}-{:02}",int(d.year()),unsigned(d.month()),unsigned(d.day()));if(batteryAgeMonthsLocal(text))b.manufactured=std::move(text);}}
        if(mode&&*mode!=0xffff){const bool energy=(*mode&0x8000)!=0;const double scale=energy?10.:1.;b.capacityUnit=energy?"mWh":"mAh";
            if(design&&*design&&*design!=0xffff)b.designCapacity=*design*scale;
            if(full&&*full&&*full!=0xffff)b.fullCapacity=*full*scale;
            if(remaining&&*remaining!=0xffff)b.remainingCapacity=*remaining*scale;
            if(b.designCapacity&&b.fullCapacity&&*b.designCapacity>0){b.healthPercent=(*b.fullCapacity)*100./(*b.designCapacity);b.healthSource=b.source;b.healthAtMs=now;}}
        if(requestCurrent&&*requestCurrent!=0xffff&&*requestCurrent<=30000)result.requestedChargeMilliamps=*requestCurrent;
        if(requestVoltage&&(*requestVoltage==0||(*requestVoltage>=5000&&*requestVoltage<=25000)))result.requestedChargeVolts=*requestVoltage/1000.;
        if(flags!=0xff&&flags==flagsAgain)result.conservationMode=(flags&0x10)!=0;
        if(light!=0xff&&light==lightAgain&&(light&0x80))result.rapidChargeMode=(light&1)!=0;
        result.state=state;result.coherent=true;result.sampledAtMs=now;result.status="EC Lenovo · BMS cache; snapshot not atomic";
        b.powerStatus=result.status;stampBatteryReading(b,now);owner->batteryCache=result;owner->batterySampleAt=now;return result;
    }catch(const std::exception& error){result.status=error.what();return result;}
}
FanMaximumSample readLegionFans(){
    if(!admitted())throw std::runtime_error("Lenovo: read-only FAN identity not confirmed");
    legion::Session session;FanMaximumSample sample;
    for(int ch=0;ch<2;++ch){const int rpm=session.feature(ch?0x04030002:0x04030001);
        if(rpm<0||rpm>20000)throw std::runtime_error("Lenovo: invalid WMI FAN RPM");sample.rpm[ch]=rpm;}
    return sample;
}
FanDiagnosticSession prepareLegionDiagnostic(){
    auto c=controller();if(!c)throw std::runtime_error("Lenovo: diagnostic controller unavailable");
    auto operation=deviceOperations().tryBegin();if(!operation)throw std::runtime_error("Lenovo: FAN diagnostics busy");
    std::lock_guard lock(c->gate);const auto epoch=diagnosticFanEpoch();const auto before=c->read();const int powerMode=c->modeCode;
    if(before.needsRestore||!before.writable)throw std::runtime_error("Lenovo: foreign/interrupted FAN state; diagnostic writes refused");
    NativeFanCurves active{};if(before.mode==1){const auto receipt=loadLease();if(!receipt||!receipt->committed)throw std::runtime_error("Lenovo: active Manual receipt missing");active=receipt->requested;}
    const auto editor=c->draft;
    FanWriteRequest automatic{FanResourceOperation::Default};automatic.origin=FanWriteOrigin::Restore;
    executeFanResource({nullptr,c.get(),nullptr,operation,true,lenovo::profileId},automatic).requireAccepted();
    FanDiagnosticSession session;session.previousMode=before.mode==1?"Manual CPU=GPU":before.mode==2?"Max":before.mode==3?"Min":"Default";
    const auto consumed=std::make_shared<std::atomic_bool>(false);
    session.restore=[c,epoch,powerMode,beforeMode=before.mode,active,editor,consumed]{
        FanDiagnosticRestore result;
        if(consumed->exchange(true)){result.status="Lenovo: diagnostic restore already consumed";return result;}
        if(diagnosticFanEpoch()!=epoch){result.superseded=true;result.status="Lenovo: newer FAN command/sleep superseded diagnostic restore";return result;}
        auto operation=deviceOperations().tryBegin();if(!operation){result.status="Lenovo: diagnostic restore busy";return result;}
        try{std::lock_guard lock(c->gate);const auto now=c->read();
            if(diagnosticFanEpoch()!=epoch||c->modeCode!=powerMode||now.manual||now.needsRestore||!now.writable){result.superseded=true;result.status="Lenovo: Default/power baseline changed; old FAN state not replayed";return result;}
            result.attempted=true;
            if(beforeMode!=0){FanWriteRequest request{beforeMode==1?FanResourceOperation::Manual:beforeMode==2?FanResourceOperation::Maximum:FanResourceOperation::Minimum};request.origin=FanWriteOrigin::Restore;
                if(beforeMode==1){FanCurveProfiles p;for(int ch=0;ch<2;++ch)p.setActivePointCount(ch,10);p.curves[0]=active;p.linked=true;request.profiles=p;}
                executeFanResource({nullptr,c.get(),nullptr,operation,true,lenovo::profileId},request).requireAccepted();
                c->draft=editor; // Restore the applied packet without discarding an unapplied editor draft.
            }
            result.confirmed=true;result.status="Lenovo: pre-dump FAN mode restored with fresh readback; power/settings unchanged";
        }catch(const std::exception& e){result.status=e.what();}
        return result;
    };
    return session;
}
FanMaximumResult measureLegionMaximum(std::stop_token token){
    FanMaximumResult skipped;skipped.source="Lenovo IT5507 · WMI Full Speed 0x04020000/FNST · WMI RPM";
    auto c=controller();if(!c){skipped.status="Lenovo: diagnostic FAN controller unavailable";return skipped;}
    auto operation=deviceOperations().tryBegin();if(!operation){skipped.status="Lenovo: Max probe busy";return skipped;}
    const auto epoch=diagnosticFanEpoch();bool begun{};
    const auto read=[&]{std::lock_guard lock(c->gate);if(diagnosticFanEpoch()!=epoch)throw std::runtime_error("Lenovo: Max probe superseded by command/sleep");
        const auto actual=c->read();if(actual.needsRestore||!actual.writable||(begun?(actual.mode!=2||!actual.manual):actual.mode!=0))throw std::runtime_error("Lenovo: Max ownership/baseline not confirmed");
        FanMaximumSample sample;sample.rpm=c->snapshot.fanRpm;for(int ch=0;ch<2;++ch)sample.percent[ch]=actual.actualPercent[ch];return sample;};
    return measureMaximumFans(skipped.source,[&]{FanWriteRequest request{FanResourceOperation::Maximum};request.origin=FanWriteOrigin::Restore;
        executeFanResource({nullptr,c.get(),nullptr,operation,true,lenovo::profileId},request).requireAccepted();begun=true;
    },read,[&]{
        std::lock_guard lock(c->gate);
        if(diagnosticFanEpoch()!=epoch)throw std::runtime_error("Lenovo: Max cleanup superseded; newer FAN command not overwritten");
        FanWriteRequest request{FanResourceOperation::Default};request.origin=FanWriteOrigin::Restore;
        executeFanResource({nullptr,c.get(),nullptr,operation,true,lenovo::profileId},request).requireAccepted();
    },token);
}
void requestLegionConfigurationRestore(const std::string& reason){if(auto c=controller()){std::lock_guard lock(c->gate);c->paused=false;if(!c->restore)c->restore=std::make_unique<ConfigurationRestore>(std::string(lenovo::profileId));c->restore->request(reason);}}
void suspendLegionControl(){std::lock_guard lock(instanceGate);if(instance){instance->paused=true;if(instance->restore)instance->restore->suspend();}}
void shutdownLegionControl(){std::shared_ptr<Controller> c;{std::lock_guard lock(instanceGate);c=std::move(instance);}if(c){c->stopped=true;c->restore.reset();}}
}
