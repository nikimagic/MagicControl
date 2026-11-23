// Pure byte-buffer tests. No product entry point, platform library or reader.
#include "BiosImageValidation.h"
#include "VbiosImageValidation.h"
#include "NvflashOutput.h"
#include "NvflashPlatform.h"
#include "OfflineTest.h"
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
namespace bios=mc::diagnostics::bios;
namespace vbios=mc::diagnostics::vbios;
namespace nv=mc::diagnostics::nvflash;
using Bytes=std::vector<char>;
using Rom=std::vector<std::uint8_t>;
constexpr std::size_t MiB=1024*1024;
char byte(unsigned value){return std::bit_cast<char>(static_cast<std::uint8_t>(value));}
void put16(Bytes& b,std::size_t p,unsigned n){b.at(p)=char(n);b.at(p+1)=char(n>>8);}
void put32(Bytes& b,std::size_t p,std::uint32_t n){for(unsigned i=0;i<4;++i)b.at(p+i)=char(n>>(8*i));}
void signature(Bytes& b,std::size_t p,std::string_view s){std::copy(s.begin(),s.end(),b.begin()+p);}
void region(Bytes& b,unsigned n,unsigned base,unsigned end){put32(b,0x40+4*n,base|(end<<16));}
void fv(Bytes& b,std::size_t at){
    constexpr unsigned length=0x20000,header=72;
    std::fill_n(b.begin()+at,header,char{0});
    // PI FFS2 GUID and a complete block map, rather than just _FVH magic.
    constexpr std::array<unsigned char,16> guid{0x78,0xe5,0x8c,0x8c,0x3d,0x8a,0x1c,0x4f,0x99,0x35,0x89,0x61,0x85,0xc3,0x2d,0xd3};
    std::transform(guid.begin(),guid.end(),b.begin()+at+16,[](auto value){return byte(value);});
    put32(b,at+32,length);put32(b,at+40,0x4856465f);put16(b,at+48,header);b[at+55]=2;
    put32(b,at+56,1);put32(b,at+60,length);
    unsigned sum{};for(unsigned i=0;i<header;i+=2)sum+=std::uint8_t(b[at+i])|(unsigned(std::uint8_t(b[at+i+1]))<<8);
    put16(b,at+50,0-sum);
}
Bytes spi(bool two=false,std::size_t size=0){
    if(!size)size=(two?8:4)*MiB;
    Bytes b(size,byte(0xff));
    put32(b,16,0x0ff0a55a);put32(b,20,3|(4<<16)|(two?1<<8:0));put32(b,24,9);
    unsigned density{};for(auto n=size/(two?2:1)/(512*1024);n>1;n>>=1)++density;
    put32(b,0x30,density|(two?density<<4:0));
    for(unsigned n=0;n<16;++n)put32(b,0x40+4*n,0x00007fff);
    region(b,0,0,0);region(b,2,1,2);region(b,3,3,3);region(b,1,4,unsigned(size/4096-1));
    put32(b,0x90,0xf00);signature(b,0x1000,"$FPT");b[0x3000]=byte(0x88);
    fv(b,0x4000);fv(b,0x24000);b[size-16]=byte(0xe9);return b;
}
template<class F>void refuses(std::string_view marker,F action){
    bool failed{};try{action();}catch(const std::runtime_error& e){failed=true;CHECK(std::string_view(e.what()).find(marker)!=std::string_view::npos);}CHECK(failed);
}
void afu(){
    for(auto n:{std::size_t(0),MiB/2,MiB-1,65*MiB}){Bytes b(n,'x');refuses("outside",[&]{bios::validate(b,n,false);});}
    for(char blank:{char(0),byte(0xff)}){Bytes b(MiB,blank);refuses("only00/FF",[&]{bios::validate(b,MiB,false);});}
    Bytes mixed(MiB,0);mixed.back()=byte(0xff);refuses("only00/FF",[&]{bios::validate(mixed,MiB,false);});
    Bytes b(MiB,'x');
    for(auto declared:{std::uint64_t(0),std::uint64_t(MiB-1),std::uint64_t(2*MiB),std::numeric_limits<std::uint64_t>::max()})
        refuses("SMBIOS",[&]{bios::validate(b,declared,false);});
    // AMI/AMD acceptance does not require an Intel Descriptor, ME or reset opcode.
    for(auto size:{MiB,32*MiB,64*MiB}){
        Bytes plain(size,0);plain[100]=1;const auto s=bios::validate(plain,size,false);
        CHECK(s.kind=="declared_rom_image");CHECK(s.descriptorBytes==0&&s.components==0&&s.regions.empty());
    }
    auto intel=spi();CHECK(bios::validate(intel,intel.size(),false).kind=="declared_rom_image");
    // No physical layout or bootability claim is returned by this size-only path.
    const Bytes tiny(3,'x');refuses("Truncated",[&]{bios::word(tiny,0);});
    refuses("Truncated",[&]{bios::word(b,std::numeric_limits<std::size_t>::max());});
    CHECK(bios::word(b,b.size()-4)==0x78787878);
}
void intel(bool nineteen){
    auto base=spi(nineteen);const auto size=base.size();
    for(auto n:{std::size_t(0),MiB/2,65*MiB}){Bytes b(n,'x');refuses("outside",[&]{bios::validate(b,0,true);});}
    for(char blank:{char(0),byte(0xff)}){Bytes b(MiB,blank);refuses("only00/FF",[&]{bios::validate(b,0,true);});}
    const auto valid=bios::validate(base,0,true); // Intel capacity source is IFD, not SMBIOS.
    CHECK(valid.kind=="intel_spi"&&valid.descriptorBytes==size&&valid.components==(nineteen?2u:1u));
    CHECK(valid.firmwareVolumes==2&&valid.regions.size()==16&&bios::allRegionsReadable(valid));
    CHECK(valid.regions[0].base==0&&valid.regions[0].end==4095);
    CHECK(valid.regions[1].base==0x4000&&valid.regions[1].end==size-1);
    CHECK(valid.regions[2].base==0x1000&&valid.regions[2].end==0x2fff);
    CHECK(bios::validate(base,1,true).descriptorBytes==size); // SMBIOS does not define Intel SPI capacity.
    auto otherComponents=spi(!nineteen);CHECK(bios::validate(otherComponents,0,true).components==(nineteen?1u:2u));
    if(!nineteen){
        auto minimum=spi(false,MiB);CHECK(bios::validate(minimum,0,true).descriptorBytes==MiB);
        auto maximum=spi(false,64*MiB);CHECK(bios::validate(maximum,0,true).descriptorBytes==64*MiB);
    }
    const auto mutation=[&](std::string_view reason,auto edit){auto b=base;edit(b);refuses(reason,[&]{bios::validate(b,0,true);});};
    mutation("Descriptor missing",[](auto& b){put32(b,16,0);});
    mutation("table offsets",[](auto& b){put32(b,20,(4<<16)|1);});
    mutation("table offsets",[](auto& b){put32(b,20,3|(255<<16));});
    mutation("table offsets",[](auto& b){put32(b,24,1);});
    mutation("component count",[](auto& b){put32(b,20,3|(4<<16)|(2<<8));});
    mutation("density",[](auto& b){put32(b,0x30,8);});
    if(nineteen)mutation("density",[](auto& b){put32(b,0x30,3|(8<<4));});
    mutation("flash capacity",[](auto& b){b.resize(b.size()-4096);});
    mutation("flash capacity",[](auto& b){b.resize(b.size()+4096);});
    mutation("region outside",[](auto& b){region(b,1,4,unsigned(b.size()/4096));});
    mutation("region extent",[](auto& b){region(b,0,0,1);});
    mutation("region extent",[](auto& b){region(b,0,1,1);});
    for(unsigned n:{0u,1u,2u})mutation("region disabled",[n](auto& b){region(b,n,0x7fff,0);});
    mutation("overlap",[](auto& b){region(b,1,3,unsigned(b.size()/4096-1));});
    // A gap is a conservative unsupported-layout refusal, not proof of corruption.
    mutation("gaps",[](auto& b){region(b,1,5,unsigned(b.size()/4096-1));});
    mutation("cover flash",[](auto& b){region(b,1,4,unsigned(b.size()/4096-2));});
    mutation("only00/FF",[](auto& b){std::fill(b.begin()+0x1000,b.begin()+0x3000,byte(0xff));});
    mutation("only00/FF",[](auto& b){std::fill(b.begin()+0x3000,b.begin()+0x4000,char{0});});
    mutation("partition header",[](auto& b){signature(b,0x1000,"NONE");});
    auto cpd=base;signature(cpd,0x1000,"$CPD");CHECK(bios::validate(cpd,0,true).firmwareVolumes==2);
    mutation("checksum",[](auto& b){b[0x4000+50]^=1;});
    mutation("FV headers or reset",[](auto& b){put32(b,0x24000+40,0);});
    mutation("FV headers or reset",[](auto& b){b[b.size()-16]=0;b[b.size()-15]=0;});
    for(unsigned nops:{0u,1u,2u,3u})for(unsigned opcode:{0xe9u,0xeau}){
        auto b=base;std::fill_n(b.end()-16,nops,byte(0x90));b[size-16+nops]=byte(opcode);
        CHECK(bios::validate(b,0,true).firmwareVolumes==2);
    }
    std::array<char,16> stub{};CHECK(!bios::resetJump(std::span<const char>(stub).first(15)));
    stub.fill(byte(0x90));CHECK(!bios::resetJump(stub));
    stub[14]=byte(0xe9);CHECK(!bios::resetJump(stub)); // Truncated rel16.
    stub.fill(byte(0x90));stub[12]=byte(0xea);CHECK(!bios::resetJump(stub)); // Truncated far pointer.
    stub.fill(0);stub[2]=byte(0xe9);CHECK(!bios::resetJump(stub)); // No arbitrary opcode scanning.
    stub.fill(0);stub[0]=byte(0xfa);stub[1]=byte(0xe9);CHECK(bios::resetJump(stub)); // Preserve prior opcode1 admission.
    // Valid payload changes do not become a header-checksum or bootability test.
    auto payload=base;payload[0x5000]^=1;CHECK(bios::validate(payload,0,true).firmwareVolumes==2);
    // Read permission is a separate mandatory collector gate after Shape parsing.
    auto denied=base;put32(denied,0x90,0xb00);CHECK(!bios::allRegionsReadable(bios::validate(denied,0,true)));
    auto extended=base;region(extended,1,5,unsigned(size/4096-1));region(extended,12,4,4);fv(extended,0x5000);
    CHECK(!bios::allRegionsReadable(bios::validate(extended,0,true)));
    Bytes shortHeader(55,'x');CHECK(bios::firmwareVolumes(shortHeader)==0);
    auto hugeFv=base;put32(hugeFv,0x4000+36,0xffffffff);CHECK(bios::firmwareVolumes(hugeFv)==1);
    auto truncated=base;put32(truncated,0x4000+32,0xffffffff);CHECK(bios::firmwareVolumes(truncated)==1);
    auto odd=base;put16(odd,0x4000+48,73);CHECK(bios::firmwareVolumes(odd)==1);
    auto shortFv=base;put16(shortFv,0x4000+48,54);CHECK(bios::firmwareVolumes(shortFv)==1);
    auto longFv=base;put16(longFv,0x4000+48,4098);CHECK(bios::firmwareVolumes(longFv)==1);
    auto invertedFv=base;put32(invertedFv,0x4000+32,16);CHECK(bios::firmwareVolumes(invertedFv)==1);
    // Distinct reader admission checks; both reuse this same IFD2 image parser.
    if(nineteen){
        for(unsigned id:{0x7700u,0x777fu,0x7f00u,0x7f7fu})CHECK(bios::reviewedFpt19Lpc(static_cast<std::uint16_t>(id)));
        for(unsigned id:{0x76ffu,0x7780u,0x7effu,0x7f80u,0x7a0cu})CHECK(!bios::reviewedFpt19Lpc(static_cast<std::uint16_t>(id)));
        for(auto id:{"eluktronics-hydroc16g2","mechrevo-yaoshi-series-x6ar55xy"})CHECK(bios::fpt19Ultra275Profile(id,"Intel(R) Core(TM) Ultra 9 275HX"));
        CHECK(!bios::fpt19Ultra275Profile("mechrevo-gm6ir0c","Intel Core Ultra 9 275HX"));
        CHECK(!bios::fpt19Ultra275Profile("eluktronics-hydroc16g2","Intel Core Ultra 9 285HX"));
        CHECK(!bios::fpt19Ultra275Profile("eluktronics-hydroc16g2","AMD Ryzen 9 275HX"));
    }else{
        for(unsigned id:{0x7a00u,0x7a0cu,0x7affu,0x5180u,0x51ffu,0x5480u,0x54ffu})CHECK(bios::reviewedFpt16Lpc(static_cast<std::uint16_t>(id)));
        for(unsigned id:{0x79ffu,0x7b00u,0x517fu,0x5200u,0x547fu,0x5500u,0x7700u})CHECK(!bios::reviewedFpt16Lpc(static_cast<std::uint16_t>(id)));
    }
}
void romWord(Rom& b,std::size_t p,unsigned n){b.at(p)=std::uint8_t(n);b.at(p+1)=std::uint8_t(n>>8);}
void checksum(Rom& b,std::size_t at=0){b[at+511]=0;unsigned sum{};for(unsigned i=0;i<512;++i)sum+=b[at+i];b[at+511]=std::uint8_t(0-sum);}
Rom pciRom(unsigned device=0x28a0,unsigned code=0){
    Rom b(512,0);b[0]=0x55;b[1]=0xaa;romWord(b,0x18,0x1c);
    for(unsigned i=0;i<4;++i)b[0x1c+i]=std::uint8_t("PCIR"[i]);
    romWord(b,0x20,0x10de);romWord(b,0x22,device);romWord(b,0x26,0x18);romWord(b,0x2c,1);
    b[0x30]=std::uint8_t(code);b[0x31]=0x80;checksum(b);return b;
}
void nvidia(){
    auto base=pciRom();const auto valid=vbios::validate(base,0x28a0);
    CHECK(valid.images.size()==1&&valid.images[0].bytes==512&&valid.images[0].checksum==0&&valid.trailingBytes==0);
    const auto mutation=[&](std::string_view reason,auto edit){auto b=base;edit(b);refuses(reason,[&]{vbios::validate(b,0x28a0);});};
    for(auto size:{std::size_t(0),std::size_t(511),65*MiB}){Rom b(size,0);refuses("truncated or oversized",[&]{vbios::validate(b,0x28a0);});}
    for(auto c:{std::uint8_t(0),std::uint8_t(255)}){Rom b(512,c);refuses("55AA",[&]{vbios::validate(b,0x28a0);});}
    mutation("55AA",[](auto& b){b[0]=0;});
    mutation("pointer/signature",[](auto& b){romWord(b,0x18,0xfff0);});
    mutation("pointer/signature",[](auto& b){romWord(b,0x18,0x10);});
    mutation("pointer/signature",[](auto& b){b[0x1c]='X';});
    mutation("image length",[](auto& b){romWord(b,0x2c,0);});
    mutation("image length",[](auto& b){romWord(b,0x2c,2);});
    mutation("image length",[](auto& b){romWord(b,0x26,0x17);});
    mutation("image length",[](auto& b){romWord(b,0x26,512);});
    mutation("does not match",[](auto& b){romWord(b,0x20,0x1002);});
    mutation("does not match",[](auto& b){romWord(b,0x22,0x2800);});
    mutation("does not match",[](auto& b){b[0x30]=1;});
    mutation("checksum",[](auto& b){b[100]^=1;});
    mutation("55AA",[](auto& b){b[0x31]=0;checksum(b);});
    auto efi=pciRom(0x28a0,3);efi[100]^=1;CHECK(vbios::validate(efi,0x28a0).images[0].codeType==3);
    auto chain=base;chain[0x31]=0;checksum(chain);chain.insert(chain.end(),efi.begin(),efi.end());
    const auto pair=vbios::validate(chain,0x28a0);CHECK(pair.images.size()==2&&pair.images[1].offset==512);
    auto entire=base;entire.resize(2*MiB,255);CHECK(vbios::validate(entire,0x28a0).trailingBytes==2*MiB-512);
    auto alias=base;romWord(alias,0x22,0xffff);alias[0x28]=3;romWord(alias,0x24,0x1c);romWord(alias,0x26,0x1c);
    romWord(alias,0x38,0x2800);romWord(alias,0x3a,0x28a0);checksum(alias);CHECK(vbios::validate(alias,0x28a0).images.size()==1);
    alias=base;romWord(alias,0x22,0xffff);alias[0x28]=3;romWord(alias,0x24,0xffff);checksum(alias);
    refuses("device list",[&]{vbios::validate(alias,0x28a0);});
    // PCI3 lists must be outside PCIR and terminated even if the device occurs.
    alias=base;romWord(alias,0x22,0xffff);alias[0x28]=3;romWord(alias,0x26,0x1c);romWord(alias,0x24,0x1c);
    std::fill(alias.begin()+0x38,alias.end(),1);romWord(alias,0x38,0x28a0);checksum(alias);
    refuses("device list",[&]{vbios::validate(alias,0x28a0);});
    alias=base;romWord(alias,0x22,0xffff);alias[0x28]=3;romWord(alias,0x26,0x1c);romWord(alias,0x24,0x0e);romWord(alias,0x2a,0x28a0);checksum(alias);
    refuses("device list",[&]{vbios::validate(alias,0x28a0);});
    alias=base;alias[0x28]=3;romWord(alias,0x26,0x1c);romWord(alias,0x24,0xffff);checksum(alias);
    refuses("device list",[&]{vbios::validate(alias,0x28a0);});
    const auto descriptor=[](std::string geometry){return "EEPROM ID (EF,4015) : Winbond SyntheticPart 2.7-3.6V "+geometry+", page\n";};
    const auto capacity=nv::capacityFromCheck(descriptor("16384Kx1S"));
    CHECK(capacity.admitted&&capacity.bytes==2*MiB&&nv::entireSizeMatches(capacity,2*MiB));
    CHECK(!nv::entireSizeMatches(capacity,2000*1024));CHECK(!nv::entireSizeMatches(capacity,0));
    CHECK(nv::capacityFromCheck(descriptor("2048Kx8S")).bytes==2*MiB);
    CHECK(nv::capacityFromCheck(descriptor("131072Kx1S")).admitted);
    CHECK(!nv::capacityFromCheck(descriptor("262144Kx1S")).admitted);
    CHECK(!nv::capacityFromCheck("EEPROM ID (FF,FFFF) : Unknown").admitted);
    CHECK(!nv::capacityFromCheck(descriptor("18446744073709551615Kx8S")).admitted);
    CHECK(!nv::capacityFromCheck(descriptor("16384Kx1S")+"Limited the part size to the max of buffer size").admitted);
    CHECK(nv::compareConfirmed("Firmware images are identical"));CHECK(!nv::compareConfirmed(""));
    CHECK(!nv::compareConfirmed("Firmware images are identical\nERROR: read failed"));
    CHECK(!nv::sameCapacity(capacity,nv::capacityFromCheck(descriptor("32768Kx1S"))));
    const std::set<std::string> allowed{"--list","--check","--index=7","--save","--entire","--compare","/artifact.rom"};
    for(auto command:{nv::Command::List,nv::Command::Check,nv::Command::Save,nv::Command::SaveEntire,nv::Command::Compare})
        for(const auto& arg:nv::arguments(command,7,"/artifact.rom"))CHECK(allowed.contains(arg));
    CHECK(rejected([]{nv::arguments(nv::Command::Save,7,"--protectoff");}));
    CHECK(rejected([]{nv::arguments(static_cast<nv::Command>(99),7,"/artifact.rom");}));
}
int main(int argc,char** argv){try{
    if(argc>=5&&std::string_view(argv[1])=="inspect"){
        std::ifstream f(argv[3],std::ios::binary);if(!f)throw std::runtime_error("Cannot open fixture");
        const Bytes b((std::istreambuf_iterator<char>(f)),{});
        if(std::string_view(argv[2])=="afu"||std::string_view(argv[2])=="intel"){
            const auto s=bios::validate(b,std::stoull(argv[4]),std::string_view(argv[2])=="intel");
            std::cout<<"{\"accepted\":true,\"kind\":\""<<s.kind<<"\",\"descriptor_bytes\":"<<s.descriptorBytes<<",\"components\":"<<s.components
                <<",\"firmware_volumes\":"<<s.firmwareVolumes<<",\"all_regions_readable\":"<<(bios::allRegionsReadable(s)?"true":"false")<<"}\n";
        }else if(std::string_view(argv[2])=="nvidia"){
            const auto shape=vbios::validate(
                {reinterpret_cast<const std::uint8_t*>(b.data()),b.size()},
                static_cast<unsigned>(std::stoul(argv[4],nullptr,0)));
            std::cout<<"{\"ifr_version\":"<<shape.ifrVersion<<",\"prefix_bytes\":"<<shape.prefixBytes
                <<",\"trailing_bytes\":"<<shape.trailingBytes<<",\"images\":"<<shape.images.size()<<"}\n";
        }else throw std::runtime_error("Unknown fixture route");return 0;
    }
    CHECK(argc==2);const std::string_view suite=argv[1];
    if(suite=="afu")afu();else if(suite=="fpt16")intel(false);else if(suite=="fpt19")intel(true);else if(suite=="nvidia")nvidia();else throw std::runtime_error("Unknown suite");
    std::cout<<"PASS "<<suite<<": "<<offlineChecks<<" pure-buffer checks; no hardware/vendor code linked.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 4;}}
