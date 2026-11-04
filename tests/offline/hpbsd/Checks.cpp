#include "src/protocols/minisforum/HpbsdFans.h"
#include "src/protocols/minisforum/HpbsdPower.h"
#include "src/protocols/jedec/Spd5118.h"
#include "MagicControl/GpuPresentation.h"
#include "MagicControl/FanControlLimits.h"
#include <iostream>
#include <stdexcept>
using namespace mc;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class F>void rejects(F f){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Unsafe input was accepted");}
int main(){try{
 check(hpbsd::stockPower(0)==std::array<int,3>{54400,60000,65000}&&hpbsd::stockPower(1)==std::array<int,3>{70800,70800,70800},"BIOS-native power endpoint changed");
 rejects([]{hpbsd::stockPower(2);});rejects([]{hpbsd::stockFanSelector(-1);});
 check(hpbsd::recoverablePower(hpbsd::stockPower(1))&&hpbsd::recoverablePower({65000,70000,70800})&&!hpbsd::recoverablePower({25000,20000,30000}),"Custom rollback baseline order");
 check(hpbsd::customPowerMaximum==kApuManualMaximum&&hpbsd::customPowerMilliwatts({5,70,255})==std::array<int,3>{5000,70000,255000},"Custom request bounds were narrowed by a diagnostic ceiling");
 rejects([]{hpbsd::customPowerMilliwatts({4,25,30});});rejects([]{hpbsd::customPowerMilliwatts({20,25,256});});rejects([]{hpbsd::customPowerMilliwatts({30,25,40});});
 // Both factory transitions and every interrupted restoration keep the same
 // protected rows. The selector is committed only after the complete table.
 for(const auto selector:{0xb1,0xb2}){
  const auto before=hpbsd::factory(selector),after=hpbsd::factory(selector==0xb1?0xb2:0xb1);
  auto value=hpbsd::values(before);
  for(const auto [index,byte]:hpbsd::plan(value,hpbsd::values(after))){
   value[index]=byte;hpbsd::validateImmutable(hpbsd::withValues(selector,value));
   check(hpbsd::reachable(hpbsd::values(before),hpbsd::values(after),value),"Factory transition prefix lost");
   auto recovery=value;
   for(const auto [i,v]:hpbsd::plan(value,hpbsd::values(before))){recovery[i]=v;hpbsd::validateImmutable(hpbsd::withValues(selector,recovery));check(hpbsd::reachable(value,hpbsd::values(before),recovery),"Factory rollback prefix lost");}
   check(recovery==hpbsd::values(before),"Factory rollback incomplete");
  }
  check(value==hpbsd::values(after),"Factory switch plan incomplete");
 }
 static_assert(fanInput::temperatureMinimum==20&&fanInput::temperatureMaximum==100);
 static_assert(fanInput::percentMinimum==5&&fanInput::percentMaximum==100);
 check(fanInput::validTemperature(20)&&fanInput::validTemperature(100)&&!fanInput::validTemperature(19)&&!fanInput::validTemperature(101),"Shared temperature bounds");
 check(fanInput::validPercent(5)&&fanInput::validPercent(100)&&!fanInput::validPercent(4)&&!fanInput::validPercent(101),"Shared percent bounds");
 {struct Point{int temperature,duty;};std::array<Point,6> old{{{10,7},{20,10},{75,4},{85,100},{101,60},{255,100}}};
  fanInput::normalizeDraft(old);check(old[0].temperature==20&&old[0].duty==10&&old[1].temperature==75&&old[1].duty==5&&old[2].temperature==85&&old[2].duty==100&&old[3].temperature==100&&old[4].temperature==255,"Legacy draft normalization/protection merge");}
 {const std::vector<FanCurvePoint> requested{{20,5},{80,30},{85,40},{100,50}};const auto effective=fanCurveWithMaximumStage(requested,85);
  check(effective==std::vector<FanCurvePoint>{{20,5},{80,30},{85,100}}&&requested.back().temperature==100,"User100C point must remain saved while native85C protection remains effective");}
 // Inject read/selector failures into the observed page7->0->7 protocol.
 for(int fail=-1;fail<24;++fail){int page=7,step{},writes{};bool restoreError{};
  const auto point=[&](){if(step++==fail)throw std::runtime_error("injected bus failure");};
  const auto read=[&](int reg,bool)->unsigned {point();if(reg==0xb)return page;if(page)return 0;switch(reg){case 0:return 0x1851;case 2:return 0x12;case 3:return 0xb380;case 5:return 3;case 0x1a:return 0;case 0x31:return 0x264;default:throw std::runtime_error("unknown register");}};
  const auto select=[&](int value){point();check(value==0||value==7,"Unexpected selector");++writes;page=value;};
  std::optional<double> result;bool failed{};
  try{result=spd5118::temperatureWithPage7Recovery(read,select);}
  catch(const spd5118::RestoreError&){restoreError=true;failed=true;}catch(const std::exception&){failed=true;}
  if(!failed)check(result==38.25,"SPD temperature decode");if(fail==-1)check(!failed,"Healthy SPD transaction failed");
  check(page==7||restoreError,"Lost SPD page without explicit restoration failure");check(writes<=2,"Repeated SPD selector writes");
 }
 {int writes{};const auto t=spd5118::temperatureWithPage7Recovery([](int reg,bool)->unsigned{return reg==0?0x1234:0;},[&](int){++writes;});check(!t&&writes==0,"Unknown hub was changed");}
 unsigned prefixes{};
 for(const hpbsd::Byte selector:{0xb1,0xb2}){
  const auto baseline=hpbsd::factory(selector);hpbsd::validateImmutable(baseline);
  for(int cpu:{5,36,50,65,100})for(int system:{5,38,65,100}){
   const auto target=hpbsd::manual({cpu,system},selector);auto current=hpbsd::values(baseline);
   check(hpbsd::allowed(target,selector),"Legal target rejected");
   const auto forward=hpbsd::plan(current,target);
   for(const auto [i,v]:forward){
    current[i]=v;auto s=hpbsd::withValues(selector,current);hpbsd::validateImmutable(s);
    check(hpbsd::reachable(hpbsd::values(baseline),target,current),"Interrupted apply cannot be recovered");
    // Recovery can itself be interrupted at every write, without modifying
    // critical rows or losing the exact restoration-prefix classification.
    auto recovering=current;
    for(const auto [index,value]:hpbsd::plan(current,hpbsd::values(baseline))){recovering[index]=value;auto restored=hpbsd::withValues(selector,recovering);hpbsd::validateImmutable(restored);check(hpbsd::reachable(current,hpbsd::values(baseline),recovering),"Interrupted Default cannot be recovered");++prefixes;}
   }
   check(current==target,"Target plan incomplete");
  }
  auto tampered=baseline;tampered[24]=50;rejects([&]{hpbsd::validateImmutable(tampered);});
  tampered=baseline;tampered[41]=39;rejects([&]{hpbsd::validateImmutable(tampered);});
  tampered=baseline;tampered[2]=255;rejects([&]{hpbsd::validateImmutable(tampered);});
  auto recipe=hpbsd::defaultDraft(selector);const std::array<int,7> ordinary{32,45,54,66,76,78,80};for(int i=0;i<7;++i)recipe[0][i].temperature=ordinary[i];recipe[1][0].temperature=34;recipe[1][1].temperature=80;
  auto inactive=recipe;inactive[0][15]={91,17};inactive[1][12]={37,83};
  check(hpbsd::curve(inactive,selector)==hpbsd::curve(recipe,selector),"inactive common cells leaked into hardware payload");
  const auto custom=hpbsd::curve(recipe,selector);check(hpbsd::allowed(custom,selector),"Native curve endpoint rejected");
  auto changed=hpbsd::values(baseline);
  for(const auto [i,value]:hpbsd::plan(changed,custom)){
   changed[i]=value;const auto s=hpbsd::withValues(selector,changed);hpbsd::validateImmutable(s);
   bool moved{};for(std::size_t k=16;k<changed.size();++k)moved|=changed[k]!=hpbsd::values(baseline)[k];
   if(moved){for(int k=0;k<7;++k)check(changed[k]>=18,"Temperature move could stop CPU fan");check(changed[14]>=15&&changed[15]>=15,"Temperature move could stop System fan");}
   check(hpbsd::reachable(hpbsd::values(baseline),custom,changed),"Curve prefix not recoverable");
   auto recovery=changed;for(const auto [index,v]:hpbsd::plan(changed,hpbsd::values(baseline))){recovery[index]=v;hpbsd::validateImmutable(hpbsd::withValues(selector,recovery));check(hpbsd::reachable(changed,hpbsd::values(baseline),recovery),"Curve restore prefix not recoverable");++prefixes;}
  }
  check(changed==custom,"Curve apply plan incomplete");
  const auto encoded=hpbsd::withValues(selector,custom);
  check(encoded[22]==80&&encoded[26]==80&&encoded[39]==80&&encoded[43]==80,"Last ordinary boundary did not move hot activation earlier");
  std::array<int,8> customSlopes{};for(int i=0;i<8;++i)customSlopes[i]=encoded[27+i];
  check(hpbsd::targetPercent(hpbsd::points(encoded),customSlopes,0,81)==100&&hpbsd::targetPercent(hpbsd::points(encoded),{},1,80)==100,"Earlier hot MAX stage lost");
  auto low=recipe;low[0][0].duty=5;low[1][0].duty=5;check(hpbsd::allowed(hpbsd::curve(low,selector),selector),"5% native curve rejected");
  auto wide=recipe;wide[0][0].temperature=20;wide[0][6].temperature=100;wide[1][0].temperature=20;wide[1][1].temperature=100;
  const auto wideNative=hpbsd::withValues(selector,hpbsd::curve(wide,selector));check(wide[0][6].temperature==100&&wide[1][1].temperature==100&&wideNative[22]==93&&wideNative[26]==93&&wideNative[39]==83&&wideNative[43]==83,"20/100C input endpoint rejected or separate firmware protection lost");
  for(int i=0;i<7;++i)wide[0][i].temperature=88+2*i;wide[1][0].temperature=98;
  check(hpbsd::curveAllowed(hpbsd::curve(wide,selector),selector),"High ordinary points cannot be encoded with separate MAX protection");
  // Legacy v3 endpoint:30 original fields, original hot boundaries. Admission
  // and exact recovery remain independent of the expanded user-input range.
  auto legacy=baseline;for(int ch=0;ch<2;++ch)for(int i=0;i<hpbsd::bandCount[ch]-1;++i){const int at=(ch?35:3)+3*i;legacy[at]=std::max<int>(legacy[at],ch?15:18);}
  for(int i=0;i<7;++i){const int at=3+3*i;legacy[27+i]=std::min(255,100*(legacy[at+3]-legacy[at])/(legacy[at+1]-legacy[at+2]));}
  check(hpbsd::curveAllowed(hpbsd::values(legacy),selector),"Legacy curve ownership recovery rejected");
  const auto migrated=hpbsd::normalizeDraft(hpbsd::points(legacy));
  for(int ch=0;ch<2;++ch)for(int i=0;i<hpbsd::bandCount[ch]-1;++i)check(fanInput::validTemperature(migrated[ch][i].temperature)&&fanInput::validPercent(migrated[ch][i].duty),"Legacy ordinary fields not normalized");
  check(hpbsd::curveAllowed(hpbsd::curve(migrated,selector),selector),"Migrated draft rejected");
  auto invalid=recipe;invalid[0][6].temperature=101;rejects([&]{hpbsd::curve(invalid,selector);});
  invalid=recipe;invalid[1][1].temperature=101;rejects([&]{hpbsd::curve(invalid,selector);});
  invalid=recipe;invalid[0][3].temperature=invalid[0][2].temperature;rejects([&]{hpbsd::curve(invalid,selector);});
  invalid=recipe;invalid[0][0].duty=0;rejects([&]{hpbsd::curve(invalid,selector);});
  invalid=recipe;invalid[0][0].duty=4;rejects([&]{hpbsd::curve(invalid,selector);});
  invalid=recipe;invalid[0][0].temperature=19;rejects([&]{hpbsd::curve(invalid,selector);});
  std::array<int,8> cpuSlopes{},systemSlopes{};for(int i=0;i<8;++i)cpuSlopes[i]=baseline[27+i];
  const auto p=hpbsd::points(baseline);check(hpbsd::targetPercent(p,cpuSlopes,0,94)==100,"CPU hot stage plot lost");check(hpbsd::targetPercent(p,systemSlopes,1,83)==100,"System hot stage plot lost");
 }
 rejects([]{hpbsd::manual({4,38});});rejects([]{hpbsd::manual({36,4});});rejects([]{hpbsd::manual({101,100});});rejects([]{hpbsd::factory(0);});
 GraphicsAdapter a,b,software;a.id="gpu-a";a.physicalId="pci:one";a.name="Radeon";a.vendorId=0x1002;a.type=GpuType::Integrated;b=a;b.id="gpu-b";
 software.id="warp";software.vendorId=0x1414;software.software=true;
 GpuReading live;live.uuid="adlx";live.osAdapterId=a.id;live.physicalId=a.physicalId;live.available=true;live.type=GpuType::Integrated;live.temperature=35;live.utilization=0;live.graphicsMhz=800;live.powerMw=5000;live.status="fresh ADLX";
 GpuReading absent;absent.uuid=b.id;absent.osAdapterId=b.id;absent.physicalId=b.physicalId;absent.status="OS fallback unavailable";
 for(bool reverse:{false,true}){
  std::vector<GpuProviderReading> rows{{0x1002,live,100},{0x1002,absent,100}};if(reverse)std::reverse(rows.begin(),rows.end());
  const auto result=gpuPresentation({a,b,software},rows,{{"display",b.id,"","route","",true}},100);
  check(result.cards.size()==1,"RDP alias/software became another GPU card");
  check(result.cards[0].status=="fresh ADLX"&&result.cards[0].celsius==35&&result.cards[0].usagePercent==0,"Unavailable fallback overwrote a fresh GPU provider");
  check(result.routes[0].adapterId==result.cards[0].adapterId,"Display route lost its physical alias");
 }
 b.physicalId="pci:two";absent.physicalId=b.physicalId;
 check(gpuPresentation({a,b},{{0x1002,live,100},{0x1002,absent,100}},{},100).cards.size()==2,"Different physical GPUs were merged by name");
 std::cout<<"PASS: "<<prefixes<<" interrupted restore prefixes; critical rows/overrides, input bounds, RDP aliases and provider status\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
