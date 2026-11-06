#pragma once
// Diagnostic exception authorized by the owner on2026-10-22. ROM-ID remains
// read-only in production and all other diagnostics. No ROM save procedure.
inline int runBatteryRomIdTest(int argc,char** argv){
    fs::path output,recovery;bool apply=false,readOnly=false;
    for(int i=2;i<argc;++i){const std::string arg=argv[i];
        if(arg=="--apply")apply=true;else if(arg=="--read-only")readOnly=true;
        else if(arg=="--output"&&i+1<argc)output=fs::path(probe::wide(argv[++i]));
        else if(arg=="--restore"&&i+1<argc)recovery=fs::path(probe::wide(argv[++i]));
        else throw std::runtime_error("ROM-ID RAM mode: --apply/--read-only/--restore DIR --output DIR");}
    if(output.empty()||static_cast<int>(apply)+static_cast<int>(readOnly)+static_cast<int>(!recovery.empty())!=1)
        throw std::runtime_error("ROM-ID RAM mode needs --output and exactly one mode");
    checkCompetitors();Device ec(true);BatteryStoredMailbox mailbox;
    WinHandle completed(CreateEventW(nullptr,TRUE,FALSE,nullptr));cleanupComplete=completed.value;
    SetConsoleCtrlHandler(controlHandler,TRUE);
    struct Finish{~Finish(){if(cleanupComplete)SetEvent(cleanupComplete);SetConsoleCtrlHandler(controlHandler,FALSE);cleanupComplete=nullptr;}} finish;
    const auto folder=output/(utc(true)+"-"+std::to_string(GetCurrentProcessId()));
    BatteryFlagSession session(ec,folder,true);
    session.event("session_start",{{"mode","battery-romid-ram"},{"apply",apply},
        {"address",0x770},{"trial_value",4},{"mask",0xfb},{"step_seconds",10},
        {"platform_writes",false},{"wmi_writes",false},{"set_romid_called",false},{"nvm_persistence_verified",false}});
    if(!recovery.empty()){session.recover(recovery);session.dump("recovery-ec-ram");return 0;}
    session.guard(false);
    if(ec.stable(0x770)!=0xff||ec.stable(0x7C3)!=7)
        throw std::runtime_error("Need original ROM-ID FF and platform7; writes=0");
    session.sample("initial");session.dump("initial-ec-ram");
    const auto upper=ec.stable(0x7B9)&0x7f,stored=mailbox.read(0x87F)&0x7f;
    session.event("wmi_baseline",{{"upper",upper},{"stored",stored},{"rom_word",mailbox.read(0x770)}});
    if(readOnly){session.event("read_only_complete");return 0;}
    if(!upper||upper>99||upper!=stored||ec.read(0x4AB)<=upper||ec.read(0x4AB)>98)
        throw std::runtime_error("Need matching nonzero live/stored upper below SOC11..98; writes=0");
    if((ec.read(0x432)&3)!=2||(ec.read(0x434)|(ec.read(0x435)<<8))<100)
        throw std::runtime_error("Need initial charging>=100mA; writes=0");

    // Monitor configuration, not temperature/PWM/status outputs. A ROM-ID change
    // is also consulted by fan policy; abort instead of repairing unrelated data.
    struct Watched{unsigned offset,mask,value;};std::vector<Watched> watched;
    const auto watch=[&](unsigned a,unsigned mask){watched.push_back({a,mask,ec.stable(a)&mask});};
    watch(0x726,0x80);watch(0x727,0x40);watch(0x741,1);watch(0x751,0xff);
    watch(0x7C5,0x80);watch(0x7C6,4);watch(0x7A6,0x30);watch(0x7CC,0xff);
    watch(0x7C3,0xff);watch(0x7B9,0x7f);watch(0x7D0,0xff);
    for(unsigned a=0x771;a<=0x77f;++a)watch(a,0xff);
    for(unsigned a=0x783;a<=0x785;++a)watch(a,0xff);
    for(unsigned a=0xf00;a<=0xf5f;++a)watch(a,0xff);
    const auto contextMatches=[&]{bool ok=true;
        for(const auto& w:watched){const auto now=ec.read(w.offset)&w.mask;if(now!=w.value){
            session.event("context_changed",{{"address",w.offset},{"mask",w.mask},{"before",w.value},{"now",now}});ok=false;}}
        return ok;
    };
    const auto observe=[&](const char* phase,unsigned rom){
        const auto start=GetTickCount64();unsigned stopped=0;
        for(unsigned i=0;i<10;++i){
            session.guard(false);
            if(ec.read(0x770)!=rom||!contextMatches())throw std::runtime_error("ROM/context changed; stop and restore owned mask only");
            if(ec.read(0x43E)>=85)throw std::runtime_error("CPU temperature>=85C; stop ROM-ID trial");
            const auto row=session.sample(phase);
            const auto current=static_cast<unsigned>(*row.find("current_mA")->number());
            const auto state=static_cast<unsigned>(*row.find("state_raw")->number());
            if(current==0&&(state&3)==0)++stopped;
            const auto end=start+(i+1)*1000;
            while(GetTickCount64()<end){if(stopping.load())throw Cancelled();Sleep(25);}
        }
        return stopped;
    };
    try{
        observe("baseline-platform7-romFF",0xff);
        if((ec.read(0x432)&3)!=2||(ec.read(0x434)|(ec.read(0x435)<<8))<100)
            throw std::runtime_error("Charging baseline disappeared; writes=0");
        session.change(0x770,0xfb,0); // FF ->04, preserving the unchanged bit2.
        const auto stopped=observe("trial-platform7-rom04",4);
        session.dump("trial-rom04-ec-ram");
        session.event("trial_verdict",{{"stopped_samples",stopped},{"total_samples",10},
            {"gate",(ec.read(0x742)&4)!=0},{"upper",upper},{"stored",mailbox.read(0x87F)&0x7f},
            {"platform",ec.read(0x7C3)},{"rom",ec.read(0x770)},
            {"physical_inhibition_candidate",stopped>=8&&(ec.read(0x742)&4)!=0}});
        session.restore(0);
        if(ec.stable(0x770)!=0xff)throw std::runtime_error("ROM-ID restoration not confirmed");
        observe("restored-platform7-romFF",0xff);
        session.dump("final-ec-ram");
        const bool context=contextMatches(),copy=(mailbox.read(0x87F)&0x7f)==stored;
        session.event("complete",{{"restored",ec.read(0x770)==0xff&&context&&copy},
            {"configuration_unchanged",context},{"stored_unchanged",copy},{"nvm_persistence_verified",false}});
        if(!context||!copy)throw std::runtime_error("Unowned context changed; inspect snapshots, no blind rewrite");
        return 0;
    }catch(...){
        const auto failure=std::current_exception();
        session.event("abort");session.restore(0);
        session.event("abort_restored",{{"rom",ec.read(0x770)},{"platform",ec.read(0x7C3)}});
        session.dump("abort-restored-ec-ram");std::rethrow_exception(failure);
    }
}
