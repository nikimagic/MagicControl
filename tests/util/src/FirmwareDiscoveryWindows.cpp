// Explicit owner-requested discovery experiment. No product startup/restore.
// No ECRW, mailbox, PWUP, UEFI setter, fan/profile/lighting or driver installation.
#include "Json.h"
#include "MagicControl/IEcBackend.h"
#include "BackendWindows.h"
#include "uniwill-acpi/ReviewedDriverWindows.h"
#include <windows.h>
#include <setupapi.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs=std::filesystem;
using probe::Json;
namespace review=mc::windows::reviewed_uniwill;
std::string text(const std::wstring& v){return review::text(v);}
std::string utc(){return std::format("{:%FT%TZ}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));}
void save(const fs::path& p,const Json& j){std::ofstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot save report");f<<j.dump()<<'\n';if(!f)throw std::runtime_error("Report write failed");}
std::string hex(unsigned n){return std::format("0x{:X}",n);}
Json identity(){
 Json j;constexpr auto k=L"HARDWARE\\DESCRIPTION\\System\\BIOS";
 for(const auto name:{L"SystemManufacturer",L"SystemProductName",L"SystemFamily",L"BaseBoardManufacturer",L"BaseBoardProduct",L"BIOSVendor",L"BIOSVersion",L"BIOSReleaseDate"})
  j[text(name)]=text(review::registry(HKEY_LOCAL_MACHINE,k,name));
 j["source"]="Windows SMBIOS registry view; no DeviceRegistry/profile lookup";return j;
}
Json pnp(){
 Json result=Json::array();const auto h=SetupDiGetClassDevsW(nullptr,L"ACPI",nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);
 if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("PnP enumeration failed");
 struct Cleanup{HDEVINFO h;~Cleanup(){SetupDiDestroyDeviceInfoList(h);}} cleanup{h};
 SP_DEVINFO_DATA d{sizeof(d)};
 for(DWORD i=0;i<4096&&SetupDiEnumDeviceInfo(h,i,&d);++i){wchar_t id[2048]{};if(!SetupDiGetDeviceInstanceIdW(h,&d,id,2048,nullptr))continue;
  const auto prop=[&](DWORD key){wchar_t value[4096]{};DWORD type{};if(!SetupDiGetDeviceRegistryPropertyW(h,&d,key,&type,reinterpret_cast<BYTE*>(value),sizeof(value),nullptr))return std::string{};return text(value);};
  Json row{{"instance",text(id)},{"description",prop(SPDRP_DEVICEDESC)},{"service",prop(SPDRP_SERVICE)},{"hardware_id_first",prop(SPDRP_HARDWAREID)},{"driver_key",prop(SPDRP_DRIVER)}};
  if(!_wcsnicmp(id,L"ACPI\\INOU0000\\",14))row["uniwill_candidate"]=true;
  result.push(row);
 }return result;
}
Json acpi(const fs::path& output){
 const DWORD provider=0x41435049;DWORD size=EnumSystemFirmwareTables(provider,nullptr,0);
 if(!size||size>65536)throw std::runtime_error("ACPI table enumeration unavailable or exceeds limit");
 std::vector<DWORD> ids((size+3)/4);const auto got=EnumSystemFirmwareTables(provider,ids.data(),size);
 if(!got||got>size||got%4)throw std::runtime_error("ACPI enumeration changed/invalid");ids.resize(got/4);
 const DWORD dsdt=0x54445344;if(std::find(ids.begin(),ids.end(),dsdt)==ids.end())ids.push_back(dsdt);
 std::map<DWORD,int> occurrences;for(auto id:ids)++occurrences[id];
 Json result=Json::array();fs::create_directories(output);unsigned index{};
 for(const auto& [id,count]:occurrences){std::string sig(4,' ');memcpy(sig.data(),&id,4);if(sig=="MSDM"||sig=="SLIC")continue;
  Json row{{"signature_requested",sig},{"signature_occurrences",count},{"duplicate_instances_complete",count==1},{"source","GetSystemFirmwareTable; first instance per signature"}};
  const auto n=GetSystemFirmwareTable(provider,id,nullptr,0);if(!n||n>8*1024*1024){row["win32_error"]=GetLastError();result.push(row);continue;}
  std::vector<char> data(n);const auto actual=GetSystemFirmwareTable(provider,id,data.data(),n);
  if(!actual||actual>n){row["win32_error"]=GetLastError();result.push(row);continue;}data.resize(actual);
  const auto name=std::format("api-{:03}-{:08X}.aml",++index,id);std::ofstream f(output/name,std::ios::binary);f.write(data.data(),data.size());
  unsigned sum{};for(auto b:data)sum=(sum+static_cast<unsigned char>(b))&255;
  row["file"]=name;row["bytes"]=actual;row["checksum_sum_mod256"]=sum;
  row["sha256"]=mc::windows::detail::binarySha256(data);result.push(row);
 }return result;
}
Json readEC(bool smrwAdvertised){
 Json result{{"catalogue_consulted",false},{"board_allowlist_used",false},{"EC_configuration_writes",0},{"method_calls_are_getters_only",true}};
 std::string evidence,admission;bool driver=false;
 for(const auto pin:{"6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235","e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2","5b4208d762dad4fec1cfc66adcea29dbb3644e917ba77944306c390b90ce2a20"}){
  evidence.clear();try{review::verifyReviewedUniwillDriver(evidence,pin);driver=true;break;}catch(const std::exception& e){admission=e.what();}
 }
 result["installed_driver_evidence"]=evidence;result["driver_ABI_reviewed"]=driver;
 if(!driver){result["error"]=admission;return result;}
 auto ec=mc::createWindowsAcpiDiagnosticBackend();result["backend"]=ec->info().id;result["backend_writable"]=ec->info().writable;
 const auto sample=[&](unsigned address){Json row{{"address",hex(address)}};try{
  ec->lockTransaction();struct Unlock{mc::IEcBackend& ec;~Unlock(){ec.unlockTransaction();}} unlock{*ec};
  const auto a=ec->read(static_cast<std::uint16_t>(address)),b=ec->read(static_cast<std::uint16_t>(address));
  row["read1"]=a;row["read2"]=b;row["stable"]=a==b;row["status"]="read_completed";
 }catch(const std::exception& e){row["error"]=e.what();}return row;};
 Json fields=Json::array();for(auto a:{0x400u,0x454u,0x456u,0x49Fu,0x726u,0x73Cu,0x73Du,0x740u,0x741u,0x742u,0x74Cu,0x751u,0x765u,0x766u,0x767u,0x768u,0x770u,0x77Eu,0x77Fu,0x783u,0x784u,0x785u,0x78Cu,0x78Eu,0x7A4u,0x7A6u,0x7B9u,0x7C3u,0x7C5u,0x7C6u,0x7C7u,0x7C8u,0x7CAu,0x7D0u})fields.push(sample(a));result["candidate_fields"]=fields;
 Json defaults=Json::array();for(auto base:{0x730u,0x734u,0x7A7u,0x7D8u}){Json rows=Json::array();for(unsigned i=0;i<(base==0x7D8?3u:4u);++i)rows.push(sample(base+i));defaults.push(Json{{"base",hex(base)},{"bytes",rows}});}result["candidate_default_blocks"]=defaults;
 Json tables=Json::array();for(auto base:{0xF00u,0xF30u}){Json values=Json::array();for(unsigned i=0;i<48;++i)values.push(sample(base+i));tables.push(Json{{"base",hex(base)},{"bytes",values}});}result["current_ram_fan_tables"]=tables;
 result["ram_table_scope"]="Current RAM only, not claimed as factory ROM; no mailbox writes or table activation";
 result["smrw"]=Json{{"declaration_in_live_ACPI",smrwAdvertised},{"queried",false}};
 if(smrwAdvertised){
  const auto h=CreateFileW(L"\\\\.\\ACPIDriver",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
  if(h==INVALID_HANDLE_VALUE){result["smrw"]["win32_error"]=GetLastError();return result;}
  struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{h};Json replies=Json::array();
  ec->lockTransaction();struct Unlock{mc::IEcBackend& e;~Unlock(){e.unlockTransaction();}} unlock{*ec};
  for(unsigned char key=0;key<12;++key){
   // Independently reviewed OEM read ABI: BB,key. Never AA (write).
   const unsigned char in[]{0xBB,key};std::array<unsigned char,64> data;data.fill(0xA5);DWORD returned{};
   const auto ok=DeviceIoControl(h,0x9C40A500,const_cast<unsigned char*>(in),sizeof(in),data.data(),16,&returned,nullptr);const auto error=ok?0:GetLastError();
   bool guard=true;for(unsigned i=16;i<data.size();++i)guard=guard&&data[i]==0xA5;
   Json bytes=Json::array();for(unsigned i=0;i<std::min<DWORD>(returned,16);++i)bytes.push(data[i]);
   replies.push(Json{{"key",key},{"transport_succeeded",ok!=FALSE},{"win32_error",error},{"bytes_returned",returned},{"guard_intact",guard},{"raw_output",bytes},{"reply_not_overwritten",returned==0||data[0]==0xA5}});
   if(!guard||returned>16)throw std::runtime_error("SMRW response exceeds reviewed ABI");std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  result["smrw"]["queried"]=true;result["smrw"]["replies"]=replies;
  result["smrw"]["interpretation"]="Raw getters; successful transport/zeros alone do not prove semantic support. No PWUP or setters called.";
 }return result;
}
int wmain(int argc,wchar_t** argv){
 SetConsoleOutputCP(CP_UTF8);fs::path output;bool inventory=false,read=false,smrw=false;
 for(int i=1;i<argc;++i){const std::wstring a=argv[i];if(a==L"--out"&&i+1<argc)output=argv[++i];else if(a==L"--inventory")inventory=true;else if(a==L"--read")read=true;else if(a==L"--smrw-advertised")smrw=true;else return 2;}
 if(output.empty()||inventory==read)return 2;
 try{fs::create_directories(output);const auto file=output/(inventory?"inventory.json":"read-results.json");if(fs::exists(file))throw std::runtime_error("Report already exists; use a new run folder");
  Json result{{"utc",utc()},{"mode",inventory?"inventory":"read_only_protocol_candidates"},{"catalogue_consulted",false},{"configuration_writes_allowed",false},{"SMBIOS",identity()}};
  try{result["pnp_acpi"]=pnp();}catch(const std::exception& e){result["pnp_error"]=e.what();}
  if(inventory){try{result["acpi_tables"]=acpi(output/"acpi");}catch(const std::exception& e){result["acpi_error"]=e.what();}}
  else{try{result["EC"]=readEC(smrw);}catch(const std::exception& e){result["ec_error"]=e.what();}}
  save(file,result);std::cout<<"Saved "<<text(file.wstring())<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
