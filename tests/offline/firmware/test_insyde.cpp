#include "InsydeBiosRead.h"
#include <fstream>
#include <iostream>
using namespace mc::diagnostics;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void put(std::vector<char>& b,std::size_t at,std::uint64_t value,unsigned count=4){for(unsigned n=0;n<count;++n)b.at(at+n)=static_cast<char>(value>>(n*8));}
void checksum(std::vector<char>& b,std::size_t at,std::size_t size,std::size_t field){b[at+field]=0;unsigned sum{};for(std::size_t i=0;i<size;++i)sum+=static_cast<unsigned char>(b[at+i]);b[at+field]=static_cast<char>(0-sum);}
std::vector<char> synthetic(std::size_t size,std::size_t map,bool amd=false){
 std::vector<char> b(size,0);put(b,0x2020,0x1000,8);put(b,0x2028,0x4856465f);put(b,0x2030,56,2);
 unsigned sum{};for(unsigned at=0;at<56;at+=2)sum+=static_cast<unsigned char>(b[0x2000+at])|(unsigned(static_cast<unsigned char>(b[0x2001+at]))<<8);put(b,0x2032,0-sum,2);
 put(b,map,0x4d444648);put(b,map+4,112);put(b,map+8,28);put(b,map+12,84);b[map+17]=1;put(b,map+20,0x100000000ull-size,8);
 b[map+28]=1;put(b,map+28+40,size,8);put(b,map+28+48,1);checksum(b,map,28,19);
 if(amd){put(b,0x6000,0x55aa55aa);put(b,0x6014,0x8000);put(b,0x8000,0x50535024);put(b,0x8008,1);put(b,0x8010,0xb);put(b,0x8014,0xffffffff);put(b,0x8004,insyde::fletcher(std::span<const char>(b).subspan(0x8008,24)));}
 return b;
}
template<class F>void rejects(F f){bool failed{};try{f();}catch(const std::exception&){failed=true;}if(!failed)throw std::runtime_error("Invalid Insyde case accepted");}
int main(int argc,char** argv){try{
 check(insyde::candidate("AuthenticAMD","HUAWEI",true),"Honor native H2O evidence lost");
 for(const auto* cpu:{"AuthenticAMD","GenuineIntel"})check(insyde::candidate(cpu,"Insyde Software Corp.",false)&&insyde::candidate(cpu,"unknown",true),"Unknown model with actual Insyde evidence rejected");
 check(!insyde::candidate("AuthenticAMD","HUAWEI",false)&&!insyde::candidate("GenuineIntel","AMI",false)&&!insyde::candidate("ARM","Insyde",true),"Brand or wrong CPU cannot authorize native reader");
 std::vector<char> acpi(36,0);std::copy_n("UEFI",4,acpi.begin());put(acpi,4,36);std::copy_n("H2O BIOS",8,acpi.begin()+16);checksum(acpi,0,36,9);
 check(insyde::acpiEvidence(acpi),"Valid native ACPI evidence rejected");acpi[8]^=1;check(!insyde::acpiEvidence(acpi),"Broken ACPI checksum accepted");
 const auto args=insyde::readArguments("C:\\capture\\bios.partial.bin");if(args[1]!="-g"||args.size()!=2)throw std::runtime_error("Non-read arguments");
 if(!insyde::readExitAccepted(0)||!insyde::readExitAccepted(3010)||insyde::readExitAccepted(259)||insyde::readExitAccepted(1703))throw std::runtime_error("Insyde read exit policy");
 rejects([]{insyde::readArguments("");});rejects([]{insyde::readArguments("-ecp");});rejects([]{insyde::readArguments("bios\n-g");});
 rejects([]{std::vector<char> b(8*1024*1024,0);insyde::validate(b,0,"GenuineIntel");});
 for(std::size_t size:{8u*1024*1024,32u*1024*1024})for(bool amd:{false,true}){
  auto generated=synthetic(size,0x4000,amd);const auto cpu=amd?"AuthenticAMD":"GenuineIntel";
  const auto v=insyde::validate(generated,0,cpu);check(v.romBytes==size&&v.map.offset==0x4000&&v.map.base==0x100000000ull-size,"Dynamic size/map still depends on Honor offsets");
  rejects([&]{insyde::validate(generated,size/2,cpu);});
  auto broken=generated;broken[0x4013]^=1;rejects([&]{insyde::validate(broken,0,cpu);});
  broken=generated;put(broken,0x4000+28+40,size+1,8);rejects([&]{insyde::validate(broken,0,cpu);});
  broken=generated;put(broken,0x4000+28+48,0);rejects([&]{insyde::validate(broken,0,cpu);});
  const auto valid=insyde::validate(broken,0,cpu,[](auto){return std::string(64,'0');});check(valid.protectedHashes==1,"Immutable region hash was not checked");
  rejects([&]{insyde::validate(broken,0,cpu,[](auto){return std::string(64,'1');});});
  generated.resize(size/2);rejects([&]{insyde::validate(generated,0,cpu);});
 }
 if(argc!=2)throw std::runtime_error("Known private original16MiB image required");
 std::ifstream file(argv[1],std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Fixture missing");std::vector<char> b(static_cast<std::size_t>(file.tellg()));file.seekg(0);file.read(b.data(),static_cast<std::streamsize>(b.size()));
 const auto verified=insyde::validate(b,0,"AuthenticAMD");check(verified.map.offset==0xea8000&&verified.romBytes==b.size()&&verified.pspRoots==1,"Original Honor no longer validates through generic map/PSP resolver");
 check(insyde::validate(b,b.size(),"AuthenticAMD").smbiosSize,"SMBIOS size source lost");
 auto broken=b;broken[0xea8000]=0;rejects([&]{insyde::validate(broken,0,"AuthenticAMD");});
 broken=b;broken[0xea8017]=0;rejects([&]{insyde::validate(broken,0,"AuthenticAMD");});
 broken=b;broken[0x74000]=0;rejects([&]{insyde::validate(broken,0,"AuthenticAMD");});
 broken=b;for(int n=0;n<8;++n)broken[0xea8000+28+40+n]=char(0xff);rejects([&]{insyde::validate(broken,0,"AuthenticAMD");});
 std::cout<<"PASS Insyde: generic admission, immutable hashes, relocated 8/32MiB maps, original Honor16MiB/HFDM/PSP/"<<verified.shape.firmwareVolumes<<" FV, corrupt/truncated rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
