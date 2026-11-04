#pragma once

#include <array>
#include <algorithm>
#include <atomic>
#include <string>
#include <string_view>
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Monitoring.h"
#include "../../protocols/uniwill/hydroc16g2/Diagnostics.h"
#include "Fonts.h"

namespace mc::gui::ui {
// Presentation metadata only. These descriptions never grant hardware access.
enum class LaptopSupport { Controls, Battery, Inventory, System, Monitoring };
struct SupportedLaptop {
    std::string_view id,name;
    LaptopSupport support;
    mc::ProcessorVendor previewCpuVendor{mc::ProcessorVendor::Unknown};
    std::string_view previewCpu{},previewGpu{};
};
inline constexpr std::array<SupportedLaptop,19> kSupportedLaptops{{
    {"wu-jie-15-xpro","MECHREVO · WUJIE X5SP4NAG",LaptopSupport::Controls},
    {"huawei-hlyl-wxx9","Honor · HLYL-WXX9",LaptopSupport::Controls},
    {"mechrevo-gm6ir0c","MECHREVO · JiguangX GM6IR0C",LaptopSupport::Controls,mc::ProcessorVendor::Intel,"Intel Core i7-14650HX","NVIDIA GeForce RTX 4060 Laptop GPU"},
    {"noname","Unknown",LaptopSupport::System},
    {mc::hydroc16g2::profileId,"Eluktronics · HYDROC-16 G2",LaptopSupport::Controls,mc::ProcessorVendor::Intel,"Intel Core Ultra 9 275HX","NVIDIA GeForce RTX 5090 Laptop GPU"},
    {"mechrevo-gm5hg0a","MECHREVO · Yilong15Pro GM5HG0A",LaptopSupport::Controls,mc::ProcessorVendor::Amd,"AMD Ryzen 7 8845H","NVIDIA GeForce RTX 4060 Laptop GPU"},
    {"machenike-l16","MACHENIKE · L16 S",LaptopSupport::Controls,mc::ProcessorVendor::Amd,"AMD Ryzen 9 7945HX","NVIDIA GeForce RTX 5070 Ti Laptop GPU"},
    {"mechrevo-gm6px7x","MECHREVO · GM6PX7X",LaptopSupport::Inventory,mc::ProcessorVendor::Intel,"Intel Core i9-13900HX","NVIDIA GeForce RTX 4070 Laptop GPU"},
    {"mechrevo-kuangshi-jiguang-series-x6xx55xw-b2","MECHREVO · KUANGSHI X6xx55xW-B2",LaptopSupport::Inventory,mc::ProcessorVendor::Intel,"Intel Core i5-14450HX","NVIDIA GeForce RTX 5060 Laptop GPU"},
    {"mechrevo-canglong-series-x6xr55xy","MECHREVO · CANGLONG X6xR55xY",LaptopSupport::Inventory,mc::ProcessorVendor::Amd,"AMD Ryzen 9 9955HX","NVIDIA GeForce RTX 5070 Ti Laptop GPU"},
    {"mechrevo-gm6bg0x","MECHREVO · GM6BG0X",LaptopSupport::Inventory,mc::ProcessorVendor::Amd,"AMD Ryzen 7 7435H","NVIDIA GeForce RTX 4060 Laptop GPU"},
    {"mechrevo-kuangshi-jiguang-series-x6xx57tw","MECHREVO · JIGUANG X6xx57TW",LaptopSupport::Inventory,mc::ProcessorVendor::Intel,"Intel Core i7-14650HX","NVIDIA GeForce RTX 5070 Ti Laptop GPU"},
    {"mechrevo-jiaolong-series-x6dr55xx-b2","MECHREVO · JIAOLONG X6DR55xx-B2",LaptopSupport::Inventory,mc::ProcessorVendor::Amd,"AMD Ryzen 9 8940HX","NVIDIA GeForce RTX 5070 Laptop GPU"},
    {"mechrevo-gx6hg0u-f","MECHREVO · Huan 16 Air GX6HG0U-F",LaptopSupport::Inventory,mc::ProcessorVendor::Amd,"AMD Ryzen 7 8745H","NVIDIA GeForce RTX 4060 Laptop GPU"},
    {"mechrevo-canglong-series-x8xr55xp-b2","MECHREVO · CANGLONG X8xR55xP-B2",LaptopSupport::Inventory,mc::ProcessorVendor::Amd,"AMD Ryzen 9 9955HX","NVIDIA GeForce RTX 5070 Laptop GPU"},
    {"mechrevo-yaoshi-series-x6ar55xy","MECHREVO · YAOSHI X6AR55xY",LaptopSupport::Inventory,mc::ProcessorVendor::Intel,"Intel Core Ultra 9 275HX","NVIDIA GeForce RTX 5070 Ti Laptop GPU"},
    {"mechrevo-gm6ix9b","MECHREVO · GM6IX9B",LaptopSupport::Inventory,mc::ProcessorVendor::Intel,"Intel Core i9-14900HX","NVIDIA GeForce RTX 4090 Laptop GPU"},
    {"minisforum-hpbsd","MINISFORUM · EliteMini HPBSD",LaptopSupport::Controls},
    {"lenovo-legion-kwcn","Lenovo · Legion KWCN",LaptopSupport::Controls}
}};
// Presentation order only: stable model indices still own preview/actions.
inline constexpr std::array<int,kSupportedLaptops.size()> kLaptopMenuOrder{0,2,5,6,1,4,17,18,7,8,9,10,11,12,13,14,15,16,3};
// Preserve the compact panel's pre-0.1.170 measured width. New model names and
// status columns belong only to the laptop list, not languages/credits/QR.
inline constexpr std::size_t kCompactMenuReferenceModels=7;
static_assert(kSupportedLaptops[0].id=="wu-jie-15-xpro"&&kSupportedLaptops[1].id=="huawei-hlyl-wxx9"&&
    kSupportedLaptops[2].id=="mechrevo-gm6ir0c"&&kSupportedLaptops[3].id=="noname"&&
    kSupportedLaptops[4].id==mc::hydroc16g2::profileId&&kSupportedLaptops[5].id=="mechrevo-gm5hg0a"&&
    kSupportedLaptops[6].id=="machenike-l16");
inline constexpr bool kLaptopCatalogueValid=[] {
    std::array<bool,kSupportedLaptops.size()> seen{};
    for(const int i:kLaptopMenuOrder){if(i<0||i>=static_cast<int>(seen.size())||seen[i])return false;seen[i]=true;}
    for(std::size_t i=0;i<seen.size();++i)for(std::size_t j=i+1;j<seen.size();++j)
        if(kSupportedLaptops[i].id==kSupportedLaptops[j].id)return false;
    return true;
}();
static_assert(kLaptopCatalogueValid);
inline const char* laptopSupportCaption(int index,bool linuxHost=false){
    if(index<0||index>=static_cast<int>(kSupportedLaptops.size()))return "Системные функции";
    switch(kSupportedLaptops[index].support){
    case LaptopSupport::Inventory:return "Только распознавание";
    case LaptopSupport::System:return "Системные функции";
    case LaptopSupport::Monitoring:return "Мониторинг";
    case LaptopSupport::Battery:return linuxHost?"EC не подключён":"Частично: батарея";
    case LaptopSupport::Controls:return linuxHost?(index==1?"Частично: sysfs":"EC не подключён"):"Управление с ограничениями";
    }
    return "Системные функции";
}
inline constexpr char kLaptopsCaption[]="Ноутбуки";
inline constexpr char kLaptopsForwardCaption[]="Ноутбуки  ▸";
inline constexpr char kLaptopsBackArrow[]="‹";
inline constexpr int kLaptopMenuBackItem=static_cast<int>(kSupportedLaptops.size());
inline constexpr char kLaptopPreviewNote[]="Другая модель — просмотр";
inline int laptopMenuRow(int model){
    const auto at=std::find(kLaptopMenuOrder.begin(),kLaptopMenuOrder.end(),model);
    return at==kLaptopMenuOrder.end()?-1:static_cast<int>(at-kLaptopMenuOrder.begin());
}
inline int nextLaptopMenuItem(int model,bool backward){
    const int count=static_cast<int>(kLaptopMenuOrder.size()),row=laptopMenuRow(model);
    return kLaptopMenuOrder[row<0?(backward?count-1:0):(row+(backward?count-1:1))%count];
}
// Heading/back is a real action before the model rows, for keyboard and mouse.
inline int nextLaptopMenuAction(int item,bool backward){
    if(item==kLaptopMenuBackItem)return backward?kLaptopMenuOrder.back():kLaptopMenuOrder.front();
    const int row=laptopMenuRow(item);
    if(row<0)return backward?kLaptopMenuOrder.back():kLaptopMenuBackItem;
    if(backward)return row==0?kLaptopMenuBackItem:kLaptopMenuOrder[row-1];
    return row+1==static_cast<int>(kLaptopMenuOrder.size())?kLaptopMenuBackItem:kLaptopMenuOrder[row+1];
}
inline int currentLaptop(const mc::BoardDescriptor& board){const auto id=mc::deviceProfile(board).id;
    for(int i=0;i<static_cast<int>(kSupportedLaptops.size());++i){if(kSupportedLaptops[i].id==id)return i;}
    return 3; // Universal UI for unrecognized hardware; no borrowed EC map.
}
// Presentation only. Hardware factories ALWAYS use the real SMBIOS descriptor.
inline std::atomic<int> laptopSelection{-1};
inline int selectedLaptopIndex(){const int index=laptopSelection.load();return index>=0?index:currentLaptop(mc::readBoardDescriptor());}
inline bool laptopPreview(){return selectedLaptopIndex()!=currentLaptop(mc::readBoardDescriptor());}
inline bool hydrocModel(){return selectedLaptopIndex()==4;}
inline bool gm5Model(){return selectedLaptopIndex()==5;}
inline bool l16Model(){return selectedLaptopIndex()==6;}
inline bool catalogueModel(){const int i=selectedLaptopIndex();return i>=0&&i<static_cast<int>(kSupportedLaptops.size())&&kSupportedLaptops[i].support==LaptopSupport::Inventory;}
inline bool discretePreviewModel(){const int i=selectedLaptopIndex();return i>=0&&i<static_cast<int>(kSupportedLaptops.size())&&!kSupportedLaptops[i].previewCpu.empty();}
inline std::array<mc::ProcessorVendor,2> discretePreviewVendors(){return {
    discretePreviewModel()?kSupportedLaptops[selectedLaptopIndex()].previewCpuVendor:mc::ProcessorVendor::Intel,mc::ProcessorVendor::Nvidia};}
inline std::array<std::string,2> nvidiaPreviewProcessorModels(){
    if(discretePreviewModel()){const auto& model=kSupportedLaptops[selectedLaptopIndex()];return {std::string(model.previewCpu),std::string(model.previewGpu)};}
    return {"Intel Core i7-14650HX","NVIDIA GeForce RTX 4060 Laptop GPU"};}
inline bool selectedNvidiaPreviewModel(){const int i=selectedLaptopIndex();return i>=0&&i<static_cast<int>(kSupportedLaptops.size())&&kSupportedLaptops[i].previewGpu.starts_with("NVIDIA ");}
inline bool nvidiaModelPreview(){return selectedNvidiaPreviewModel()&&laptopPreview();}
inline bool noNameModel(){return selectedLaptopIndex()==3;}
// INFO always operates on the actual computer, never the menu's preview model.
// Standard OS/tool prechecks admit each operation; a catalogue identity grants
// no EC rights and an unknown SMBIOS is not a reason to disable diagnostics.
// Owner rule27.10.2026: INFO always addresses this computer, including Unknown
// and unconnected catalog devices. Collectors perform their own reader checks.
inline bool biosDeviceActionsAllowed(){return true;}
// Explicit dump action may use a model's narrow diagnostic adapter. That does
// not grant its ordinary FANs page an unverified hardware writer.
inline bool biosFanProbesAllowed(){return !laptopPreview()&&!noNameModel()&&!catalogueModel()&&kSupportedLaptops[selectedLaptopIndex()].id!="minisforum-hpbsd"&&kSupportedLaptops[selectedLaptopIndex()].support!=LaptopSupport::Monitoring;}
// Actual adapters remain accessible under any catalog selection. A model's
// static Nvidia preview creates a tab only when its vendor is explicit.
inline bool showNvidiaTab(bool detected){return detected||selectedNvidiaPreviewModel();}
inline bool showRadeonTab(bool detected){return detected;}
inline std::array<bool,7> visibleGpuTabs(bool nvidia,bool radeon,bool ovclock=false){return {true,true,true,true,showNvidiaTab(nvidia),showRadeonTab(radeon),ovclock};}
inline mc::BoardDescriptor presentationBoard(){auto actual=mc::readBoardDescriptor();const int index=selectedLaptopIndex();
    if(index<0||index>=static_cast<int>(kSupportedLaptops.size())||index==currentLaptop(actual))return actual;
    if(index==3)return {"Unknown","","noname",""};
    for(const auto* profile:mc::deviceProfiles())if(profile->id==kSupportedLaptops[index].id)
        return {std::string(profile->manufacturer),std::string(profile->baseBoard),std::string(profile->id),std::string(profile->productName)};
    return actual;
}
struct DeviceHeader {
    std::string manufacturer, model;
    std::string label() const { return manufacturer + (model.empty() ? "" : " " + model); }
};
inline DeviceHeader deviceHeader(const mc::BoardDescriptor& board) {
    DeviceHeader result{board.manufacturer.empty() ? "MagicControl" : board.manufacturer, board.productName};
    const auto& profile=mc::deviceProfile(board);
    // One presentation catalogue owns both the menu label and header parts on
    // Windows/Linux. Raw SMBIOS fields stay untouched for exact identification.
    // Unknown hardware keeps its reported identity rather than a borrowed label.
    for (const auto& laptop : kSupportedLaptops) {
        if (laptop.id != profile.id) continue;
        constexpr std::string_view separator = " · ";
        const auto split = laptop.name.find(separator);
        if (split != std::string_view::npos)
            return {std::string(laptop.name.substr(0, split)), std::string(laptop.name.substr(split + separator.size()))};
    }
    if (!profile.displayName.empty()) result.model=profile.displayName;
    return result;
}
struct LanguageOption { Language language; std::string_view label; };
inline constexpr std::array<LanguageOption,4> kLanguageOptions{{
    {Language::Russian,"Русский"}, {Language::English,"English"},
    {Language::SimplifiedChinese,"简体中文"}, {Language::TraditionalChinese,"繁體中文"}
}};
} // namespace mc::gui::ui
