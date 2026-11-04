#include "../common/FirmwareEvidence.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include <glib.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <memory>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace mc::diagnostics {
bool firmwareValidUtf8(std::string_view value) noexcept {
    return value.empty()||g_utf8_validate(value.data(),static_cast<gssize>(value.size()),nullptr);
}
std::string firmwareSha256(std::span<const std::uint8_t> bytes){
    std::unique_ptr<GChecksum,decltype(&g_checksum_free)> checksum(g_checksum_new(G_CHECKSUM_SHA256),g_checksum_free);
    if(!checksum)throw std::runtime_error("GLib SHA256 allocation failed");
    for(std::size_t at=0;at<bytes.size();){
        const auto count=std::min<std::size_t>(65536,bytes.size()-at);
        g_checksum_update(checksum.get(),reinterpret_cast<const guchar*>(bytes.data()+at),static_cast<gssize>(count));at+=count;
    }
    const auto* value=g_checksum_get_string(checksum.get());
    if(!value)throw std::runtime_error("GLib SHA256 digest unavailable");
    return value;
}

namespace {
using Clock=std::chrono::steady_clock;
constexpr unsigned long sysfsMagic=0x62656572UL,efivarfsMagic=0xde5e81e4UL;
constexpr std::size_t maximumAcpiEntries=4096,maximumIdentityBytes=4096;
constexpr std::size_t maximumEntryPointBytes=4096,maximumVariablePayloadBytes=64*1024;
constexpr std::string_view variableGuid="9f33f85c-13ca-4fd1-9c4a-96217722c593";
constexpr std::array variableNames{"OemMagicVariable","UniWillVariable","OemMagicDoor"};
struct Fd{
    int value{-1};Fd()=default;explicit Fd(int fd):value(fd){}
    Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;
    Fd(Fd&& other)noexcept:value(std::exchange(other.value,-1)){}
    Fd& operator=(Fd&& other)noexcept{if(this!=&other){if(value>=0)::close(value);value=std::exchange(other.value,-1);}return *this;}
    ~Fd(){if(value>=0)::close(value);}
};
struct Failure{std::string status,domain,detail;std::optional<std::uint32_t> code;};
Failure systemFailure(std::string operation,int code){
    return {"read_failed","errno",std::move(operation)+": "+std::strerror(code),static_cast<std::uint32_t>(code)};
}
Failure policyFailure(std::string status,std::string detail){return {std::move(status),"collector_policy",std::move(detail),{}};}
std::string rawHex(std::string_view value){
    constexpr char digits[]="0123456789abcdef";std::string result;result.reserve(value.size()*2);
    for(const unsigned char byte:value){result+=digits[byte>>4];result+=digits[byte&15];}return result;
}
std::string asciiDisplay(std::string_view value){
    // Preserve the original byte sequence separately. Metadata JSON must stay
    // valid UTF-8 even if a corrupt firmware signature produced a non-UTF8 name.
    std::string result;result.reserve(value.size());
    for(const unsigned char byte:value)result+=byte>=32&&byte<=126?static_cast<char>(byte):'?';
    return result;
}
bool safeLeaf(std::string_view name){return !name.empty()&&name!="."&&name!=".."&&name.size()<=255&&name.find('/')==std::string_view::npos&&name.find('\0')==std::string_view::npos;}
bool excludedSignature(std::string_view name){
    if(name.size()<4)return false;std::array<char,4> signature{};
    for(unsigned i=0;i<4;++i){const unsigned char c=name[i];signature[i]=static_cast<char>(c>='a'&&c<='z'?c-'a'+'A':c);}
    const std::string_view s(signature.data(),signature.size());return s=="MSDM"||s=="SLIC";
}
std::uint32_t little32(std::span<const std::uint8_t> bytes,std::size_t at){
    return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);
}
std::string jsonBool(bool value){return value?"true":"false";}
std::string jsonNumber(std::optional<std::uint64_t> value){return value?std::to_string(*value):"null";}
bool checksumZero(std::span<const std::uint8_t> bytes){std::uint8_t sum{};for(const auto byte:bytes)sum=static_cast<std::uint8_t>(sum+byte);return sum==0;}

struct Budget{
    Clock::time_point deadline{Clock::now()+std::chrono::seconds(20)};
    std::size_t readBytes{},records{},acpiEntries{};bool recordLimitReported{};
    bool expired()const{return Clock::now()>=deadline;}
    bool canRead()const{return !expired()&&readBytes<maximumFirmwareTotalBytes&&records+1<maximumFirmwareRecords;}
};
struct Reader{
    Budget budget;const FirmwareEmitter& emitter;
    explicit Reader(const FirmwareEmitter& emit):emitter(emit){}
    void send(FirmwareRecord record){
        if(budget.records+1>=maximumFirmwareRecords){
            if(!budget.recordLimitReported&&budget.records<maximumFirmwareRecords){
                budget.recordLimitReported=true;FirmwareRecord limit;limit.kind="collector";limit.status="record_limit";
                limit.errorDomain="collector_policy";limit.metadataJson="{\"maximum_records\":"+std::to_string(maximumFirmwareRecords)+"}";
                ++budget.records;emitter(limit);
            }
            return;
        }
        ++budget.records;emitter(record);
    }
    Failure unavailable()const{
        if(budget.expired())return policyFailure("deadline","20s collector loop budget expired");
        if(budget.readBytes>=maximumFirmwareTotalBytes)return policyFailure("total_byte_limit","256MiB collector read budget exhausted");
        return policyFailure("record_limit","8192 record budget exhausted");
    }
    void failed(const char* kind,std::string source,const Failure& failure,std::string extra=""){
        FirmwareRecord record;record.kind=kind;record.source=std::move(source);record.status=failure.status;
        record.errorDomain=failure.domain;record.errorCode=failure.code;
        record.metadataJson="{\"detail\":"+firmwareQuote(asciiDisplay(failure.detail))+",\"detail_raw_hex\":"+firmwareQuote(rawHex(failure.detail))+(extra.empty()?"":","+extra)+"}";send(std::move(record));
    }
};

bool trustedFs(int fd,unsigned long expected,Failure& failure){
    struct statfs fs{};if(::fstatfs(fd,&fs)<0){failure=systemFailure("fstatfs",errno);return false;}
    if(static_cast<unsigned long>(fs.f_type)!=expected){failure=policyFailure("filesystem_mismatch",std::format("expected_fs=0x{:x} actual_fs=0x{:x}",expected,static_cast<unsigned long>(fs.f_type)));return false;}
    return true;
}
Fd sysRoot(Failure& failure){
    Fd fd(::open("/sys",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if(fd.value<0){failure=systemFailure("open /sys",errno);return {};}
    if(!trustedFs(fd.value,sysfsMagic,failure))return {};return fd;
}
Fd directory(int parent,std::string_view leaf,unsigned long magic,Failure& failure){
    if(!safeLeaf(leaf)){failure=policyFailure("unsafe_name","source directory is not one safe component");return {};}
    const std::string name(leaf);Fd fd(::openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if(fd.value<0){failure=systemFailure("openat directory "+asciiDisplay(leaf),errno);return {};}
    if(!trustedFs(fd.value,magic,failure))return {};return fd;
}
Fd walk(int parent,std::initializer_list<const char*> parts,Failure& failure){
    Fd current;int next=parent;
    for(const auto* part:parts){auto fd=directory(next,part,sysfsMagic,failure);if(fd.value<0)return {};current=std::move(fd);next=current.value;}
    return current;
}

struct ReadResult{
    std::vector<std::uint8_t> bytes;std::optional<std::uint64_t> advertised;
    Failure failure;bool eof{},excluded{};
};
ReadResult readFile(Reader& reader,int parent,std::string_view leaf,unsigned long magic,std::size_t limit,bool acpi=false){
    ReadResult result;limit=std::min(limit,maximumFirmwareArtifactBytes);
    if(!reader.budget.canRead()){result.failure=reader.unavailable();return result;}
    if(!safeLeaf(leaf)){result.failure=policyFailure("unsafe_name","source file is not one safe component");return result;}
    const std::string name(leaf);struct stat before{};
    if(::fstatat(parent,name.c_str(),&before,AT_SYMLINK_NOFOLLOW)<0){result.failure=systemFailure("fstatat",errno);return result;}
    if(!S_ISREG(before.st_mode)){result.failure=policyFailure("unsupported_file_type","only regular sysfs/efivarfs binary files may be read; symlinks are not followed");return result;}
    Fd fd(::openat(parent,name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    if(fd.value<0){result.failure=systemFailure("openat file",errno);return result;}
    struct stat opened{};
    if(::fstat(fd.value,&opened)<0){result.failure=systemFailure("fstat",errno);return result;}
    if(!S_ISREG(opened.st_mode)||before.st_dev!=opened.st_dev||before.st_ino!=opened.st_ino){result.failure=policyFailure("source_changed","file identity/type changed while opening");return result;}
    if(!trustedFs(fd.value,magic,result.failure))return result;
    if(opened.st_size>=0)result.advertised=static_cast<std::uint64_t>(opened.st_size);
    std::array<std::uint8_t,65536> buffer{};
    while(result.bytes.size()<limit){
        if(!reader.budget.canRead()){result.failure=reader.unavailable();return result;}
        const auto remaining=maximumFirmwareTotalBytes-reader.budget.readBytes;
        auto count=std::min({buffer.size(),limit-result.bytes.size(),remaining});
        // Inspect the standard table header before reading a table body. A
        // spoofed filename must not cause MSDM/SLIC payloads to be exported.
        if(acpi&&result.bytes.size()<36)count=std::min(count,36-result.bytes.size());
        const auto got=::read(fd.value,buffer.data(),count);
        if(got<0){const auto code=errno;if(code==EINTR)continue;result.failure=systemFailure("read",code);return result;}
        if(got==0){result.eof=true;return result;}
        const auto size=static_cast<std::size_t>(got);reader.budget.readBytes+=size;
        result.bytes.insert(result.bytes.end(),buffer.begin(),buffer.begin()+size);
        if(acpi&&result.bytes.size()>=4&&excludedSignature(std::string_view(reinterpret_cast<const char*>(result.bytes.data()),4))){
            result.excluded=true;result.bytes.clear();result.failure=policyFailure("excluded_sensitive_table","MSDM/SLIC licensing tables are excluded");return result;
        }
    }
    // Do not read a seventeenth MiB to infer EOF. Reaching the cap without EOF
    // remains incomplete even if st_size happened to equal the limit.
    result.failure=policyFailure("artifact_byte_limit","read reached its artifact limit; EOF was not confirmed");return result;
}
std::string captureMetadata(const ReadResult& result){
    return "\"captured_bytes\":"+std::to_string(result.bytes.size())+",\"advertised_st_size\":"+jsonNumber(result.advertised)+
        ",\"eof_confirmed\":"+jsonBool(result.eof)+",\"detail\":"+firmwareQuote(asciiDisplay(result.failure.detail))+",\"detail_raw_hex\":"+firmwareQuote(rawHex(result.failure.detail));
}
std::string hashMetadata(std::span<const std::uint8_t> bytes,std::string_view key="sha256"){
    try{return firmwareQuote(key)+":"+firmwareQuote(firmwareSha256(bytes));}
    catch(const std::exception& error){return firmwareQuote(std::string(key)+"_error")+":"+firmwareQuote(asciiDisplay(error.what()));}
}
std::string acpiValidation(const ReadResult& read){
    const auto bytes=std::span<const std::uint8_t>(read.bytes);std::string metadata="\"validation\":{";
    if(bytes.size()<4)return metadata+"\"header\":\"short signature\",\"checksum\":null}";
    const std::string signature(reinterpret_cast<const char*>(bytes.data()),4);
    metadata+="\"signature_display\":"+firmwareQuote(asciiDisplay(signature))+",\"signature_hex\":"+firmwareQuote(rawHex(signature));
    // RSDP has a different header/checksum contract; never validate it as SDT.
    if(bytes.size()>=8&&std::memcmp(bytes.data(),"RSD PTR ",8)==0){
        metadata+=",\"format\":\"RSDP\",\"checksum20_ok\":"+(bytes.size()>=20?jsonBool(checksumZero(bytes.first(20))):"null");
        if(bytes.size()>15&&bytes[15]>=2){
            const auto length=bytes.size()>=24?std::optional<std::uint64_t>(little32(bytes,20)):std::nullopt;
            metadata+=",\"declared_length\":"+jsonNumber(length)+",\"extended_checksum_ok\":"+(length&&*length>=36&&*length<=bytes.size()?jsonBool(checksumZero(bytes.first(static_cast<std::size_t>(*length)))):"null");
        }
        return metadata+"}";
    }
    if(bytes.size()<8)return metadata+",\"header\":\"short length\",\"checksum\":null}";
    const auto declared=little32(bytes,4);
    metadata+=",\"declared_length\":"+std::to_string(declared)+",\"captured_length_matches\":"+jsonBool(declared==bytes.size());
    if(signature=="FACS")return metadata+",\"format\":\"FACS\",\"checksum_applicable\":false}";
    metadata+=",\"format\":\"ACPI SDT\",\"standard_header_present\":"+jsonBool(bytes.size()>=36&&declared>=36);
    if(bytes.size()>=36){
        metadata+=",\"revision\":"+std::to_string(bytes[8])+",\"oem_id_hex\":"+firmwareQuote(rawHex(std::string_view(reinterpret_cast<const char*>(bytes.data()+10),6)))+
            ",\"oem_table_id_hex\":"+firmwareQuote(rawHex(std::string_view(reinterpret_cast<const char*>(bytes.data()+16),8)));
    }
    metadata+=",\"checksum_ok\":"+(declared>=36&&declared<=bytes.size()?jsonBool(checksumZero(bytes.first(declared))):"null");
    return metadata+"}";
}
std::string smbiosValidation(const ReadResult& read,bool entry,const Budget& budget){
    const auto bytes=std::span<const std::uint8_t>(read.bytes);
    if(entry){
        if(bytes.size()>=7&&std::memcmp(bytes.data(),"_SM3_",5)==0){
            const auto length=bytes[6];const bool valid=length>=24&&length<=bytes.size();
            return "\"validation\":{\"entry_format\":\"SMBIOS3\",\"declared_length\":"+std::to_string(length)+",\"header_complete\":"+jsonBool(valid)+
                ",\"checksum_ok\":"+(valid?jsonBool(checksumZero(bytes.first(length))):"null")+"}";
        }
        if(bytes.size()>=6&&std::memcmp(bytes.data(),"_SM_",4)==0){
            const auto length=bytes[5];const bool valid=length>=31&&length<=bytes.size();
            const bool intermediate=bytes.size()>=31&&std::memcmp(bytes.data()+16,"_DMI_",5)==0;
            return "\"validation\":{\"entry_format\":\"SMBIOS2\",\"declared_length\":"+std::to_string(length)+",\"header_complete\":"+jsonBool(valid)+
                ",\"checksum_ok\":"+(valid?jsonBool(checksumZero(bytes.first(length))):"null")+",\"intermediate_anchor_ok\":"+jsonBool(intermediate)+
                ",\"intermediate_checksum_ok\":"+(intermediate?jsonBool(checksumZero(bytes.subspan(16,15))):"null")+"}";
        }
        if(bytes.size()>=5&&std::memcmp(bytes.data(),"_DMI_",5)==0)return "\"validation\":{\"entry_format\":\"legacy DMI\",\"checksum_ok\":"+(bytes.size()>=15?jsonBool(checksumZero(bytes.first(15))):"null")+"}";
        return "\"validation\":{\"entry_format\":\"unknown/short anchor\",\"header_complete\":false,\"checksum_ok\":null}";
    }
    std::size_t at{},count{};bool end{},valid=true,limited{};
    while(at<bytes.size()){
        if(budget.expired()||count>=65536){limited=true;break;}
        if(bytes.size()-at<4){valid=false;break;}
        const auto type=bytes[at],length=bytes[at+1];
        if(length<4||length>bytes.size()-at){valid=false;break;}
        std::size_t next=at+length;
        while(next+1<bytes.size()&&(bytes[next]!=0||bytes[next+1]!=0))++next;
        if(next+1>=bytes.size()){valid=false;break;}
        at=next+2;++count;if(type==127){end=true;break;}
    }
    return "\"validation\":{\"format\":\"SMBIOS structure stream\",\"structures_examined\":"+std::to_string(count)+",\"structure_bounds_valid\":"+jsonBool(valid)+
        ",\"end_of_table_seen\":"+jsonBool(end)+",\"validation_limited\":"+jsonBool(limited)+",\"unparsed_bytes\":"+std::to_string(bytes.size()-at)+",\"checksum_applicable\":false}";
}

void tableDirectory(Reader& reader,int fd,bool dynamic){
    const std::string source=dynamic?"/sys/firmware/acpi/tables/dynamic":"/sys/firmware/acpi/tables";
    const int duplicate=::fcntl(fd,F_DUPFD_CLOEXEC,0);
    if(duplicate<0){reader.failed("acpi",source,systemFailure("duplicate directory fd",errno));return;}
    DIR* raw=::fdopendir(duplicate);
    if(!raw){const auto code=errno;::close(duplicate);reader.failed("acpi",source,systemFailure("fdopendir",code));return;}
    struct CloseDirectory{void operator()(DIR* value)const noexcept{::closedir(value);}};
    std::unique_ptr<DIR,CloseDirectory> directoryOwner(raw);std::vector<std::string> names;
    for(;;){
        if(!reader.budget.canRead()){reader.failed("acpi",source,reader.unavailable());break;}
        errno=0;const auto* entry=::readdir(raw);
        if(!entry){if(errno)reader.failed("acpi",source,systemFailure("readdir",errno));break;}
        const std::string name(entry->d_name);if(name=="."||name=="..")continue;
        if(reader.budget.acpiEntries>=maximumAcpiEntries){reader.failed("acpi",source,policyFailure("directory_entry_limit","4096 shared ACPI directory-entry budget exhausted"));break;}
        ++reader.budget.acpiEntries;
        // Only one reviewed subdirectory is visited by the caller. Never walk
        // tables/data, configfs, overlays, or arbitrary child directories.
        if(!dynamic&&(name=="dynamic"||name=="data"))continue;
        if(!safeLeaf(name)){reader.failed("acpi",source,policyFailure("unsafe_name","unsafe ACPI source name"));continue;}
        struct stat st{};
        if(::fstatat(fd,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)<0){reader.failed("acpi",source+"/"+asciiDisplay(name),systemFailure("fstatat entry",errno));continue;}
        if(!S_ISREG(st.st_mode)){reader.failed("acpi",source+"/"+asciiDisplay(name),policyFailure("unsupported_file_type","ACPI source is not a regular binary file; not followed"));continue;}
        names.push_back(name);
    }
    std::sort(names.begin(),names.end());std::size_t index{},remaining=names.size();
    for(const auto& name:names){
        const auto path=source+"/"+asciiDisplay(name);
        if(!reader.budget.canRead()){reader.failed("acpi",path,reader.unavailable(),"\"unattempted_listed_files\":"+std::to_string(remaining));break;}
        --remaining;
        if(excludedSignature(name)){reader.failed("acpi",path,policyFailure("excluded_sensitive_table","MSDM/SLIC licensing tables are excluded"),"\"source_leaf_hex\":"+firmwareQuote(rawHex(name)));continue;}
        auto result=readFile(reader,fd,name,sysfsMagic,maximumFirmwareArtifactBytes,true);
        FirmwareRecord record;record.kind="acpi";record.source=path;
        record.name=result.excluded||result.bytes.empty()?"":std::format("acpi/{}-{:04}.aml",dynamic?"dynamic":"static",++index);
        record.success=result.eof&&!result.excluded;record.status=record.success?"captured":result.failure.status;
        record.errorDomain=result.failure.domain;record.errorCode=result.failure.code;
        record.metadataJson="{\"source_leaf_hex\":"+firmwareQuote(rawHex(name))+",\"dynamic\":"+jsonBool(dynamic)+
            ",\"format\":\"raw ACPI table, not BIOS/EC flash\",\"non_atomic_snapshot\":true,"+captureMetadata(result)+","+acpiValidation(result);
        if(!result.excluded)record.metadataJson+=","+hashMetadata(result.bytes);
        record.metadataJson+="}";record.bytes=std::move(result.bytes);reader.send(std::move(record));
    }
}
void acpiTables(Reader& reader,int root){
    if(!reader.budget.canRead()){
        reader.failed("acpi","/sys/firmware/acpi/tables",reader.unavailable());
        reader.failed("acpi","/sys/firmware/acpi/tables/dynamic",reader.unavailable());return;
    }
    Failure failure;auto tables=walk(root,{"firmware","acpi","tables"},failure);
    if(tables.value<0){reader.failed("acpi","/sys/firmware/acpi/tables",failure);reader.failed("acpi","/sys/firmware/acpi/tables/dynamic",failure);return;}
    tableDirectory(reader,tables.value,false);
    if(!reader.budget.canRead()){reader.failed("acpi","/sys/firmware/acpi/tables/dynamic",reader.unavailable());return;}
    auto dynamic=directory(tables.value,"dynamic",sysfsMagic,failure);
    if(dynamic.value<0)reader.failed("acpi","/sys/firmware/acpi/tables/dynamic",failure);
    else tableDirectory(reader,dynamic.value,true);
}
void smbios(Reader& reader,int root){
    Failure failure;Fd tables;
    if(reader.budget.canRead())tables=walk(root,{"firmware","dmi","tables"},failure);else failure=reader.unavailable();
    for(const auto* leaf:{"smbios_entry_point","DMI"}){
        const std::string source=std::string("/sys/firmware/dmi/tables/")+leaf;
        if(tables.value<0){reader.failed("smbios",source,failure);continue;}
        auto result=readFile(reader,tables.value,leaf,sysfsMagic,std::strcmp(leaf,"DMI")==0?maximumFirmwareArtifactBytes:maximumEntryPointBytes);
        FirmwareRecord record;record.kind="smbios";record.source=source;
        record.name=result.bytes.empty()?"":std::strcmp(leaf,"DMI")==0?"smbios/linux-dmi.bin":"smbios/linux-entry.bin";
        record.success=result.eof;record.status=record.success?"captured":result.failure.status;record.errorDomain=result.failure.domain;record.errorCode=result.failure.code;
        record.metadataJson="{\"format\":\"Linux raw SMBIOS, no Windows RawSMBIOSData/RSMB wrapper\","+captureMetadata(result)+","+hashMetadata(result.bytes)+","+
            smbiosValidation(result,std::strcmp(leaf,"smbios_entry_point")==0,reader.budget)+"}";
        record.bytes=std::move(result.bytes);reader.send(std::move(record));
    }
}

struct Identity{BoardDescriptor board;std::string id,status;bool eligible{};};
Identity identity(Reader& reader,int root){
    Identity result;Failure failure;
    // /sys/class/dmi/id is normally a symlink. Use its fixed native target so
    // every source component can remain O_NOFOLLOW without following class links.
    auto dmi=walk(root,{"devices","virtual","dmi","id"},failure);
    if(dmi.value<0){result.status=failure.detail;return result;}
    for(const auto& [leaf,target]:std::array<std::pair<const char*,std::string*>,3>{{{"sys_vendor",&result.board.manufacturer},{"board_name",&result.board.baseBoard},{"product_name",&result.board.productName}}}){
        auto value=readFile(reader,dmi.value,leaf,sysfsMagic,maximumIdentityBytes);
        if(!value.eof){result.status=std::string(leaf)+": "+value.failure.detail;return result;}
        target->assign(value.bytes.begin(),value.bytes.end());
        const auto white=[](unsigned char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
        while(!target->empty()&&white(static_cast<unsigned char>(target->back())))target->pop_back();
        auto at=target->begin();while(at!=target->end()&&white(static_cast<unsigned char>(*at)))++at;target->erase(target->begin(),at);
    }
    const auto& profile=deviceProfile(result.board);result.id=profile.id;
    result.eligible=(result.id=="mechrevo-gm6ir0c"&&result.board.manufacturer=="MECHREVO"&&result.board.baseBoard=="GM6IR0C"&&result.board.productName=="JiguangX Series GM6IR0C")||
        (result.id=="eluktronics-hydroc16g2"&&result.board.manufacturer=="Eluktronics Inc."&&result.board.baseBoard=="HYDROC-16"&&result.board.productName=="HYDROC-16 G2");
    result.status=result.eligible?"exact actual SMBIOS matched reviewed OEM-variable profiles":"actual SMBIOS does not match either reviewed OEM-variable profile";
    return result;
}
std::string candidateMetadata(std::span<const std::uint8_t> payload,bool door){
    std::string metadata="\"verified_layout\":false,\"threshold_enforcement_verified\":false,\"candidate_raw_fields\":{";
    if(door)return metadata+"\"DoorByte0\":{\"payload_offset\":0,\"raw\":"+(payload.empty()?"null":std::to_string(payload[0]))+"}}";
    bool first=true;
    for(const auto [name,offset]:std::array<std::pair<const char*,std::size_t>,4>{{{"BatteryLimitation",0x30},{"ChargeMaximumLimit",0x31},{"ChargeMinimumLimit",0x32},{"APVersionCheck",0x43}}}){
        if(!first)metadata+=',';first=false;
        metadata+=firmwareQuote(name)+":{\"payload_offset\":"+std::to_string(offset)+",\"offset_hex\":"+firmwareQuote(std::format("0x{:02X}",offset))+
            ",\"raw\":"+(offset<payload.size()?std::to_string(payload[offset]):"null")+"}";
    }
    return metadata+"}";
}
void variables(Reader& reader,int root,const Identity& actual){
    Failure failure;Fd efivars;
    if(actual.eligible){
        if(!reader.budget.canRead())failure=reader.unavailable();
        else{auto efi=walk(root,{"firmware","efi"},failure);
            if(efi.value>=0)efivars=directory(efi.value,"efivars",efivarfsMagic,failure);}
    }
    for(const auto* name:variableNames){
        const std::string leaf=std::string(name)+"-"+std::string(variableGuid);
        const std::string source="/sys/firmware/efi/efivars/"+leaf;
        const auto base="\"variable_name\":"+firmwareQuote(name)+",\"vendor_guid\":"+firmwareQuote(variableGuid)+
            ",\"actual_profile\":"+firmwareQuote(actual.id)+",\"identity_status\":"+firmwareQuote(asciiDisplay(actual.status))+",\"identity_status_raw_hex\":"+firmwareQuote(rawHex(actual.status));
        if(!actual.eligible){reader.failed("uefi",source,policyFailure("not_applicable","OEM variable reads are restricted to exact actual GM6IR0C/HYDROC-16 G2; no foreign profile fallback"),base);continue;}
        if(efivars.value<0){reader.failed("uefi",source,failure,base);continue;}
        // Four leading attribute bytes belong to efivarfs, not the OEM bank.
        auto result=readFile(reader,efivars.value,leaf,efivarfsMagic,maximumVariablePayloadBytes+4);
        FirmwareRecord record;record.kind="uefi";record.source=source;record.errorDomain=result.failure.domain;record.errorCode=result.failure.code;
        record.status=result.eof?"captured":result.failure.status;
        record.metadataJson="{"+base+",\"raw_total_bytes\":"+(result.eof?std::to_string(result.bytes.size()):"null")+",\"raw_captured_bytes\":"+std::to_string(result.bytes.size())+",\"payload_offset\":4,\"attributes_format\":\"uint32 little-endian\","+
            captureMetadata(result)+","+hashMetadata(result.bytes,"raw_sha256");
        if(result.bytes.size()<4){
            record.success=false;if(result.eof){record.status="invalid_short_efivar";record.errorDomain="format_validation";}
            record.metadataJson+=",\"payload_bytes\":null,\"attributes_available\":false,\"raw_prefix_hex\":"+firmwareQuote(rawHex(std::string_view(reinterpret_cast<const char*>(result.bytes.data()),result.bytes.size())))+"}";
        }else{
            record.attributes=little32(result.bytes,0);const auto payload=std::span<const std::uint8_t>(result.bytes).subspan(4);
            record.name=std::string("uefi/")+name+".bin";record.success=result.eof;
            record.metadataJson+=",\"attributes_available\":true,\"attributes_hex\":"+firmwareQuote(std::format("0x{:08X}",*record.attributes))+
                ",\"payload_bytes\":"+std::to_string(payload.size())+","+hashMetadata(payload,"payload_sha256")+","+candidateMetadata(payload,std::strcmp(name,"OemMagicDoor")==0)+"}";
            record.bytes.assign(payload.begin(),payload.end());
        }
        reader.send(std::move(record));
    }
}
}

void collectPlatformFirmware(bool stateOnly,const FirmwareEmitter& emit){
    Reader reader(emit);Failure failure;auto root=sysRoot(failure);
    if(root.value<0){
        if(!stateOnly){reader.failed("acpi","/sys/firmware/acpi/tables",failure);reader.failed("acpi","/sys/firmware/acpi/tables/dynamic",failure);reader.failed("smbios","/sys/firmware/dmi/tables/smbios_entry_point",failure);reader.failed("smbios","/sys/firmware/dmi/tables/DMI",failure);}
        Identity unavailable;unavailable.status="trusted sysfs/actual SMBIOS unavailable: "+failure.detail;variables(reader,-1,unavailable);return;
    }
    const auto actual=identity(reader,root.value);
    // Capture the small before/after policy banks before a large/slow ACPI
    // source can consume the whole cooperative budget. No fallback bank is used.
    variables(reader,root.value,actual);
    if(!stateOnly){acpiTables(reader,root.value);smbios(reader,root.value);}
    // Completion/final belongs to the common wire server. No filesystem writes,
    // mount/chmod/chattr, AML evaluation, physical memory or EC access occur here.
}
}
