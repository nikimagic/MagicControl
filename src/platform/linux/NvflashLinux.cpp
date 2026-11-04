#include "../common/NvflashPlatform.h"
#include "../common/FirmwareEvidence.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>
#include <array>
#include <algorithm>
#include <stdexcept>
namespace mc::diagnostics::nvflash {
namespace {
namespace fs=std::filesystem;using Clock=std::chrono::steady_clock;
constexpr char toolHash[]="754693fb8f833be75601f95cb08a6e0cf538053cce84ab69020ec74ae96a6e70";
struct Fd {int fd{-1};~Fd(){if(fd>=0)close(fd);}};
int openAboveStdio(const fs::path& p,int flags,mode_t mode=0600){
    int fd=open(p.c_str(),flags,mode);
    if(fd>=0&&fd<3){const auto duplicate=fcntl(fd,F_DUPFD_CLOEXEC,3);close(fd);fd=duplicate;}
    return fd;
}
std::string q(std::string_view s){return firmwareQuote(s);}
std::vector<std::uint8_t> bytes(const fs::path& p){
    if(fs::file_size(p)!=15909600)throw std::runtime_error("Linux NVFlash size mismatch");
    std::ifstream f(p,std::ios::binary);std::vector<std::uint8_t> b(15909600);f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));
    if(!f||firmwareSha256(b)!=toolHash)throw std::runtime_error("Linux NVFlash SHA256 mismatch");return b;
}
fs::path tool(){return "/var/lib/MagicControl/readers/NvFlash-5.867/nvflash";}
void protectedDirectory(const fs::path& p){
    if(mkdir(p.c_str(),0700)&&errno!=EEXIST)throw std::runtime_error("Cannot create protected NVFlash directory");
    struct stat s{};if(lstat(p.c_str(),&s)||!S_ISDIR(s.st_mode)||s.st_uid!=0||(s.st_mode&022))throw std::runtime_error("NVFlash cache is not a root-owned protected directory");
}
void pinned(){
    struct stat s{};if(lstat(tool().c_str(),&s)||!S_ISREG(s.st_mode)||s.st_uid!=0||(s.st_mode&022)||s.st_nlink!=1)throw std::runtime_error("Unsafe Linux NVFlash cache member");bytes(tool());
}
void syncFile(int fd){if(fsync(fd))throw std::runtime_error("Cannot flush VBIOS file");}
}
void atomicText(const fs::path& p,const std::string& text){
    auto temporary=p;temporary+=".tmp";
    Fd f{open(temporary.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600)};if(f.fd<0)throw std::runtime_error("Cannot open VBIOS metadata");
    std::size_t at{};while(at<text.size()){const auto n=write(f.fd,text.data()+at,text.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("Cannot write VBIOS metadata");at+=static_cast<std::size_t>(n);}syncFile(f.fd);
    if(rename(temporary.c_str(),p.c_str()))throw std::runtime_error("Cannot commit VBIOS metadata");
}
std::shared_ptr<void> preflight(const fs::path& artifacts,const Event& event){
    event("privilege_preflight","{\"effective_uid\":"+std::to_string(geteuid())+"}");
    if(geteuid()!=0)throw std::runtime_error("NVFlash requires a root session; no sudo/polkit dialog is launched");
    for(const auto& p:{fs::path("/var/lib/MagicControl"),fs::path("/var/lib/MagicControl/readers"),tool().parent_path()})protectedDirectory(p);
    auto* descriptor=new int(open((tool().parent_path()/"capture.lock").c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600));
    std::shared_ptr<void> lease(descriptor,[](void* p){auto* f=static_cast<int*>(p);if(*f>=0)close(*f);delete f;});
    if(*descriptor>=0&&*descriptor<3){const auto duplicate=fcntl(*descriptor,F_DUPFD_CLOEXEC,3);close(*descriptor);*descriptor=duplicate;}
    if(*descriptor<0||flock(*descriptor,LOCK_EX|LOCK_NB))throw std::runtime_error("Another VBIOS reader is active or the lease is unavailable");
    if(fs::exists("/sys/module/nvtool"))throw std::runtime_error("An existing nvtool module is loaded; reader will not take ownership of it");
    if(!fs::exists(tool())){
        std::array<char,4096> executable{};const auto n=readlink("/proc/self/exe",executable.data(),executable.size());if(n<=0||static_cast<std::size_t>(n)>=executable.size())throw std::runtime_error("Cannot locate Linux portable executable");
        const auto source=fs::path(std::string(executable.data(),static_cast<std::size_t>(n))).parent_path()/"tools/NvFlash/nvflash";
        const auto data=bytes(source);Fd f{open(tool().c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0500)};if(f.fd<0)throw std::runtime_error("Cannot create pinned Linux NVFlash cache member");
        std::size_t at{};while(at<data.size()){const auto count=write(f.fd,data.data()+at,data.size()-at);if(count<0&&errno==EINTR)continue;if(count<=0)throw std::runtime_error("Cannot stage Linux NVFlash");at+=static_cast<std::size_t>(count);}syncFile(f.fd);
    }
    pinned();
    const auto metadata="{\"version\":\"5.867.0\",\"platform\":\"Linux x86_64 ELF\",\"bytes\":15909600,\"sha256\":"+q(toolHash)+",\"source\":\"https://www.computerbase.de/downloads/systemtools/grafikkarten/nvflash/\",\"archive_sha256\":\"73ab51e121129059f1377b3bf21a8001f6649d64af2471455499c803a9c80301\",\"protected_cache\":"+q(tool().string())+",\"module_unload_requested\":false}";
    atomicText(artifacts/"tool.json",metadata+"\n");event("tool_verified",metadata);return lease;
}
unsigned run(Command command,unsigned index,const fs::path& output,const fs::path& artifacts,const std::string& label,
    const Event& event,std::stop_token token,std::atomic<std::uint64_t>* progress,std::uint64_t completedBytes,const std::shared_ptr<void>& lease){
    if(index>255)throw std::runtime_error("Invalid NVFlash index");pinned();if(token.stop_requested())throw std::runtime_error("VBIOS cancelled before command");
    if(fs::exists("/sys/module/nvtool"))throw std::runtime_error("An nvtool module exists before this command; reader will not unload or reuse it");
    Fd out{openAboveStdio(artifacts/(label+".stdout.log"),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC)},err{openAboveStdio(artifacts/(label+".stderr.log"),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC)},input{openAboveStdio("/dev/null",O_RDONLY|O_CLOEXEC)};
    if(out.fd<0||err.fd<0||input.fd<0)throw std::runtime_error("Cannot create NVFlash raw logs");
    const auto exe=tool().string(),cwd=artifacts.string(),outputArg=output.string();
    auto values=arguments(command,index,outputArg);values.insert(values.begin(),exe);
    std::vector<char*> argv;for(auto& value:values)argv.push_back(value.data());argv.push_back(nullptr);
    std::string argumentLog="[";for(const auto& value:values){if(argumentLog.size()>1)argumentLog+=',';argumentLog+=q(value);}argumentLog+=']';
    std::array<char*,4> env{const_cast<char*>("PATH=/usr/sbin:/usr/bin:/sbin:/bin"),const_cast<char*>("LC_ALL=C"),const_cast<char*>("LANG=C"),nullptr};
    const int leaseFd=*static_cast<int*>(lease.get());
    if(leaseFd<3)throw std::runtime_error("Invalid inherited NVFlash lease descriptor");
    event("vendor_requested","{\"exe\":"+q(exe)+",\"operation\":"+q(commandName(command))+",\"index\":"+std::to_string(index)+",\"argv\":"+argumentLog+",\"output\":"+q(outputArg)+",\"isolated_session\":true,\"lease_inherited_by_reader\":true,\"flash_programming_requested\":false}");
    event("vendor_raw_files","{\"label\":"+q(label)+",\"stdout\":"+q((artifacts/(label+".stdout.log")).string())+",\"stderr\":"+q((artifacts/(label+".stderr.log")).string())+"}");
    const auto begin=Clock::now();const auto pid=fork();if(pid<0)throw std::runtime_error("Cannot fork NVFlash reader");
    if(pid==0){
        if(dup2(input.fd,0)<0||dup2(out.fd,1)<0||dup2(err.fd,2)<0||chdir(cwd.c_str())||setsid()<0){const char message[]="NVFlash reader stdio/cwd/session setup failed\n";write(2,message,sizeof(message)-1);_exit(126);}
        // A detached session receives neither the SSH terminal's SIGINT nor
        // its controlling TTY. The root-owned flock remains held by the child
        // even if the frontend is killed/disconnected during this read.
        if(fcntl(leaseFd,F_SETFD,0)||(leaseFd>3&&syscall(SYS_close_range,3u,static_cast<unsigned>(leaseFd-1),0u))||syscall(SYS_close_range,static_cast<unsigned>(leaseFd+1),~0u,0u)){const char message[]="NVFlash reader descriptor isolation failed\n";write(2,message,sizeof(message)-1);_exit(126);}
        execve(exe.c_str(),argv.data(),env.data());const char message[]="NVFlash reader execve failed\n";write(2,message,sizeof(message)-1);_exit(127);
    }
    struct Pending {pid_t pid;bool reaped{};std::shared_ptr<void> lease;~Pending(){if(!reaped)std::thread([p=pid,guard=lease]{int status{};while(waitpid(p,&status,0)<0&&errno==EINTR){} }).detach();}} pending{pid,false,lease};
    event("vendor_started","{\"operation\":"+q(commandName(command))+",\"pid\":"+std::to_string(pid)+",\"label\":"+q(label)+"}");bool cancellationLogged{};int status{};
    for(;;){
        const auto waited=waitpid(pid,&status,WNOHANG);if(waited==pid){pending.reaped=true;break;}if(waited<0&&errno!=EINTR)throw std::runtime_error("NVFlash waitpid failed");
        if(saves(command)&&progress){std::error_code ec;const auto n=fs::file_size(output,ec);if(!ec)progress->store(completedBytes+std::min<std::uint64_t>(n,64*1024*1024));}
        if(token.stop_requested()&&!cancellationLogged){event("cancel_pending","{\"reason\":\"Finish current reader before cancellation\"}");cancellationLogged=true;}
        struct stat a{},b{};if(fstat(out.fd,&a)||fstat(err.fd,&b))throw std::runtime_error("Cannot inspect NVFlash raw log sizes");
        if(Clock::now()-begin>std::chrono::minutes(10)||a.st_size+b.st_size>32*1024*1024){event("vendor_process_pending","{\"pid\":"+std::to_string(pid)+",\"forcibly_terminated\":false}");throw std::runtime_error("NVFlash reader still running; PID and raw output retained; capture lease held until exit");}
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    syncFile(out.fd);syncFile(err.fd);const unsigned code=WIFEXITED(status)?static_cast<unsigned>(WEXITSTATUS(status)):128u+static_cast<unsigned>(WTERMSIG(status));
    event("vendor_returned","{\"label\":"+q(label)+",\"exit_code\":"+std::to_string(code)+",\"elapsed_ms\":"+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count())+"}");return code;
}
}
