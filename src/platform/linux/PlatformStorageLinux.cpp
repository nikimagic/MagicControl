#include "MagicControl/HardwareInventory.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/FileStorage.h"
#include <utility>

#include <atomic>
#include <stdexcept>
#include <filesystem>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mc {

std::shared_ptr<void> detail::lockStorageFile(const std::filesystem::path& path,bool wait,std::string& error){
    error.clear();
    try{
        std::shared_ptr<void> legacy;
        const auto old=path.parent_path().parent_path()/".locks";
        if(path.parent_path().filename()=="locks"&&std::filesystem::exists(old)){
            if(std::filesystem::is_symlink(std::filesystem::symlink_status(old))||!std::filesystem::is_directory(old))throw std::runtime_error("Invalid legacy lock directory");
            legacy=lockStorageFile(old/path.filename(),wait,error);if(!legacy)return {};
        }
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
        struct Lease {int fd{-1};~Lease(){if(fd>=0)::close(fd);}};
        auto owner=std::make_shared<Lease>();
        auto& fd=owner->fd;
        fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if(fd<0&&errno==ENOENT){
            fd=::open(path.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0644);
            if(fd<0&&errno==EEXIST)fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        }
        if(fd<0){error="Cannot open storage lock: "+std::string(std::strerror(errno));return {};}
        struct stat info{};if(::fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1){error="Storage lock is not an ordinary file";return {};}
        int result;do{result=::flock(fd,LOCK_EX|(wait?0:LOCK_NB));}while(result&&errno==EINTR);
        if(result){if(wait||(errno!=EWOULDBLOCK&&errno!=EAGAIN))error="Cannot acquire storage lock: "+std::string(std::strerror(errno));return {};}
        if(legacy)return std::make_shared<std::pair<std::shared_ptr<void>,std::shared_ptr<void>>>(std::move(legacy),std::move(owner));
        return owner;
    }catch(const std::exception& e){error=e.what();return {};}
}
bool detail::writeStorageTextAtomically(const std::filesystem::path& target,std::string_view text,std::string& error){
    error.clear();std::filesystem::path temporary;bool created=false;
    try{
        if(!target.parent_path().empty())std::filesystem::create_directories(target.parent_path());
        auto lockName=target.filename();lockName+=".publish.lock";
        const auto publication=lockStorageFile(target.parent_path()/"locks"/lockName,true,error);
        if(!publication)return false;
        static std::atomic<unsigned long long> next{};
        int fd=-1;
        for(unsigned attempt=0;attempt<128;++attempt){
            temporary=target;temporary+=".tmp-"+std::to_string(::getpid())+"-"+std::to_string(++next);
            fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0666);
            if(fd>=0){created=true;break;}if(errno!=EEXIST)break;
        }
        if(fd<0)throw std::runtime_error("Cannot create storage temporary: "+std::string(std::strerror(errno)));
        bool ok=true;int code{};
        for(std::size_t done=0;done<text.size();){
            const auto n=::write(fd,text.data()+done,text.size()-done);
            if(n<0&&errno==EINTR)continue;
            if(n<=0){code=errno;ok=false;break;}
            done+=static_cast<std::size_t>(n);
        }
        if(ok&&::fsync(fd)){code=errno;ok=false;}
        if(::close(fd)&&ok){code=errno;ok=false;}
        if(!ok)throw std::runtime_error("Cannot write/flush storage temporary: "+std::string(std::strerror(code)));
        if(replaceFileAtomically(temporary.string(),target.string(),error))return true;
    }catch(const std::exception& e){error=e.what();}
    if(created){std::error_code ignored;std::filesystem::remove(temporary,ignored);}
    return false;
}

bool detail::appendHistoryRow(const std::filesystem::path& path,std::string_view row,std::string& error){
    std::filesystem::create_directories(path.parent_path());
    const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0){error="Cannot open identity history: "+std::string(std::strerror(errno));return false;}
    struct Close{int fd;~Close(){::close(fd);}} close{fd};
    struct stat info{};
    if(::fstat(fd,&info)||!S_ISREG(info.st_mode)){error="Identity history is not an ordinary file";return false;}
    if(::flock(fd,LOCK_EX)){error="Cannot lock identity history: "+std::string(std::strerror(errno));return false;}
    struct Unlock{int fd;~Unlock(){::flock(fd,LOCK_UN);}} unlock{fd};
    for(std::size_t done=0;done<row.size();){
        const auto n=::write(fd,row.data()+done,row.size()-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){error="Cannot append identity history: "+std::string(std::strerror(errno));return false;}
        done+=static_cast<std::size_t>(n);
    }
    if(::fsync(fd)){error="Cannot flush identity history: "+std::string(std::strerror(errno));return false;}
    return true;
}

bool replaceFileAtomically(const std::string& source, const std::string& destination,
                           std::string& error) {
    std::error_code status;
    const auto fromText = std::u8string(reinterpret_cast<const char8_t*>(source.data()),
                                        reinterpret_cast<const char8_t*>(source.data()) + source.size());
    const auto toText = std::u8string(reinterpret_cast<const char8_t*>(destination.data()),
                                      reinterpret_cast<const char8_t*>(destination.data()) + destination.size());
    std::filesystem::rename(std::filesystem::path(fromText), std::filesystem::path(toText), status);
    if (!status) return true;
    error = "Не удалось заменить файл сведений: " + status.message();
    return false;
}

}
