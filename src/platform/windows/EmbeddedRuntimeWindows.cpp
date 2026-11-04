#include "EmbeddedRuntimeWindows.h"
#include "BinaryHashWindows.h"
#include "MagicControl/EmbeddedRuntimeCatalog.h"
#include <windows.h>
#include <shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <array>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace mc::windows {
namespace {
[[noreturn]] void failure(const char* what){throw std::runtime_error(std::string("Embedded runtime: ")+what+" Win32="+std::to_string(GetLastError()));}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    HANDLE release(){const auto h=value;value=INVALID_HANDLE_VALUE;return h;}
};
bool trustedSid(PSID sid){return sid&&(IsWellKnownSid(sid,WinBuiltinAdministratorsSid)||IsWellKnownSid(sid,WinLocalSystemSid));}
struct Security {
    PSECURITY_DESCRIPTOR descriptor{};
    Security(){if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr))failure("cache security descriptor");}
    ~Security(){LocalFree(descriptor);}
};
struct Cache {
    std::mutex mutex;
    std::map<std::string,std::filesystem::path> groups;
    std::vector<HANDLE> pinned;
    void directory(const std::filesystem::path& path){
        Security sd;SECURITY_ATTRIBUTES attributes{sizeof(attributes),sd.descriptor,FALSE};
        if(!CreateDirectoryW(path.c_str(),&attributes)&&GetLastError()!=ERROR_ALREADY_EXISTS)failure("create protected cache directory");
        Handle handle{CreateFileW(path.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
        if(handle.value==INVALID_HANDLE_VALUE)failure("open protected cache directory (administrator required)");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(handle.value,&info))failure("cache directory information");
        if((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("Embedded runtime: cache directory is a link or not a directory");
        PSID owner{};PACL dacl{};PSECURITY_DESCRIPTOR actual{};
        const DWORD status=GetSecurityInfo(handle.value,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&dacl,nullptr,&actual);
        if(status!=ERROR_SUCCESS){SetLastError(status);failure("cache directory security");}
        struct Free { PSECURITY_DESCRIPTOR p;~Free(){LocalFree(p);} } cleanup{actual};
        if(!trustedSid(owner)||!dacl||dacl->AceCount!=2)throw std::runtime_error("Embedded runtime: cache ownership/ACL is not trusted");
        for(DWORD i=0;i<dacl->AceCount;++i){void* raw{};
            if(!GetAce(dacl,i,&raw))failure("cache ACE");
            const auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);
            if(ace->Header.AceType!=ACCESS_ALLOWED_ACE_TYPE||ace->Mask!=FILE_ALL_ACCESS||!trustedSid(const_cast<DWORD*>(&ace->SidStart)))
                throw std::runtime_error("Embedded runtime: cache permits an untrusted identity");
        }
        pinned.push_back(handle.release()); // Prevent directory rename while providers use its files.
    }
    void file(const std::filesystem::path& path,std::span<const char> bytes,std::string_view expected){
        if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){
            if(GetLastError()!=ERROR_FILE_NOT_FOUND)failure("cache file attributes");
            GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Embedded runtime: temporary file identity unavailable");
            wchar_t suffix[40]{};StringFromGUID2(id,suffix,40);
            auto temporary=path;temporary+=suffix;
            struct Cleanup {std::filesystem::path path;~Cleanup(){DeleteFileW(path.c_str());}} cleanup{temporary};
            Handle output{CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
            if(output.value==INVALID_HANDLE_VALUE)failure("create cache file");
            DWORD written{};
            if(!WriteFile(output.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(output.value))failure("write cache file");
            CloseHandle(output.release());
            if(!MoveFileW(temporary.c_str(),path.c_str())&&GetLastError()!=ERROR_ALREADY_EXISTS&&GetLastError()!=ERROR_FILE_EXISTS)failure("publish cache file");
        }
        Handle input{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
        if(input.value==INVALID_HANDLE_VALUE)failure("pin cache file");
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        if(!GetFileInformationByHandle(input.value,&info)||!GetFileSizeEx(input.value,&size))failure("cache file information");
        if(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)||size.QuadPart!=bytes.size())throw std::runtime_error("Embedded runtime: cache file type/size mismatch");
        std::vector<char> data(bytes.size());DWORD read{};
        if(!ReadFile(input.value,data.data(),static_cast<DWORD>(data.size()),&read,nullptr)||read!=data.size())failure("read pinned cache file");
        if(detail::binarySha256(data)!=expected)throw std::runtime_error("Embedded runtime: cache SHA256 mismatch");
        pinned.push_back(input.release()); // Keep verification and later DLL/module loading bound to the same bytes.
    }
};
// Pins intentionally live until process termination, including static provider teardown.
Cache& cache(){static auto* value=new Cache;return *value;}
const embedded::ResourceEntry& entry(std::string_view group,std::string_view name){
    for(const auto& file:embedded::files)if(file.group==group&&file.name==name)return file;
    throw std::runtime_error("Embedded runtime: unknown catalogue member");
}
std::filesystem::path root(){
    PWSTR raw{};if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData,KF_FLAG_DEFAULT,nullptr,&raw)))throw std::runtime_error("Embedded runtime: ProgramData unavailable");
    std::filesystem::path result=raw;CoTaskMemFree(raw);return result/L"MagicControlRuntime";
}
}
std::span<const char> embeddedRuntimeBytes(std::string_view group,std::string_view name){
    const auto& file=entry(group,name);const auto executable=GetModuleHandleW(nullptr);
    const auto resource=FindResourceW(executable,MAKEINTRESOURCEW(file.id),MAKEINTRESOURCEW(10));
    if(!resource)failure("resource missing");
    const auto size=SizeofResource(executable,resource);
    const auto* data=static_cast<const char*>(LockResource(LoadResource(executable,resource)));
    if(!data||size!=file.bytes)throw std::runtime_error("Embedded runtime: resource size mismatch");
    const std::span<const char> bytes{data,size};
    if(detail::binarySha256(bytes)!=file.sha256)throw std::runtime_error("Embedded runtime: resource SHA256 mismatch");
    return bytes;
}
std::filesystem::path embeddedRuntimeCachePath(std::string_view group){
    bool known{};for(const auto& file:embedded::files)if(file.group==group)known=true;
    if(!known)throw std::runtime_error("Embedded runtime: unknown group");
    return root()/std::string(embedded::bundleHash)/std::string(group);
}
std::filesystem::path embeddedRuntimeDirectory(std::string_view group){
    auto& state=cache();std::lock_guard lock{state.mutex};
    if(const auto found=state.groups.find(std::string(group));found!=state.groups.end())return found->second;
    const auto directory=embeddedRuntimeCachePath(group);
    state.directory(root());state.directory(directory.parent_path());state.directory(directory);
    for(const auto& file:embedded::files){if(file.group!=group)continue;
        const auto path=directory/std::string(file.name);
        if(path.parent_path()!=directory)state.directory(path.parent_path());
        state.file(path,embeddedRuntimeBytes(file.group,file.name),file.sha256);
    }
    state.groups.emplace(group,directory);return directory;
}
}
