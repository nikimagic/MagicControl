#include "../common/FastDiagnosticWire.h"
#include "MagicControl/AsyncReaper.h"
#include <atomic>
#include <algorithm>
#include <thread>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
namespace mc::diagnostics {
namespace {
std::atomic<bool> helperRegistered{};
struct Socket{int value{-1};~Socket(){if(value>=0)::close(value);}void reset(int next=-1){if(value>=0)::close(value);value=next;}};
class Hardware final:public ReadOnlyDiagnosticHardware{
public:EcAccess* access()noexcept override{return nullptr;}FanMaximumSample fans()override{return {};}
};
class Worker final:public FastDiagnosticWorker{
    Socket socket_;pid_t child_{-1};unsigned failedPasses_{};
    std::shared_ptr<TelemetryGate::DiagnosticCollection> collection_;
    void stop()noexcept{
        socket_.reset();
        if(child_>0){
            // Only this fixed read-only child/process group is terminated.
            // Never signal the GUI, active fan commands or another user's PID.
            ::kill(-child_,SIGKILL);::kill(child_,SIGKILL);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            bool exited=false;
            while(std::chrono::steady_clock::now()<deadline){
                const auto value=::waitpid(child_,nullptr,WNOHANG);
                if(value==child_||(value<0&&errno==ECHILD)){exited=true;break;}
                if(value<0&&errno!=EINTR)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            // A kernel D-state can outlive the bounded wait. Retain the
            // diagnostic admission until a reaper confirms the helper exited.
            if(!exited&&collection_){
                const auto child=child_;auto collection=collection_;
                const auto reap=[child,collection]{for(;;){const auto result=::waitpid(child,nullptr,0);
                    if(result==child||(result<0&&errno==ECHILD))break;
                    if(result<0&&errno!=EINTR)std::this_thread::sleep_for(std::chrono::seconds(1));}};
                mc::detail::startReaper(reap);
            }
            child_=-1;
        }
    }
    void start(){
        if(!helperRegistered.load())throw std::runtime_error("Diagnostic worker early entry point was not registered");
        if(failedPasses_>=3)throw std::runtime_error("Read-only diagnostic worker restart limit reached; previous provider errors retained");
        int pair[2]{};if(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))throw std::runtime_error("Cannot create diagnostic socket: "+std::string(std::strerror(errno)));
        Socket parent{pair[0]},child{pair[1]};
        posix_spawn_file_actions_t actions{};const auto actionResult=posix_spawn_file_actions_init(&actions);
        if(actionResult)throw std::runtime_error("Cannot initialize diagnostic file actions");
        struct DestroyActions{posix_spawn_file_actions_t* p;~DestroyActions(){posix_spawn_file_actions_destroy(p);}} destroyActions{&actions};
        if(posix_spawn_file_actions_adddup2(&actions,child.value,STDIN_FILENO)||posix_spawn_file_actions_adddup2(&actions,child.value,STDOUT_FILENO)||
           posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0)||posix_spawn_file_actions_addclosefrom_np(&actions,3))throw std::runtime_error("Cannot restrict diagnostic inherited descriptors");
        posix_spawnattr_t attributes{};if(posix_spawnattr_init(&attributes))throw std::runtime_error("Cannot initialize diagnostic process attributes");
        struct DestroyAttributes{posix_spawnattr_t* p;~DestroyAttributes(){posix_spawnattr_destroy(p);}} destroyAttributes{&attributes};
        sigset_t empty;sigemptyset(&empty);
        if(posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP|POSIX_SPAWN_SETSIGMASK)||posix_spawnattr_setpgroup(&attributes,0)||posix_spawnattr_setsigmask(&attributes,&empty))throw std::runtime_error("Cannot isolate diagnostic process group");
        char exe[32768]{};const auto bytes=::readlink("/proc/self/exe",exe,sizeof(exe)-1);if(bytes<=0||bytes>=static_cast<ssize_t>(sizeof(exe)-1))throw std::runtime_error("Cannot resolve diagnostic executable");exe[bytes]=0;
        char argument[]="--diagnostic-fast-worker";char* arguments[]{exe,argument,nullptr};pid_t pid{};
        const auto result=::posix_spawn(&pid,exe,&actions,&attributes,arguments,environ);
        if(result)throw std::runtime_error("Cannot start read-only diagnostic worker: "+std::string(std::strerror(result)));
        child_=pid;socket_.value=parent.value;parent.value=-1;
        const auto flags=::fcntl(socket_.value,F_GETFL,0);if(flags<0||::fcntl(socket_.value,F_SETFL,flags|O_NONBLOCK)<0){stop();throw std::runtime_error("Cannot make diagnostic parent socket nonblocking");}
    }
    void receive(void* destination,std::size_t size,std::stop_token cancellation,std::chrono::steady_clock::time_point deadline){
        auto* bytes=static_cast<unsigned char*>(destination);std::size_t at{};
        while(at<size){
            if(cancellation.stop_requested())throw std::runtime_error("diagnostic provider pass cancelled; partial observations retained");
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("diagnostic provider deadline expired; partial observations retained");
            const auto got=::recv(socket_.value,bytes+at,size-at,MSG_DONTWAIT);
            if(got>0){at+=static_cast<std::size_t>(got);continue;}
            if(!got)throw std::runtime_error("diagnostic worker socket closed");
            if(errno==EINTR)continue;
            if(errno!=EAGAIN&&errno!=EWOULDBLOCK)throw std::runtime_error("diagnostic worker receive: "+std::string(std::strerror(errno)));
            pollfd p{socket_.value,POLLIN,0};const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(::poll(&p,1,static_cast<int>(std::max<std::int64_t>(1,std::min<std::int64_t>(left,50))))<0&&errno!=EINTR)throw std::runtime_error("diagnostic worker poll failed");
        }
    }
public:
    explicit Worker(std::shared_ptr<TelemetryGate::DiagnosticCollection> collection):collection_(std::move(collection)){}
    ~Worker()override{stop();}
    FirmwareCaptureResult firmware(bool stateOnly,const FirmwareEmitter& emit,std::stop_token cancellation,std::chrono::milliseconds timeout,bool biosRom)override{
        const auto begin=std::chrono::steady_clock::now(),deadline=begin+timeout;FirmwareCaptureResult result;std::size_t count{},total{};
        try{
            if(cancellation.stop_requested())throw std::runtime_error("firmware evidence cancelled before start");
            if(child_<=0)start();const unsigned char command=biosRom?wire::biosRomRequest:stateOnly?wire::firmwareStateRequest:wire::firmwareRequest;
            if(::send(socket_.value,&command,1,MSG_NOSIGNAL|MSG_DONTWAIT)!=1)throw std::runtime_error("cannot request firmware evidence");
            for(;;){const auto frameDeadline=biosRom?std::min(deadline,std::chrono::steady_clock::now()+std::chrono::seconds(60)):deadline;std::uint32_t size{};receive(&size,sizeof(size),cancellation,frameDeadline);
                if(!size||size>wire::maximumFrame)throw std::runtime_error("invalid firmware frame length");
                wire::Bytes body(size);receive(body.data(),body.size(),cancellation,frameDeadline);const auto record=wire::decodeFirmware(body);
                if(record.final){result.finished=record.success;result.status=record.status;break;}
                if(++count>=(biosRom?maximumBiosRomRecords:maximumFirmwareRecords)||record.bytes.size()>maximumFirmwareTotalBytes-total)throw std::runtime_error("firmware receive budget exceeded");
                total+=record.bytes.size();emit(record);
            }
        }catch(const std::exception& e){++failedPasses_;stop();result.status=e.what();}
        result.elapsedMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();return result;
    }
    FastDiagnosticFrame read(std::stop_token cancellation,std::chrono::milliseconds timeout)override{
        const auto begin=std::chrono::steady_clock::now(),deadline=begin+timeout;FastDiagnosticFrame partial;
        try{
            if(cancellation.stop_requested())return wire::failure("diagnostic provider pass cancelled before start");
            if(child_<=0)start();
            const unsigned char command=wire::sampleRequest;if(::send(socket_.value,&command,1,MSG_NOSIGNAL|MSG_DONTWAIT)!=1)throw std::runtime_error("cannot request diagnostic sample");
            for(;;){std::uint32_t size{};receive(&size,sizeof(size),cancellation,deadline);if(!size||size>wire::maximumFrame)throw std::runtime_error("invalid diagnostic frame length");
                wire::Bytes body(size);receive(body.data(),body.size(),cancellation,deadline);partial=wire::decode(body);
                if(partial.complete||partial.status.starts_with("terminal_error:"))return partial;
            }
        }catch(const std::exception& e){++failedPasses_;stop();auto failure=wire::failure(e.what(),std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count());
            partial.complete=false;partial.status=failure.status+"; "+partial.status;partial.readDurationMs=failure.readDurationMs;failure.readings.front().status=partial.status;partial.readings.insert(partial.readings.end(),failure.readings.begin(),failure.readings.end());return partial;}
    }
};
}
std::unique_ptr<ReadOnlyDiagnosticHardware> createReadOnlyDiagnosticHardware(){return std::make_unique<Hardware>();}
std::unique_ptr<FastDiagnosticWorker> createFastDiagnosticWorker(std::shared_ptr<TelemetryGate::DiagnosticCollection> collection){return std::make_unique<Worker>(std::move(collection));}
std::optional<int> runFastDiagnosticHelper(const std::vector<std::string>& args){
    helperRegistered.store(true);if(args.size()<2||args[1]!="--diagnostic-fast-worker")return {};if(args.size()!=2)return 2;
    int type{};socklen_t bytes=sizeof(type);
    if(::getsockopt(STDIN_FILENO,SOL_SOCKET,SO_TYPE,&type,&bytes)||type!=SOCK_STREAM)return 2;
    const auto read=[](void* target,std::size_t size){auto* data=static_cast<unsigned char*>(target);std::size_t at{};while(at<size){const auto n=::recv(STDIN_FILENO,data+at,size-at,0);if(n>0)at+=static_cast<std::size_t>(n);else if(n<0&&errno==EINTR)continue;else return false;}return true;};
    const auto write=[](const void* source,std::size_t size){const auto* data=static_cast<const unsigned char*>(source);std::size_t at{};while(at<size){const auto n=::send(STDOUT_FILENO,data+at,size-at,MSG_NOSIGNAL);if(n>0)at+=static_cast<std::size_t>(n);else if(n<0&&errno==EINTR)continue;else return false;}return true;};
    try{return wire::serve(read,write);}catch(...){return 1;}
}
}
