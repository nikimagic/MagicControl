#include "OfflineTest.h"
#include "src/gui/common/DiscreteGpuCard.h"
#include <string_view>

namespace ui=mc::gui::ui;
namespace {
int actualIndex=0;
}
// Pure catalog fixture: no OS board discovery or hardware provider is linked.
namespace mc {
BoardDescriptor readBoardDescriptor(){return {};}
const DeviceProfile& deviceProfile(const BoardDescriptor&) noexcept {
    static DeviceProfile profile{};profile.id=ui::kSupportedLaptops[actualIndex].id;return profile;
}
}
int main(){
    for(int actual=0;actual<static_cast<int>(ui::kSupportedLaptops.size());++actual){
        actualIndex=actual;
        for(int selected=0;selected<static_cast<int>(ui::kSupportedLaptops.size());++selected){
            ui::laptopSelection.store(selected);
            const auto& model=ui::kSupportedLaptops[selected];
            const bool knownNvidia=model.previewGpu.starts_with("NVIDIA ");
            CHECK(ui::showNvidiaTab(true));
            CHECK(ui::showRadeonTab(true));
            CHECK(ui::showNvidiaTab(false)==knownNvidia);
            CHECK(!ui::showRadeonTab(false));
            CHECK(ui::nvidiaModelPreview()==(selected!=actual&&knownNvidia));
            const auto preview=ui::nvidiaPreviewCard();CHECK(preview.size()==1);
            CHECK(!preview[0].available&&!preview[0].powerControl&&preview[0].uuid.empty());
            CHECK(!preview[0].temperature&&!preview[0].powerMw&&!preview[0].utilization);
            if(knownNvidia){
                CHECK(preview[0].name.find(model.previewGpu)!=std::string::npos);
                CHECK(preview[0].status.find(model.name)!=std::string::npos);
                for(unsigned language=0;language<4;++language){
                    ui::setLanguage(language);const auto translated=ui::translate(preview[0].status);
                    CHECK(translated.find(model.name)!=std::string::npos);
                }
            }else{
                CHECK(preview[0].name=="GPU · Просмотр");
                CHECK(preview[0].status.find("GM6IR0C")==std::string::npos);
                for(unsigned language=1;language<4;++language){ui::setLanguage(language);CHECK(ui::translate(preview[0].status)!=preview[0].status);}
            }
        }
    }
    ui::setLanguage(0);ui::laptopSelection.store(-1);
    std::printf("GPU preview offline: %u checks; no hardware\n",offlineChecks);
}
