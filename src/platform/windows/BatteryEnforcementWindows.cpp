#include "BatteryEnforcementWindows.h"
#include <algorithm>
#include <cstring>
#include <shlobj.h>

namespace mc::windows::batteryEnforcement {
std::string machineIdentity() {
    static const auto identity=[]()->std::string {
        constexpr DWORD rsmb=0x52534D42;
        const UINT size=GetSystemFirmwareTable(rsmb,0,nullptr,0);
        if(size<8||size>1024*1024)return {};
        std::vector<std::uint8_t> data(size);
        if(GetSystemFirmwareTable(rsmb,0,data.data(),size)!=size)return {};
        std::uint32_t length{};std::memcpy(&length,data.data()+4,sizeof(length));
        if(length>size-8)return {};
        const std::size_t end=8+length;
        for(std::size_t at=8;at+4<=end;){
            const auto formatted=data[at+1];
            if(formatted<4||formatted>end-at)return {};
            if(data[at]==1){
                if(formatted<24)return {};
                const auto first=data.begin()+at+8,last=first+16;
                if(std::all_of(first,last,[](auto b){return b==0;})||
                   std::all_of(first,last,[](auto b){return b==255;}))return {};
                // Raw UUID byte order is stable for this firmware/API. No OS
                // account name, portable path or preview model participates.
                constexpr char hex[]="0123456789abcdef";std::string value="smbios1-";
                for(auto p=first;p!=last;++p){value+=hex[*p>>4];value+=hex[*p&15];}
                return value;
            }
            if(data[at]==127)break;
            std::size_t next=at+formatted;
            while(next+1<end&&(data[next]||data[next+1]))++next;
            if(next+1>=end)return {};
            at=next+2;
        }
        return {};
    }();
    return identity;
}
std::filesystem::path stateDirectory() {
    const auto machine=machineIdentity();
    if(machine.empty())throw std::runtime_error("Battery ROM-ID ownership: SMBIOS machine UUID unavailable");
    PWSTR raw{};
    const auto result=SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw);
    if(FAILED(result))throw std::runtime_error("Battery ROM-ID ownership: LocalAppData unavailable");
    const std::filesystem::path root(raw);CoTaskMemFree(raw);
    return root/L"MagicControl"/L"DeviceState"/machine;
}
std::filesystem::path ownerPath(const std::filesystem::path& settings,const std::string& id) {
    const auto path=stateDirectory()/(id+"-battery-romid-owner.txt");
    auto portable=settings/(id+"-battery-romid-owner.txt");
    if(!validOwner(portable,id)&&settings.filename()=="settings")
        portable=settings.parent_path()/(id+"-battery-romid-owner.txt");
    // Only already machine-bound receipts may migrate automatically. A legacy
    // model-only receipt at ROM04 cannot establish ownership on this machine.
    if(!std::filesystem::exists(path)&&validOwner(portable,id)){
        std::filesystem::create_directories(path.parent_path());
        std::filesystem::copy_file(portable,path,std::filesystem::copy_options::skip_existing);
    }
    return path;
}
}
