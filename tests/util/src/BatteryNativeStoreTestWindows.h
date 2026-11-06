#pragma once
// Isolated diagnostic. During the experiment WMI is read-only; its only writer
// is an explicit owned-mask cleanup fallback after the verdict has been logged.
class BatteryStoredMailbox {
    template<class T>using ComPtr=Microsoft::WRL::ComPtr<T>;
    struct Variant {VARIANT value;Variant(){VariantInit(&value);}~Variant(){VariantClear(&value);}};
    struct Bstr {BSTR value;explicit Bstr(const wchar_t* s):value(SysAllocString(s)){if(!value)throw std::bad_alloc();}~Bstr(){SysFreeString(value);}};
    struct Apartment {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}} apartment;
    ComPtr<IWbemServices> service;ComPtr<IWbemClassObject> parameters;WinHandle transaction;
    static void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Battery WMI HRESULT "+hex(static_cast<unsigned>(hr),8));}
    unsigned invoke(std::uint64_t data){
        MutexLock lock(transaction.value);Variant input;
        input.value.vt=VT_BSTR;input.value.bstrVal=SysAllocString(std::to_wstring(data).c_str());if(!input.value.bstrVal)throw std::bad_alloc();
        check(parameters->Put(L"Data",0,&input.value,CIM_UINT64));
        Bstr instance(L"AcpiTest_MULong.InstanceName='ACPI\\PNP0C14\\1_1'"),method(L"GetSetULong");
        ComPtr<IWbemClassObject> result;check(service->ExecMethod(instance.value,method.value,0,nullptr,parameters.Get(),&result,nullptr));
        Variant raw,value;check(result->Get(L"Return",0,&raw.value,nullptr,nullptr));check(VariantChangeType(&value.value,&raw.value,0,VT_UI4));
        const auto output=value.value.ulVal;if(output==0xfefefefe)throw std::runtime_error("Battery mailbox timeout");return output;
    }
public:
    BatteryStoredMailbox(){
        if(apartment.hr!=RPC_E_CHANGED_MODE)check(apartment.hr);
        const auto security=CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);
        if(security!=RPC_E_TOO_LATE)check(security);
        ComPtr<IWbemLocator> locator;check(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator)));
        Bstr space(L"ROOT\\WMI");check(locator->ConnectServer(space.value,nullptr,nullptr,nullptr,0,nullptr,nullptr,&service));
        check(CoSetProxyBlanket(service.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE));
        ComPtr<IWbemClassObject> definition,inputs;Bstr name(L"AcpiTest_MULong");check(service->GetObject(name.value,0,nullptr,&definition,nullptr));
        check(definition->GetMethod(L"GetSetULong",0,&inputs,nullptr));check(inputs->SpawnInstance(0,&parameters));
        transaction.value=CreateMutexW(nullptr,FALSE,L"Local\\MagicControl.EcTransactions.v1");if(!transaction.value)throw std::runtime_error(probe::winError());
    }
    unsigned read(unsigned offset){
        if(offset!=0x740&&offset!=0x770&&offset!=0x7C3&&offset!=0x7B9&&offset!=0x87F)throw std::runtime_error("WMI read not allowlisted");
        return invoke(0x10000000000ULL+offset);
    }
    void restoreOnly(unsigned original,unsigned ours){
        if(original>100||ours>100)throw std::runtime_error("Invalid cleanup percentage");
        MutexLock lock(transaction.value);const auto before=read(0x87F);
        if((before&0x7f)==original)return;
        if((before&0x7f)!=ours)throw std::runtime_error("Foreign WMI value; cleanup refused");
        const auto word=(before&(0xffffU&~0x7fU))|original;
        invoke(0x87FULL|(static_cast<std::uint64_t>(word)<<16));
        if((read(0x87F)&0x7f)!=original)throw std::runtime_error("WMI cleanup readback mismatch");
    }
};

inline int runBatteryNativeStoreTest(int argc,char** argv){
    fs::path output;bool apply=false,readOnly=false;unsigned limit=85;
    for(int i=2;i<argc;++i){const std::string arg=argv[i];
        if(arg=="--apply")apply=true;else if(arg=="--read-only")readOnly=true;
        else if(arg=="--output"&&i+1<argc)output=fs::path(probe::wide(argv[++i]));
        else if(arg=="--limit"&&i+1<argc){const auto value=std::string(argv[++i]);if(value=="10")limit=10;else if(value=="85")limit=85;else if(value=="95")limit=95;else throw std::runtime_error("Native-store limit must be10/85/95");}
        else throw std::runtime_error("Native-store: --apply/--read-only --limit10/85/95 --output DIR");}
    if(output.empty()||apply==readOnly)throw std::runtime_error("Native-store needs --output and one mode");
    checkCompetitors();Device ec;BatteryStoredMailbox mailbox;
    WinHandle completed(CreateEventW(nullptr,TRUE,FALSE,nullptr));cleanupComplete=completed.value;SetConsoleCtrlHandler(controlHandler,TRUE);
    struct Finish{~Finish(){if(cleanupComplete)SetEvent(cleanupComplete);SetConsoleCtrlHandler(controlHandler,FALSE);cleanupComplete=nullptr;}} finish;
    BatteryFlagSession session(ec,output/(utc(true)+"-"+std::to_string(GetCurrentProcessId())));
    session.event("session_start",{{"mode","battery-native-store"},{"apply",apply},{"limit",limit},{"rom_writes",false},{"wmi_writes_during_trial",false}});
    session.sample("initial");session.dump("initial-ec-ram");
    const auto original=ec.stable(0x7B9)&0x7f,originalPlatform=ec.stable(0x7C3),stored=mailbox.read(0x87F)&0x7f;
    session.event("wmi_baseline",{{"project",mailbox.read(0x740)},{"rom",mailbox.read(0x770)},
        {"platform",mailbox.read(0x7C3)},{"live",mailbox.read(0x7B9)},{"stored",stored}});
    if((mailbox.read(0x740)&255)!=26||(mailbox.read(0x770)&255)!=255||(mailbox.read(0x7C3)&255)!=originalPlatform||
       (mailbox.read(0x7B9)&0x7f)!=original)throw std::runtime_error("WMI/direct identity mismatch; writes=0");
    if(readOnly){session.event("read_only_complete");return 0;}
    session.guard(false);
    if(originalPlatform!=7||original!=stored||!original||original>99||limit==stored)
        throw std::runtime_error("Need platform7, nonzero matching live/stored and different target; writes=0");
    if(ec.read(0x4AB)<=limit||ec.read(0x4AB)>98||(ec.read(0x432)&3)!=2||(ec.read(0x434)|(ec.read(0x435)<<8))<100)
        throw std::runtime_error("Need SOC above target and initial charge current>=100mA; writes=0");
    bool started=false,cleanupDone=false;
    const auto observe=[&](const std::string& phase,unsigned seconds,unsigned platform,unsigned upper,bool cancelling=false){
        const auto start=GetTickCount64();unsigned stopped=0;
        for(unsigned i=0;i<seconds;++i){
            if(cancelling&&stopping.load())break;
            if(!cancelling)session.guard(false);
            if(ec.read(0x770)!=0xff||ec.read(0x7C3)!=platform||(ec.read(0x7B9)&0x7f)!=upper)
                throw std::runtime_error("Context changed during native-store observation");
            const auto row=session.sample(phase);const auto current=static_cast<unsigned>(*row.find("current_mA")->number());
            const auto flags=static_cast<unsigned>(*row.find("state_raw")->number());
            const auto value=mailbox.read(0x87F);
            session.event("stored_observed",{{"phase",phase},{"raw",value},{"low7",value&0x7f}});
            std::cout<<"WMI stored="<<hex(value)<<'\n'<<std::flush;
            if((value&0x7f)!=original&&(value&0x7f)!=limit)throw std::runtime_error("Unexpected stored value; no overwrite");
            if(current==0&&(flags&3)==0)++stopped;
            const auto end=start+(i+1)*1000;
            while(GetTickCount64()<end){if(stopping.load()){if(!cancelling)throw Cancelled();break;}Sleep(25);}
        }
        return stopped;
    };
    const auto cleanup=[&]{
        if(cleanupDone)return;
        session.event("cleanup_begin");
        const auto now=mailbox.read(0x87F)&0x7f;
        if(now!=stored&&now!=limit)throw std::runtime_error("Foreign stored threshold blocks cleanup");
        if(now!=stored){
            checkCompetitors();session.change(0x7C3,0xff,4);
            session.restoreMask(0x7B9,0x7f);
            observe("restore-native-store",5,4,original,true);
        }
        session.restore(0);
        if((mailbox.read(0x87F)&0x7f)!=stored){
            if(ec.read(0x454)!=82||ec.read(0x456)!=72||ec.read(0x740)!=26||ec.read(0x770)!=0xff)
                throw std::runtime_error("Identity changed before WMI cleanup");
            session.event("cleanup_wmi_fallback",{{"original",stored},{"ours",limit}});
            mailbox.restoreOnly(stored,limit);
        }
        session.sample("final-restored");session.dump("final-ec-ram");
        const bool matches=ec.read(0x7C3)==originalPlatform&&(ec.read(0x7B9)&0x7f)==original&&(mailbox.read(0x87F)&0x7f)==stored;
        session.event("cleanup_confirmed",{{"live_stored_platform_match",matches},{"nvm_ack_verified",false}});
        if(!matches)throw std::runtime_error("Cleanup not confirmed");cleanupDone=true;
    };
    try{
        started=true;
        session.change(0x7B9,0x7f,limit);const auto platformCheckpoint=session.checkpoint();
        session.change(0x7C3,0xff,4);
        observe("native-platform4",5,4,limit);
        const auto afterNative=mailbox.read(0x87F)&0x7f;
        session.event("native_store_result",{{"before",stored},{"after",afterNative},{"copied_by_firmware",afterNative==limit}});
        session.restore(platformCheckpoint);
        const auto stopped=observe("returned-platform7",10,7,limit);
        const bool gate=(ec.read(0x742)&4)!=0;
        session.event("trial_verdict",{{"gate",gate},{"stopped_samples",stopped},{"total_samples",10},
            {"native_stored",afterNative},{"candidate_working",gate&&afterNative==limit&&stopped>=8}});
        cleanup();session.event("complete",{{"restored",true}});return 0;
    }catch(...){
        const auto failure=std::current_exception();
        try{session.event("abort");if(started)cleanup();}catch(const std::exception& e){
            session.event("cleanup_failed",{{"error",e.what()}});try{session.restore(0);}catch(...){};throw;}
        std::rethrow_exception(failure);
    }
}
