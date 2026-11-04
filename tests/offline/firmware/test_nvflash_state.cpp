// Pure policy/transcript regression tests. No native adapters or OS APIs.
#include "NvflashResiduePolicy.h"
#include "MagicControl/OperationMessages.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace mc::diagnostics::nvflash;
namespace {
unsigned checks{};
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
auto entries(const std::shared_ptr<mc::OperationMessages>& m){std::size_t cursor{};return m->read(cursor);}
void publish(unsigned id,const std::string& body){mc::OperationMessages::publish("vbios_dump",body,id);}
}
int main(){try{
    ResidueProof base{"nvflash",1,1,3,true,true,true,ResidueImage::PinnedSigned};
    check(mayRemoveStoppedResidue(base),"verified stopped residue admitted");
    for(unsigned state:{0u,2u,3u,4u,5u,6u,7u}){auto p=base;p.state=state;check(!mayRemoveStoppedResidue(p),"non-stopped state must never be removed");}
    for(unsigned type:{0u,2u,16u,32u}){auto p=base;p.type=type;check(!mayRemoveStoppedResidue(p),"wrong service type");}
    for(unsigned start:{0u,1u,2u,4u}){auto p=base;p.start=start;check(!mayRemoveStoppedResidue(p),"wrong start mode");}
    for(auto name:{"other","nvflash-backup","evil_nvflash","nvflash.sys"}){auto p=base;p.name=name;check(!mayRemoveStoppedResidue(p),"name substring is not identity");}
    {auto p=base;p.canonicalPath=false;check(!mayRemoveStoppedResidue(p),"unreviewed path");}
    {auto p=base;p.noActiveReader=false;check(!mayRemoveStoppedResidue(p),"active reader");}
    {auto p=base;p.sameConfiguration=false;check(!mayRemoveStoppedResidue(p),"configuration changed");}
    {auto p=base;p.image=ResidueImage::Untrusted;check(!mayRemoveStoppedResidue(p),"untrusted SYS");}
    {auto p=base;p.image=static_cast<ResidueImage>(99);check(!mayRemoveStoppedResidue(p),"invalid image enum");}
    {auto p=base;p.image=ResidueImage::Missing;check(mayRemoveStoppedResidue(p),"strict stopped record with missing SYS");p.state=4;check(!mayRemoveStoppedResidue(p),"missing SYS does not permit active service cleanup");}
    check(knownResidueSys("nvflash.sys")&&!knownResidueSys("nvflash.sys --extra")&&!knownResidueSys("../nvflash.sys"),"exact SYS basename");
    {
        std::string order;
        check(readWithResidueCleanup([&]{order+='R';return 0u;},[&]{order+='C';})==0&&order=="RC","cleanup after success");
        order.clear();check(readWithResidueCleanup([&]{order+='R';return 7u;},[&]{order+='C';})==7&&order=="RC","nonzero exit retained with cleanup");
        for(bool cleanupFails:{false,true}){
            unsigned cleaned{};std::string error;
            try{readWithResidueCleanup([]()->unsigned{throw std::runtime_error("reader pending/cancelled");},[&]{++cleaned;if(cleanupFails)throw std::runtime_error("cleanup busy");});}
            catch(const std::exception& e){error=e.what();}
            check(cleaned==1&&error=="reader pending/cancelled","original reader error wins over deferred cleanup");
        }
        std::string error;
        try{readWithResidueCleanup([]{return 0u;},[]{throw std::runtime_error("cleanup failed");});}
        catch(const std::exception& e){error=e.what();}
        check(error=="cleanup failed","successful reader does not hide cleanup failure");
    }
    {
        auto m=mc::OperationMessages::create(9001);
        publish(9001,"\"event\":\"capture_failed\",\"details\":{\"error\":\"Служба занята.\"}");
        m->finish("Дамп не завершён: VBIOS не подтверждён, EEPROM не подтверждён. Служба занята. Файлы: /private",false);
        const auto rows=entries(m);check(rows.size()==2,"root error and failure status, no duplicate wrapper");
        check(rows.front().text=="Служба занята.","root error preserved");
        publish(9001,"\"event\":\"capture_failed\",\"details\":{\"error\":\"Late error\"}");
        check(entries(m).size()==2&&m->done(),"closed transcript ignores late events");
    }
    {
        auto m=mc::OperationMessages::create(9002);m->finish("files saved",true,false,"FANs: previous intent unknown; confirmed Default retained.");
        const auto rows=entries(m);check(rows.size()==3,"saved result separate from warning");
        check(rows[0].text=="Данные сохранены."&&rows[0].tone==mc::OperationMessages::Tone::Success,"saved green");
        check(rows[1].tone==mc::OperationMessages::Tone::Error,"warning red");
        check(std::none_of(rows.begin(),rows.end(),[](const auto& r){return r.text=="Сбор данных не завершён."||r.text=="Сбор данных завершён успешно.";}),"no false failure/all-success for saved warning");
    }
    for(const auto* summary:{"Частично: VBIOS сохранён, EEPROM не подтверждён. ","Частично: VBIOS не подтверждён, EEPROM сохранён. "}){
        auto m=mc::OperationMessages::create(9010);
        publish(9010,"\"event\":\"capture_failed\",\"details\":{\"error\":\"Synthetic cleanup refusal\"}");
        m->finish(std::string(summary)+"Synthetic cleanup refusal Файлы: /private",false);
        check(entries(m).size()==2,"partial summary does not repeat the same root failure");
    }
    {
        auto m=mc::OperationMessages::create(9003);
        for(int i=0;i<2;++i){
            publish(9003,"\"event\":\"driver_collision\",\"details\":{\"state\":1}");
            publish(9003,"\"event\":\"driver_residue_removed\",\"details\":{}");
        }
        publish(9003,"\"event\":\"vendor_started\",\"details\":{\"operation\":\"save_vbios\"}");
        publish(9003,"\"event\":\"pass_file\",\"details\":{\"mode\":\"vbios\",\"pass\":1,\"bytes\":512,\"exit_code\":0}");
        publish(9003,"\"event\":\"driver_cleanup_checked\",\"details\":{}");
        publish(9003,"\"event\":\"mode_verified\",\"details\":{\"mode\":\"vbios\"}");
        const auto rows=entries(m);check(rows.size()==7,"stage messages and service deduplication");
        check(rows[1].text=="Служба NVFlash уже остановлена.","observation does not claim StopService");
        check(rows.back().tone==mc::OperationMessages::Tone::Success,"verified mode green");
    }
    {
        auto m=mc::OperationMessages::create(9004);m->finish("cancel",true,true,"FAN warning");
        const auto rows=entries(m);check(rows.size()==2&&rows[0].text=="Сбор данных отменён.","cancelled is not success");
    }
    std::cout<<"PASS "<<checks<<" pure residue/transcript checks; no SCM, driver, vendor or hardware linkage.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
