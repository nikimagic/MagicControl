#include "Platform.h"
#include "UpdateScript.h"
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace mc::update::platform {
namespace {
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0&&!s.empty())throw std::runtime_error("Неверный UTF-8");std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;}
std::string utf8(const std::wstring& w){int n=WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr);return s;}
struct Handle{HANDLE h{};~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
std::wstring quote(const std::wstring& s){std::wstring out=L"\"";unsigned slashes=0;for(wchar_t c:s){if(c==L'\\'){++slashes;continue;}if(c==L'"')out.append(slashes*2+1,L'\\');else out.append(slashes,L'\\');slashes=0;out+=c;}out.append(slashes*2,L'\\');return out+L'"';}
std::wstring command(const std::vector<std::string>& args){std::wstring out;for(const auto& a:args){if(!out.empty())out+=L' ';out+=quote(wide(a));}return out;}
std::string systemTool(const wchar_t* name){wchar_t dir[MAX_PATH]{};if(!GetSystemDirectoryW(dir,MAX_PATH))throw std::runtime_error("Нет системного каталога Windows");return utf8((std::filesystem::path(dir)/name).wstring());}
std::string psQuote(std::string s){std::string out="'";for(char c:s){out+=c;if(c=='\'')out+='\'';}return out+"'";}
}
std::filesystem::path executable(){std::wstring path(32768,0);const DWORD n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));if(!n||n>=path.size())throw std::runtime_error("Не определён путь программы");path.resize(n);return path;}
std::string curl(){return systemTool(L"curl.exe");}
std::string pid(){return std::to_string(GetCurrentProcessId());}
void replace(const std::filesystem::path& from,const std::filesystem::path& to){
    // A signalled process may still be releasing its image section or an AV
    // reader. Retry only bounded sharing/access failures; no forced exit/ACL edit.
    DWORD error{};const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do{if(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return;
        error=GetLastError();if(error!=ERROR_ACCESS_DENIED&&error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION)break;
        if(std::chrono::steady_clock::now()>=deadline)break;Sleep(100);
    }while(true);
    throw std::runtime_error("Не удалось заменить файл "+utf8(to.wstring())+": "+std::to_string(error));
}
std::string run(const std::vector<std::string>& args,std::stop_token stop,int timeout){
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE readPipe{},writePipe{};
    if(!CreatePipe(&readPipe,&writePipe,&sa,0))throw std::runtime_error("Ошибка канала обновления");
    Handle input{readPipe},output{writePipe};SetHandleInformation(input.h,HANDLE_FLAG_INHERIT,0);
    Handle nullIn{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr)};
    STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdInput=nullIn.h;si.hStdOutput=si.hStdError=output.h;
    PROCESS_INFORMATION pi{};auto line=command(args);
    if(!CreateProcessW(wide(args.front()).c_str(),line.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))throw std::runtime_error("Не удалось запустить системный инструмент обновления: "+std::to_string(GetLastError()));
    Handle process{pi.hProcess},thread{pi.hThread};CloseHandle(output.h);output.h=nullptr;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(timeout);std::string result;
    for(;;){
        DWORD available{};while(PeekNamedPipe(input.h,nullptr,0,nullptr,&available,nullptr)&&available){char buffer[8192];DWORD got{};if(!ReadFile(input.h,buffer,std::min<DWORD>(available,sizeof(buffer)),&got,nullptr))break;result.append(buffer,got);if(result.size()>4*1024*1024){TerminateProcess(process.h,1);WaitForSingleObject(process.h,5000);throw std::runtime_error("Ответ обновления слишком большой");}}
        const bool done=WaitForSingleObject(process.h,30)==WAIT_OBJECT_0;
        if(done){DWORD left{};if(PeekNamedPipe(input.h,nullptr,0,nullptr,&left,nullptr)&&left)continue;break;}
        if(stop.stop_requested()||std::chrono::steady_clock::now()>deadline){TerminateProcess(process.h,1);WaitForSingleObject(process.h,5000);throw std::runtime_error(stop.stop_requested()?"Обновление отменено":"Сервер или инструмент обновления не ответил вовремя");}
    }
    DWORD code{};GetExitCodeProcess(process.h,&code);
    if(code)throw std::runtime_error("Ошибка загрузки/подготовки пакета ("+std::to_string(code)+"). "+result.substr(0,500));
    return result;
}
void launch(const std::filesystem::path& exe,const std::vector<std::string>& args){
    std::vector<std::string> all{utf8(exe.wstring())};all.insert(all.end(),args.begin(),args.end());auto line=command(all);
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi))throw std::runtime_error("Не удалось запустить помощник обновления");
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
}
void launchInstaller(const std::filesystem::path& root,const std::filesystem::path& stage){
    const auto script=stage/"apply-update.ps1";
    {std::ofstream file(script,std::ios::binary);file<<installerScript;file.close();
        if(!file)throw std::runtime_error("Не удалось подготовить скрипт обновления");}
    // Read the complete fixed script into memory before executing it, so the
    // script can remove its own staging file without a second cleanup process.
    const auto command="& ([ScriptBlock]::Create([IO.File]::ReadAllText("+psQuote(utf8(script.wstring()))+"))) "+
        psQuote(utf8(root.wstring()))+" "+psQuote(utf8(stage.wstring()));
    launch(std::filesystem::path(wide(systemTool(L"WindowsPowerShell/v1.0/powershell.exe"))),
        {"-NoProfile","-NonInteractive","-WindowStyle","Hidden","-Command",command});
}
bool waitForParent(const std::string& value){
    if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)return false;
    const auto id=std::stoul(value);if(!id||id==GetCurrentProcessId())return false;
    Handle parent{OpenProcess(SYNCHRONIZE,FALSE,id)};if(!parent.h)return GetLastError()==ERROR_INVALID_PARAMETER;
    return WaitForSingleObject(parent.h,120000)==WAIT_OBJECT_0;
}
bool linkLike(const std::filesystem::path& path){
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
}
void removeEntry(const std::filesystem::path& path){
    const auto attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Не удалось прочитать файл очистки");
    const bool readOnly=(attributes&FILE_ATTRIBUTE_READONLY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
    if(readOnly)if(!SetFileAttributesW(path.c_str(),attributes&~FILE_ATTRIBUTE_READONLY))
        throw std::runtime_error("Не удалось очистить временный файл только для чтения");
    if((attributes&FILE_ATTRIBUTE_DIRECTORY)?!RemoveDirectoryW(path.c_str()):!DeleteFileW(path.c_str())){
        const auto error=GetLastError();
        if(readOnly)SetFileAttributesW(path.c_str(),attributes);
        throw std::runtime_error("Не удалось удалить файл обновления: "+utf8(path.wstring())+" ("+std::to_string(error)+")");
    }
}
bool waitForUpdateHelpers(const std::filesystem::path& folder){
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    if(snapshot.h==INVALID_HANDLE_VALUE){snapshot.h=nullptr;return false;}
    const auto prefix=std::filesystem::canonical(folder).wstring()+L"\\";
    PROCESSENTRY32W entry{sizeof(entry)};const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    if(!Process32FirstW(snapshot.h,&entry))return false;
    do{
        if(entry.th32ProcessID==GetCurrentProcessId()||_wcsicmp(entry.szExeFile,L"MagicControl.Update.exe"))continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID)};
        if(!process.h){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;return false;}
        std::wstring image(32768,0);DWORD length=static_cast<DWORD>(image.size());
        if(!QueryFullProcessImageNameW(process.h,0,image.data(),&length))return false;image.resize(length);
        if(image.size()<=prefix.size()||_wcsnicmp(image.c_str(),prefix.c_str(),prefix.size()))continue;
        const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        if(left<=0||WaitForSingleObject(process.h,static_cast<DWORD>(left))!=WAIT_OBJECT_0)return false;
    }while(Process32NextW(snapshot.h,&entry));
    return true;
}
std::string sha256(const std::filesystem::path& path){
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};
    struct Cleanup{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{alg,hash};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA256 недоступен");
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Не удалось прочитать пакет");
    std::array<char,65536> buffer{};while(f){f.read(buffer.data(),buffer.size());if(f.gcount()&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(f.gcount()),0)<0)throw std::runtime_error("Ошибка SHA256");}
    if(!f.eof())throw std::runtime_error("Ошибка чтения SHA256");std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)throw std::runtime_error("Ошибка SHA256");
    std::string text;for(auto c:digest){text+="0123456789abcdef"[c>>4];text+="0123456789abcdef"[c&15];}return text;
}
void extract(const std::filesystem::path& archive,const std::filesystem::path& destination,std::stop_token stop){
    // Validate the whole ZIP before extraction: paths, Unix links, entry count
    // and total uncompressed size. Data is passed as quoted arguments, never code.
    const std::string script=R"PS(& { param($zip,$out)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$a=[IO.Compression.ZipFile]::OpenRead($zip)
try {
 $total=0L
 if($a.Entries.Count -gt 1000){throw 'Too many ZIP entries'}
 foreach($e in $a.Entries){
  $n=$e.FullName
  if(!$n -or $n.StartsWith('/') -or $n.Contains('\') -or $n.Contains(':') -or (($n.Split('/')) -contains '..') -or (($n.Split('/')) -contains '.')){throw 'Unsafe ZIP path'}
  if((($e.ExternalAttributes -shr 16) -band 61440) -eq 40960){throw 'ZIP links forbidden'}
  $total += $e.Length
  if($total -gt 268435456){throw 'ZIP too large'}
 }
} finally {$a.Dispose()}
[IO.Compression.ZipFile]::ExtractToDirectory($zip,$out)
} )PS";
    run({systemTool(L"WindowsPowerShell/v1.0/powershell.exe"),"-NoProfile","-NonInteractive","-Command",script+psQuote(utf8(archive.wstring()))+" "+psQuote(utf8(destination.wstring()))},stop,120);
}
}
