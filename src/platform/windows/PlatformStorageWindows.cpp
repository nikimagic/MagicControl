#include "MagicControl/HardwareInventory.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/FileStorage.h"

#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <windows.h>
#include <utility>

namespace mc {

std::shared_ptr<void> detail::lockStorageFile(const std::filesystem::path& path,bool wait,std::string& error){
    error.clear();
    try{
        std::shared_ptr<void> legacy;
        const auto old=path.parent_path().parent_path()/".locks";
        if(path.parent_path().filename()=="locks"&&std::filesystem::exists(old)){
            const auto attributes=GetFileAttributesW(old.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(attributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("Invalid legacy lock directory");
            legacy=lockStorageFile(old/path.filename(),wait,error);if(!legacy)return {};
        }
        std::filesystem::create_directories(path.parent_path());
        const auto h=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(h==INVALID_HANDLE_VALUE){error="Cannot open storage lock: Windows "+std::to_string(GetLastError());return {};}
        auto owner=std::shared_ptr<void>(h,[](void* value){CloseHandle(value);});
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(h,&info)||info.nNumberOfLinks!=1||
            (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))){error="Storage lock is not an ordinary file";return {};}
        OVERLAPPED offset{};
        if(!LockFileEx(h,LOCKFILE_EXCLUSIVE_LOCK|(wait?0:LOCKFILE_FAIL_IMMEDIATELY),0,MAXDWORD,MAXDWORD,&offset)){
            const auto code=GetLastError();if(wait||code!=ERROR_LOCK_VIOLATION)error="Cannot acquire storage lock: Windows "+std::to_string(code);
            return {};
        }
        if(legacy)return std::make_shared<std::pair<std::shared_ptr<void>,std::shared_ptr<void>>>(std::move(legacy),std::move(owner));
        return owner; // Closing the non-inheritable handle releases LockFileEx.
    }catch(const std::exception& e){error=e.what();return {};}
}
bool detail::writeStorageTextAtomically(const std::filesystem::path& target,std::string_view text,std::string& error){
    error.clear();std::filesystem::path temporary;bool created=false;
    try{
        if(!target.parent_path().empty())std::filesystem::create_directories(target.parent_path());
        auto lockName=target.filename();lockName+=L".publish.lock";
        const auto publication=lockStorageFile(target.parent_path()/"locks"/lockName,true,error);
        if(!publication)return false;
        static std::atomic<unsigned long long> next{};
        HANDLE h=INVALID_HANDLE_VALUE;
        for(unsigned attempt=0;attempt<128;++attempt){
            temporary=target;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++next);
            h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(h!=INVALID_HANDLE_VALUE){created=true;break;}
            if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)break;
        }
        if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create storage temporary: Windows "+std::to_string(GetLastError()));
        bool ok=true;DWORD code{};
        for(std::size_t done=0;done<text.size();){
            DWORD written{};const auto count=static_cast<DWORD>((std::min)(text.size()-done,std::size_t{1024*1024}));
            if(!WriteFile(h,text.data()+done,count,&written,nullptr)||written!=count){code=GetLastError();ok=false;break;}done+=written;
        }
        if(ok&&!FlushFileBuffers(h)){code=GetLastError();ok=false;}
        if(!CloseHandle(h)&&ok){code=GetLastError();ok=false;}
        if(!ok)throw std::runtime_error("Cannot write/flush storage temporary: Windows "+std::to_string(code));
        const auto utf8=[](const auto& path){const auto s=path.u8string();return std::string(reinterpret_cast<const char*>(s.data()),s.size());};
        if(replaceFileAtomically(utf8(temporary),utf8(target),error))return true;
    }catch(const std::exception& e){error=e.what();}
    if(created){std::error_code ignored;std::filesystem::remove(temporary,ignored);}
    return false;
}

bool detail::appendHistoryRow(const std::filesystem::path& path,std::string_view row,std::string& error){
    std::filesystem::create_directories(path.parent_path());
    const auto handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE){error="Cannot open identity history: Windows "+std::to_string(GetLastError());return false;}
    struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{handle};
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))){
        error="Identity history is not an ordinary file";return false;}
    OVERLAPPED offset{};
    if(!LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK,0,MAXDWORD,MAXDWORD,&offset)){
        error="Cannot lock identity history: Windows "+std::to_string(GetLastError());return false;}
    struct Unlock{HANDLE h;OVERLAPPED& offset;~Unlock(){UnlockFileEx(h,0,MAXDWORD,MAXDWORD,&offset);}} unlock{handle,offset};
    DWORD written{};LARGE_INTEGER end{};
    if(!SetFilePointerEx(handle,end,nullptr,FILE_END)||!WriteFile(handle,row.data(),static_cast<DWORD>(row.size()),&written,nullptr)||written!=row.size()||!FlushFileBuffers(handle)){
        error="Cannot append/flush identity history: Windows "+std::to_string(GetLastError());return false;}
    return true;
}

bool replaceFileAtomically(const std::string& source, const std::string& destination,
                           std::string& error) {
    const auto fromText = std::u8string(reinterpret_cast<const char8_t*>(source.data()),
                                        reinterpret_cast<const char8_t*>(source.data()) + source.size());
    const auto toText = std::u8string(reinterpret_cast<const char8_t*>(destination.data()),
                                      reinterpret_cast<const char8_t*>(destination.data()) + destination.size());
    const std::filesystem::path from(fromText);
    const std::filesystem::path to(toText);
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    error = "Не удалось заменить файл сведений: код Windows " + std::to_string(GetLastError());
    return false;
}

}
