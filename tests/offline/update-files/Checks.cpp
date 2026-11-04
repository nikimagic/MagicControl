#include "Platform.h"
#include <windows.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void write(const fs::path& p,const char* text){std::ofstream f(p);f<<text;check(bool(f),"fixture write failed");}
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
struct Handle{HANDLE h;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
int main(){try{
 const auto root=fs::path(MC_FILES_ROOT)/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));fs::create_directories(root.parent_path());check(fs::create_directory(root),"existing fixture refused");
 const auto from=root/"next.txt",to=root/"installed.txt";write(from,"new");write(to,"old");
 HANDLE lock=CreateFileW(to.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);check(lock!=INVALID_HANDLE_VALUE,"cannot lock fixture");
 std::jthread release([=]{Sleep(220);CloseHandle(lock);});const auto started=std::chrono::steady_clock::now();mc::update::platform::replace(from,to);release.join();
 check(read(to)=="new"&&!fs::exists(from),"transient replacement failed");check(std::chrono::steady_clock::now()-started<std::chrono::seconds(2),"transient lock waited too long");
 write(from,"new2");Handle held{CreateFileW(to.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)};check(held.h!=INVALID_HANDLE_VALUE,"cannot hold fixture");bool failed{};
 const auto begin=std::chrono::steady_clock::now();try{mc::update::platform::replace(from,to);}catch(const std::exception& e){failed=std::string(e.what()).find("installed.txt")!=std::string::npos;}
 const auto duration=std::chrono::steady_clock::now()-begin;check(failed&&duration>=std::chrono::seconds(2)&&duration<std::chrono::seconds(5),"permanent lock not bounded or missing target in error");
 check(read(to)=="new"&&read(from)=="new2","failed replacement damaged data");
 std::cout<<"PASS real Windows updater replace: transient sharing lock retry, bounded permanent lock failure, target error path, original/source bytes retained; hardware/apps/network=0\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
