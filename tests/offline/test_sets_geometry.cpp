// Pure shared geometry audit requested by the owner24.10.2026. No GUI, OS or EC.
#include "src/gui/common/Geometry.h"
#include "OfflineTest.h"
namespace ui=mc::gui::ui;
int main(){
    unsigned cases{};
    for(int dpi:{96,144,192})for(int logicalWidth:{720,1000,1600}){
        const ui::Scale s{dpi};
        auto g=ui::setsGeometry(s(logicalWidth),s,{s(20),s(15)},3,true,true);
        ui::placeBatteryIdentity(g,s,s(260));
        ui::placeCombinedBatteryControls(g,s,s(420));
        const std::array<ui::Rect,7> buttons{g.batteryVoltage[0],g.batteryVoltage[1],g.batteryVoltage[2],
            g.battery[0],g.battery[1],g.battery[2],g.battery[3]};
        for(std::size_t i=0;i<buttons.size();++i){
            CHECK(buttons[i].width()>0);
            CHECK(buttons[i].top==buttons[0].top&&buttons[i].bottom==buttons[0].bottom);
            if(i)CHECK(buttons[i-1].right<=buttons[i].left);
        }
        CHECK(g.headings[0].right<=g.batteryDeviceIdentity.left);
        CHECK(g.batteryDeviceIdentity.right<=g.batteryIdentity.left);
        ui::placeCombinedBatteryStatus(g,s,s(100),s(420),s(30));
        CHECK(g.headings[0].right<=g.agingCaption.left);
        CHECK(g.agingCaption.right<=g.agingOff.left);
        CHECK(g.agingOff.right==g.agingOn.left);
        CHECK(g.agingOff.width()==s(42)&&g.agingOn.width()==s(42));
        CHECK(g.agingCaption.width()==s(100));
        CHECK(g.agingOff.left-g.agingCaption.right==s(4));
        CHECK(g.agingOn.right<=g.batteryVoltageSummary.left);
        CHECK(g.batteryVoltageSummary.right<=g.batteryIdentity.left);
        CHECK(g.batteryDeviceIdentity.top==g.portDetail.top);
        CHECK(g.batteryDeviceIdentity.right==g.portDetail.right);
        ui::placeCombinedBatteryStatus(g,s,0,0,0);
        CHECK(g.agingCaption.width()>0);
        CHECK(g.batteryDeviceIdentity.width()>0);
    }
    for(int dpi:{96,120,144,192,240,288}){
        const ui::Scale s{dpi};
        const auto compact=ui::setsGeometry(s(1000),s,{s(20),s(15)},3,true,true,false,{},{},4,{},2,2,{false,true,false});
        CHECK(compact.cards[0].width()==0&&compact.cards[2].width()==0);
        CHECK(compact.cards[1].top==s(8)&&compact.cards[1].left==s(12)&&compact.cards[1].right==s(988));
        CHECK(compact.extent==compact.cards[1].bottom+s(18));
        CHECK(compact.performance[1].right==s(976));
        const auto fans=ui::fansGeometry(s(1000),s,0,false);
        CHECK(fans.monitors[0].width()==0&&fans.monitors[1].width()>0&&fans.monitors[4].width()>0);
        CHECK(fans.monitors[1].left>=s(12)&&fans.monitors[4].right<=s(988));
    }
    for(int dpi:{96,120,144,168,192,240,288})for(int logicalWidth:{720,800,1000,1280,1600})
    for(bool logger:{false,true})for(bool manual:{false,true})for(int brightness:{2,4})
    for(int profiles:{2,4,5})for(int rows:{0,27,63,ui::ecBatteryDetailRows}){
        const ui::Scale s{dpi};const int width=s(logicalWidth);
        auto g=ui::setsGeometry(width,s,{s(20),s(15)},3,true,manual,logger,{},{},brightness,
            {rows,ui::osBatteryDetailRows},profiles);
        ++cases;
        const int left=s(12),right=width-s(12),inset=s(12),gap=s(6);
        CHECK(g.cards[ui::settingsCardOrder[0]].top==s(8));
        int previousBottom{};
        for(int card:ui::settingsCardOrder){
            CHECK(g.cards[card].left==left&&g.cards[card].right==right);
            CHECK(g.cards[card].height()>0);
            if(previousBottom)CHECK(g.cards[card].top==previousBottom+gap);
            previousBottom=g.cards[card].bottom;
        }
        CHECK(g.batteryInfoCards[0].left==left+inset);
        CHECK(g.batteryInfoCards[1].right==right-inset);
        CHECK(g.batteryInfoCards[1].left-g.batteryInfoCards[0].right==s(12));
        if(logger){
            // Regression:4c4a95e used content bounds24 DIP as the outer Logger
            // sides, moving both edges inward by an extra12 DIP. Never repeat.
            CHECK(g.batteryComparison.left==left&&g.batteryComparison.right==right);
            CHECK(g.batteryComparison.top==g.cards[0].bottom+gap);
            CHECK(g.batteryDetailColumns[0].card.left==left+inset);
            CHECK(g.batteryDetailColumns[1].card.right==right-inset);
            CHECK(g.batteryDetailColumns[1].card.left-g.batteryDetailColumns[0].card.right==s(12));
            ui::placeLoggerFields(g,s,{s(20),s(36),s(8)});
            CHECK(g.loggerHeading.left==left+inset);
            CHECK(g.loggerSuffix.right==right-inset);
            CHECK(g.loggerFormats.left==left+inset&&g.loggerFormats.right==right-inset);
            for(const auto& block:g.batteryDetailColumns){
                CHECK(block.card.top>g.batteryComparison.top);
                CHECK(block.card.bottom<=g.batteryComparison.bottom-s(12));
                CHECK(block.heading.left>=block.card.left&&block.heading.right<=block.card.right);
            }
            CHECK(g.extent==g.batteryComparison.bottom+gap+s(12));
        }else{
            CHECK(g.batteryComparison.width()==0);
            CHECK(g.extent==g.cards[0].bottom+gap+s(12));
        }
    }
    std::printf("SETs geometry: %u cases, %u checks; all outer/nested cards,7 DPI; no GUI/EC\n",cases,offlineChecks);
}
