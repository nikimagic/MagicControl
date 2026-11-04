#include "../common/FastDiagnosticWire.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "BackendWindows.h"
#include "uniwill-acpi/ReviewedDriverWindows.h"
#include "../../devices/lenovo/legion-kwcn/ControlWindows.h"
#include <windows.h>
#include <atomic>
#include <thread>
#include <algorithm>
namespace mc::diagnostics {
namespace {
std::atomic<bool> helperRegistered{};
struct Handle{HANDLE value{};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}void reset(HANDLE next=nullptr){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=next;}};
class ReadOnlyBackend final:public IEcBackend{
    std::unique_ptr<IEcBackend> inner_;BackendInfo info_;
public:
    explicit ReadOnlyBackend(std::unique_ptr<IEcBackend> inner):inner_(std::move(inner)),info_(inner_->info()){info_.writable=false;info_.displayName+=" · diagnostic writes denied";}
    const BackendInfo& info()const noexcept override{return info_;}
    std::uint8_t read(std::uint16_t address)override{return inner_->read(address);}
    std::vector<std::uint8_t> readBlock(std::uint16_t address,std::size_t bytes)override{return inner_->readBlock(address,bytes);}
    void write(std::uint16_t,std::uint8_t)override{throw std::runtime_error("Isolated telemetry backend is read-only");}
    void lockTransaction()override{inner_->lockTransaction();}void unlockTransaction()noexcept override{inner_->unlockTransaction();}
};
class Hardware final:public ReadOnlyDiagnosticHardware{
    std::unique_ptr<EcAccess> access_;std::unique_ptr<IEcBackend> gm6_;std::string status_;
public:
    Hardware(){try{
        if(wcsstr(GetCommandLineW(),L"--no-ec")){status_="EC telemetry disabled by --no-ec";return;}
        const auto id=deviceProfile(readBoardDescriptor()).id;
        if(id=="wu-jie-15-xpro"){
            access_=std::make_unique<EcAccess>(std::make_unique<ReadOnlyBackend>(createDefaultBackend()));
            const auto check=access_->verifyBoard();if(!check.verified)throw std::runtime_error(check.error);
        }else if(gmRamFanProfile(id)){
            if(gm5Profile(id)){std::string proof;windows::reviewed_uniwill::verifyGm5Driver(proof);}
            if(l16Profile(id)){std::string proof;windows::reviewed_uniwill::verifyL16Driver(proof);}
            gm6_=createWindowsAcpiDiagnosticBackend();
            if(l16Profile(id))windows::reviewed_uniwill::verifyL16EcIdentity([&](auto a){return gm6_->read(a);});
            if(!gmRamProject(deviceProfile(readBoardDescriptor()).id,gm6_->read(0x740))||!(gm6_->read(0x78e)&0x40))throw std::runtime_error("GM6 diagnostic ProjectID/RAM1.5 identity mismatch");
        }
    }catch(const std::exception& e){status_=e.what();access_.reset();gm6_.reset();}}
    EcAccess* access()noexcept override{return access_.get();}
    FanMaximumSample fans()override{
        if(deviceProfile(readBoardDescriptor()).id=="lenovo-legion-kwcn")return windows::readLegionFans();
        FanMaximumSample result;if(!status_.empty())throw std::runtime_error(status_);
        if(access_)for(unsigned channel=0;channel<2;++channel){const auto value=access_->readFanFeedback(channel?FanChannel::Gpu:FanChannel::Cpu,true);result.rpm[channel]=static_cast<int>(value.rpm);result.percent[channel]=value.dutyPercent;}
        if(gm6_){
            // Exact GM6 getter addresses; no Gm6Controller, startup restore or
            // online watchdog is ever instantiated in this killable process.
            struct Lock{IEcBackend& io;explicit Lock(IEcBackend& value):io(value){io.lockTransaction();}~Lock(){io.unlockTransaction();}} lock{*gm6_};
            if(!gmRamFanProfile(deviceProfile(readBoardDescriptor()).id)||!gmRamProject(deviceProfile(readBoardDescriptor()).id,gm6_->read(0x740))||!(gm6_->read(0x78e)&64))throw std::runtime_error("GM6 read-only identity changed");
            for(unsigned channel=0;channel<2;++channel){const std::uint16_t high=channel?0x46c:0x464,low=channel?0x46b:0x465;
                std::vector<int> values;unsigned zeros{};
                for(unsigned n=0;n<3;++n){const auto h=gm6_->read(high),l=gm6_->read(low);if(h!=gm6_->read(high))continue;const int rpm=(h<<8)|l;if(!rpm)++zeros;else if(rpm<=20000)values.push_back(rpm);}
                if(values.size()>=2){std::sort(values.begin(),values.end());result.rpm[channel]=values[values.size()/2];}else if(zeros>=2&&values.empty())result.rpm[channel]=0;
                const auto duty=gm6_->read(channel?0x75c:0x75b);if(duty<=200)result.percent[channel]=duty/2.;
            }
        }
        return result;
    }
};
class Worker final:public FastDiagnosticWorker{
    Handle input_,output_,process_,job_;
    unsigned failedPasses_{};
    void stop()noexcept{
        // Only the fixed read-only helper belongs to this job. The GUI's active
        // fan controller/probe/cleanup is never terminated on a provider timeout.
        if(job_.value)TerminateJobObject(job_.value,ERROR_CANCELLED);
        if(process_.value)WaitForSingleObject(process_.value,1000);
        input_.reset();output_.reset();process_.reset();job_.reset();
    }
    void start(){
        if(!helperRegistered.load())throw std::runtime_error("Diagnostic worker early entry point was not registered");
        if(failedPasses_>=3)throw std::runtime_error("Read-only diagnostic worker restart limit reached; previous provider errors retained");
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE childInput{},parentInput{},parentOutput{},childOutput{};
        if(!CreatePipe(&childInput,&parentInput,&sa,0))throw std::runtime_error("Cannot create diagnostic input pipe");
        Handle in{childInput},out{parentInput};SetHandleInformation(out.value,HANDLE_FLAG_INHERIT,0);
        if(!CreatePipe(&parentOutput,&childOutput,&sa,0))throw std::runtime_error("Cannot create diagnostic output pipe");
        Handle reader{parentOutput},writer{childOutput};SetHandleInformation(reader.value,HANDLE_FLAG_INHERIT,0);
        Handle null{CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr)};
        Handle job{CreateJobObjectW(nullptr,nullptr)};JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot bound diagnostic worker lifetime");
        SIZE_T bytes{};InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<BYTE> attributes(bytes);
        auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if(!InitializeProcThreadAttributeList(list,1,0,&bytes))throw std::runtime_error("Cannot restrict diagnostic handle inheritance");
        struct Delete{LPPROC_THREAD_ATTRIBUTE_LIST p;~Delete(){DeleteProcThreadAttributeList(p);}} deleteList{list};
        HANDLE inherited[]{in.value,writer.value,null.value};
        if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))throw std::runtime_error("Cannot set diagnostic inherited handles");
        std::wstring exe(32768,0);const auto count=GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()));if(!count||count>=exe.size())throw std::runtime_error("Cannot locate diagnostic executable");exe.resize(count);
        auto command=L"\""+exe+L"\" --diagnostic-fast-worker";
        if(wcsstr(GetCommandLineW(),L"--no-ec"))command+=L" --no-ec";
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=list;
        startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;
        startup.StartupInfo.hStdInput=in.value;startup.StartupInfo.hStdOutput=writer.value;startup.StartupInfo.hStdError=null.value;
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&process))throw std::runtime_error("Cannot start read-only diagnostic worker: Win32="+std::to_string(GetLastError()));
        Handle child{process.hProcess},thread{process.hThread};
        if(!AssignProcessToJobObject(job.value,child.value)){TerminateProcess(child.value,ERROR_CANCELLED);WaitForSingleObject(child.value,1000);throw std::runtime_error("Cannot isolate diagnostic process");}
        if(ResumeThread(thread.value)==DWORD(-1))throw std::runtime_error("Cannot resume diagnostic process");
        input_.value=out.value;out.value=nullptr;output_.value=reader.value;reader.value=nullptr;process_.value=child.value;child.value=nullptr;job_.value=job.value;job.value=nullptr;
    }
    void receive(void* destination,std::size_t size,std::stop_token cancellation,std::chrono::steady_clock::time_point deadline){
        auto* bytes=static_cast<BYTE*>(destination);std::size_t at{};
        while(at<size){
            if(cancellation.stop_requested())throw std::runtime_error("diagnostic provider pass cancelled; partial observations retained");
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("diagnostic provider deadline expired; partial observations retained");
            DWORD available{};
            if(!PeekNamedPipe(output_.value,nullptr,0,nullptr,&available,nullptr))throw std::runtime_error("diagnostic worker pipe closed");
            if(available){DWORD got{};if(!ReadFile(output_.value,bytes+at,static_cast<DWORD>(std::min<std::size_t>(available,size-at)),&got,nullptr)||!got)throw std::runtime_error("diagnostic worker response truncated");at+=got;}
            else{if(WaitForSingleObject(process_.value,0)==WAIT_OBJECT_0)throw std::runtime_error("diagnostic worker exited");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        }
    }
public:
    ~Worker()override{stop();}
    FirmwareCaptureResult firmware(bool stateOnly,const FirmwareEmitter& emit,std::stop_token cancellation,std::chrono::milliseconds timeout,bool biosRom)override{
        const auto begin=std::chrono::steady_clock::now(),deadline=begin+timeout;FirmwareCaptureResult result;std::size_t count{},total{};
        try{
            if(cancellation.stop_requested())throw std::runtime_error("firmware evidence cancelled before start");
            if(!process_.value)start();const unsigned char command=biosRom?wire::biosRomRequest:stateOnly?wire::firmwareStateRequest:wire::firmwareRequest;DWORD written{};
            if(!WriteFile(input_.value,&command,1,&written,nullptr)||written!=1)throw std::runtime_error("cannot request firmware evidence");
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
            if(!process_.value)start();
            const unsigned char request=wire::sampleRequest;DWORD written{};
            if(!WriteFile(input_.value,&request,1,&written,nullptr)||written!=1)throw std::runtime_error("cannot request diagnostic sample");
            for(;;){std::uint32_t size{};receive(&size,sizeof(size),cancellation,deadline);
                if(!size||size>wire::maximumFrame)throw std::runtime_error("invalid diagnostic frame length");
                wire::Bytes bytes(size);receive(bytes.data(),bytes.size(),cancellation,deadline);partial=wire::decode(bytes);
                if(partial.complete||partial.status.starts_with("terminal_error:"))return partial;
            }
        }catch(const std::exception& e){++failedPasses_;stop();const auto status=std::string(e.what());
            auto failure=wire::failure(status,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count());
            partial.status=status+"; "+partial.status;partial.complete=false;partial.readDurationMs=failure.readDurationMs;
            failure.readings.front().status=partial.status;
            partial.readings.insert(partial.readings.end(),failure.readings.begin(),failure.readings.end());return partial;
        }
    }
};
}
std::unique_ptr<ReadOnlyDiagnosticHardware> createReadOnlyDiagnosticHardware(){return std::make_unique<Hardware>();}
std::unique_ptr<FastDiagnosticWorker> createFastDiagnosticWorker(){return std::make_unique<Worker>();}
std::optional<int> runFastDiagnosticHelper(const std::vector<std::string>& args){
    helperRegistered.store(true);if(args.size()<2||args[1]!="--diagnostic-fast-worker")return {};
    if(args.size()!=2&&(args.size()!=3||args[2]!="--no-ec"))return 2;
    const auto input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);
    if(GetFileType(input)!=FILE_TYPE_PIPE||GetFileType(output)!=FILE_TYPE_PIPE)return 2;
    const auto read=[&](void* target,std::size_t size){DWORD got{};return ReadFile(input,target,static_cast<DWORD>(size),&got,nullptr)&&got==size;};
    const auto write=[&](const void* source,std::size_t size){const auto* bytes=static_cast<const BYTE*>(source);std::size_t at{};while(at<size){DWORD sent{};if(!WriteFile(output,bytes+at,static_cast<DWORD>(size-at),&sent,nullptr)||!sent)return false;at+=sent;}return true;};
    try{return wire::serve(read,write);}catch(...){return 1;}
}
}
