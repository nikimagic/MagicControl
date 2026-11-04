#include "MagicControl/AppUpdate.h"
#include "MagicControl/ResourceControl.h"
#include "MagicControl/FileStorage.h"
#include <mutex>
#include "MagicControl/Version.h"
#include "MagicControl/Json.h"
#include "Platform.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace mc::update {
using json::Json;using json::JsonReader;
namespace fs=std::filesystem;
namespace {
constexpr std::uintmax_t maxPackage=128*1024*1024;
constexpr std::string_view packagePrefix="MagicControl-";
#ifdef _WIN32
constexpr char suffix[]="-windows-x64-portable.zip";
constexpr char appName[]="MagicControl.exe";
constexpr char packagePlatform[]="windows";
#else
constexpr char suffix[]="-linux-x64-portable.tar.gz";
constexpr char appName[]="MagicControl";
constexpr char packagePlatform[]="linux";
#endif
std::string pathText(const fs::path& p){const auto u=p.u8string();return {u.begin(),u.end()};}
fs::path pathFromText(const std::string& s){return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()),s.size()));}
std::string read(const fs::path& path,std::uintmax_t limit=2*1024*1024){
    if(fs::file_size(path)>limit)throw std::runtime_error("Файл обновления слишком большой");
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Не удалось прочитать файл обновления");
    return {std::istreambuf_iterator<char>(f),{}};
}
void write(const fs::path& path,const std::string& text){std::ofstream f(path,std::ios::binary);f<<text;if(!f)throw std::runtime_error("Нет доступа к папке обновления");}
void cancel(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Обновление отменено");}
bool hashValid(const std::string& s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
struct Version {std::array<unsigned long,3> n{};std::string prerelease;};
std::optional<Version> version(std::string text){
    static const std::regex pattern(R"(^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$)");
    std::smatch m;if(!std::regex_match(text,m,pattern))return {};
    try{return Version{{std::stoul(m[1]),std::stoul(m[2]),std::stoul(m[3])},m[4]};}catch(...){return {};}
}
bool newer(const Version& a,const Version& b){
    if(a.n!=b.n)return a.n>b.n;
    if(a.prerelease.empty()||b.prerelease.empty())return a.prerelease.empty()&&!b.prerelease.empty();
    std::istringstream aa(a.prerelease),bb(b.prerelease);std::string x,y;
    for(;;){const bool ax=bool(std::getline(aa,x,'.')),by=bool(std::getline(bb,y,'.'));if(!ax||!by)return ax&&!by;
        if(x==y)continue;
        auto numeric=[](const std::string& s){return !s.empty()&&s.find_first_not_of("0123456789")==std::string::npos;};
        const bool nx=numeric(x),ny=numeric(y);if(nx!=ny)return !nx;
        if(nx&&x.size()!=y.size())return x.size()>y.size();
        return x>y;
    }
}
fs::path updateFolder(){return platform::executable().parent_path()/"magiccontrol-update";}
bool updateFolderName(const fs::path& p){return p=="magiccontrol-update"||p==".magiccontrol-update";}
std::shared_ptr<void> updateLease(const fs::path& root){
    const auto locks=root/"locks";fs::create_directories(locks);
    if(platform::linkLike(locks)||fs::canonical(locks).parent_path()!=fs::canonical(root))throw std::runtime_error("Неверная папка блокировки обновления");
    std::string error;auto lease=detail::lockStorageFile(locks/"update-transaction.lock",false,error);
    if(!lease)throw std::runtime_error(error.empty()?"Обновление уже выполняется другим процессом":error);
    return lease;
}
std::optional<Version> archiveVersion(const std::string& name){
    if(!name.starts_with(packagePrefix)||!name.ends_with(suffix))return {};
    return version(name.substr(packagePrefix.size(),name.size()-packagePrefix.size()-std::string_view(suffix).size()));
}
Release localRelease(std::stop_token stop){
    Release result;result.version=MAGICCONTROL_VERSION_STRING;
    const auto current=version(result.version);if(!current)throw std::runtime_error("Версия приложения не распознана");
    auto best=*current;
    for(const auto& base:std::array{updateFolder(),platform::executable().parent_path()/".magiccontrol-update"}){
    if(!fs::exists(base))continue;
    if(platform::linkLike(base)||!fs::is_directory(base))throw std::runtime_error("Неверная папка локальных обновлений");
    // Only packages placed directly in this folder; never old staging/backup trees.
    for(const auto& entry:fs::directory_iterator(base)){
        cancel(stop);if(!fs::is_regular_file(entry.symlink_status()))continue;
        const auto name=pathText(entry.path().filename());const auto v=archiveVersion(name);
        if(!v||!newer(*v,best))continue;
        const auto size=entry.file_size();if(!size||size>maxPackage)continue;
        result.available=true;result.version=name.substr(packagePrefix.size(),name.size()-packagePrefix.size()-std::string_view(suffix).size());
        result.archiveName=name;result.localArchive=entry.path();result.preview=!v->prerelease.empty();best=*v;
    }
    }
    if(result.available){cancel(stop);result.digest="sha256:"+platform::sha256(result.localArchive);}
    return result;
}
fs::path staging(){
    const auto base=updateFolder();
    fs::create_directories(base);
    if(platform::linkLike(base)||fs::canonical(base).parent_path()!=fs::canonical(platform::executable().parent_path()))throw std::runtime_error("Папка обновлений не должна быть ссылкой");
#ifndef _WIN32
    fs::permissions(base,fs::perms::owner_all,fs::perm_options::replace);
#endif
    for(int i=0;i<20;++i){const auto name=platform::pid()+"-"+std::to_string(std::random_device{}());auto dir=base/name;
        if(fs::create_directory(dir))return dir;}
    throw std::runtime_error("Не удалось создать папку обновления");
}
using platform::download;
bool relativeSafe(const std::string& name){
    if(name.empty()||name.front()=='/'||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))return false;
    const auto path=pathFromText(name);if(path.is_absolute())return false;
    for(const auto& part:path)if(part==".."||part==".")return false;
    return true;
}
// The fixed archive root is accepted alongside previously published roots.
// It is exactly one path component; manifest platform/version/hash checks still apply.
bool packageRootSafe(const std::string& name){
    return relativeSafe(name)&&name.find('/')==std::string::npos&&
        (name=="MagicControl"||name.starts_with(packagePrefix));
}
bool preserve(const fs::path& path){
    const auto ext=path.extension().string();const auto name=path.filename().string();
    if(path.begin()!=path.end()&&*path.begin()=="settings")return true;
    return ext==".ini"||ext==".csv"||ext==".log"||name=="battery-enforcement-platform.txt"||name=="rom_id"||name=="platform_id"||name=="project_id";
}
struct File {fs::path relative;std::string hash;std::uintmax_t size;};
std::vector<File> manifest(const fs::path& package,const std::string& expectedVersion,bool installed=false){
    const auto data=JsonReader::parse(read(package/"package-manifest.json"));
    if(data.at("product").text()!="MagicControl"||data.at("platform").text()!=packagePlatform)throw std::runtime_error("Пакет другой программы или ОС");
    if(data.at("version").text()!=expectedVersion)throw std::runtime_error("Версия в манифесте не совпадает с выпуском");
    std::vector<File> files;std::set<std::string> seen;std::uintmax_t total{};
    for(const auto& entry:data.at("files").array()){
        const auto name=entry.at("path").text();const auto hash=entry.at("sha256").text();const auto size=entry.at("bytes").number();
        if(!relativeSafe(name)||!hashValid(hash)||size<0||size>256*1024*1024||!seen.insert(name).second)throw std::runtime_error("Некорректный манифест пакета");
        const auto path=pathFromText(name);const auto source=package/path;
        if(!(installed&&preserve(path))&&(!fs::is_regular_file(fs::symlink_status(source))||fs::file_size(source)!=static_cast<std::uintmax_t>(size)||platform::sha256(source)!=hash))
            throw std::runtime_error("SHA256 файла пакета не совпадает: "+name);
        total+=static_cast<std::uintmax_t>(size);if(total>256*1024*1024||files.size()>1000)throw std::runtime_error("Пакет превышает допустимый размер");
        files.push_back({path,hash,static_cast<std::uintmax_t>(size)});
    }
    if(!seen.contains(appName))throw std::runtime_error("В пакете нет MagicControl");
    files.push_back({"package-manifest.json",platform::sha256(package/"package-manifest.json"),fs::file_size(package/"package-manifest.json")});
    return files;
}
void rejectLinks(const fs::path& root,const fs::path& relative){
    auto p=root;for(const auto& part:relative){p/=part;if(platform::linkLike(p))throw std::runtime_error("Ссылки в пути установки недопустимы");}
}
#ifdef _WIN32
void startWindowsInstaller(const fs::path& directory,const std::string& incoming,std::stop_token stop={}){
    const auto dir=fs::canonical(directory),root=dir.parent_path().parent_path();
    if(!updateFolderName(dir.parent_path().filename())||platform::linkLike(directory))throw std::runtime_error("Неверный путь обновления");
    const auto order=compareVersions(incoming,MAGICCONTROL_VERSION_STRING);
    if(!order||*order<=0)throw std::runtime_error("Пакет не является новой версией");
    const auto name=read(dir/"package-root.txt",256);
    if(!packageRootSafe(name))throw std::runtime_error("Неверный путь пакета");
    const auto package=dir/"unpacked"/pathFromText(name);rejectLinks(dir,fs::path("unpacked")/pathFromText(name));
    const auto files=manifest(package,incoming);
    std::ostringstream plan;plan<<"{\"product\":\"MagicControl\",\"platform\":\"windows\",\"version\":"<<std::quoted(incoming)
        <<",\"previous_version\":\"" MAGICCONTROL_VERSION_STRING "\",\"package\":"<<std::quoted(name)
        <<",\"manifest_sha256\":\""<<platform::sha256(package/"package-manifest.json")<<"\",\"files\":[";
    bool first=true;
    for(const auto& file:files){if(preserve(file.relative))continue;
        if(!first)plan<<',';first=false;
        plan<<"{\"path\":"<<std::quoted(pathText(file.relative))<<",\"bytes\":"<<file.size<<",\"sha256\":\""<<file.hash<<"\"}";
    }
    plan<<"]}";write(dir/"install-plan.json",plan.str());
    cancel(stop);platform::launchInstaller(root,dir);
}
#endif
bool below(const fs::path& path,const fs::path& base){
    const auto relative=path.lexically_relative(base);
    return !relative.empty()&&!relative.is_absolute()&&*relative.begin()!="..";
}
void removeUpdateEntry(const fs::path& path,const fs::path& base){
    // Validate the resolved parent BEFORE recursion. Reparse points/symlinks
    // are removed themselves and are never followed outside this exact cache.
    if(path.lexically_normal()==base||!below(path.lexically_normal(),base)||!below(fs::canonical(path.parent_path()),base))
        throw std::runtime_error("Очистка вышла за папку обновлений");
    if(!platform::linkLike(path)&&fs::is_directory(fs::symlink_status(path))){
        if(!below(fs::canonical(path),base))throw std::runtime_error("Неверная папка очистки");
        std::vector<fs::path> children;for(const auto& entry:fs::directory_iterator(path))children.push_back(entry.path());
        for(const auto& child:children)removeUpdateEntry(child,base);
    }
    platform::removeEntry(path);
}
void finishSuccessfulUpdate(std::string_view folderName){
    // The NEW executable runs this before GUI/hardware initialization. This
    // also finalizes a first upgrade installed by an older --apply-update
    // helper, which has no cleanup stage and launches the new app without args.
    fs::path root;bool cleanupStarted{};
    try{
        root=fs::canonical(platform::executable().parent_path());const auto folder=root/fs::path(folderName);
        const bool legacy=folderName==".magiccontrol-update";
        const auto receipt=root/"locks"/(legacy?"update-cleanup-legacy.json":"update-cleanup.json");
        const auto legacyReceipt=root/".locks"/"update-cleanup.json";
        if(!fs::exists(folder))return;
        if(platform::linkLike(folder)||fs::canonical(folder)!=folder)throw std::runtime_error("Неверная папка очистки обновления");
        if(!fs::is_directory(folder)||(fs::is_empty(folder)&&!fs::exists(receipt)&&!(legacy&&fs::exists(legacyReceipt))))return;
        if(platform::linkLike(root/"update-result.txt"))return;
        if(read(root/"update-result.txt",8192).find("Обновление установлено.")!=0)return;
        std::shared_ptr<void> lease;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        do{try{lease=updateLease(root);}catch(const std::exception&){
            if(std::chrono::steady_clock::now()>=deadline)throw;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }}while(!lease);
        if(!platform::waitForUpdateHelpers(folder))throw std::runtime_error("Помощник обновления ещё занят; очистка отложена");
        const auto installedManifest=platform::sha256(root/"package-manifest.json");
        if(platform::linkLike(receipt))throw std::runtime_error("Неверный файл стадии очистки");
        bool committed{};
        try{const auto saved=JsonReader::parse(read(receipt,4096));
            committed=saved.at("version").text()==MAGICCONTROL_VERSION_STRING&&saved.at("manifest_sha256").text()==installedManifest;
        }catch(const std::exception&){}
        bool oldReceiptUsed{};
        if(legacy&&!committed&&!platform::linkLike(root/".locks")&&!platform::linkLike(legacyReceipt))try{
            const auto saved=JsonReader::parse(read(legacyReceipt,4096));
            committed=saved.at("version").text()==MAGICCONTROL_VERSION_STRING&&saved.at("manifest_sha256").text()==installedManifest;
            oldReceiptUsed=committed;
        }catch(const std::exception&){}
        for(const auto& entry:fs::directory_iterator(folder)){
            if(committed)break;
            if(platform::linkLike(entry.path())||!entry.is_directory())continue;
            try{
                const auto dir=fs::canonical(entry.path());if(dir.parent_path()!=folder)continue;
                if(read(dir/"version.txt",128)!=MAGICCONTROL_VERSION_STRING)continue;
                const auto name=read(dir/"package-root.txt",256);if(!packageRootSafe(name))continue;
                const auto package=dir/"unpacked"/pathFromText(name);rejectLinks(dir,fs::path("unpacked")/pathFromText(name)/"package-manifest.json");
                if(platform::sha256(package/"package-manifest.json")!=installedManifest)continue;
                committed=true;break;
            }catch(const std::exception&){continue;}
        }
        if(!committed)return; // Failed/partial installs retain all rollback data.
        // Matching final manifest/receipt is not enough: check every installed
        // payload again, excluding the settings deliberately preserved.
        const auto files=manifest(root,MAGICCONTROL_VERSION_STRING,true);
        for(const auto& file:files)if(!preserve(file.relative))rejectLinks(root,file.relative);
        // A durable receipt OUTSIDE the cache makes interrupted cleanup
        // retryable even after its own unpacked proof has been deleted.
        std::string error;
        if(!detail::writeStorageTextAtomically(receipt,"{\"version\":\"" MAGICCONTROL_VERSION_STRING "\",\"manifest_sha256\":\""+installedManifest+"\"}\n",error))
            throw std::runtime_error("Не удалось сохранить стадию очистки: "+error);
        cleanupStarted=true;
        std::vector<fs::path> children;for(const auto& entry:fs::directory_iterator(folder))children.push_back(entry.path());
        for(const auto& child:children)removeUpdateEntry(child,folder);
        if(!fs::is_empty(folder))throw std::runtime_error("Папка обновлений очищена не полностью");
        fs::remove(receipt);
        if(oldReceiptUsed)fs::remove(legacyReceipt);
        write(root/"update-result.txt","Обновление установлено. Настройки и кривые сохранены. Папка обновлений очищена.");
    }catch(const std::exception& error){
        // Cleanup failure never rolls back an already installed valid version.
        try{if(cleanupStarted)write(root/"update-result.txt",std::string("Обновление установлено. Очистка отложена: ")+error.what());}catch(...){}
    }
}
}
std::optional<int> compareVersions(const std::string& left,const std::string& right){
    const auto a=version(left),b=version(right);if(!a||!b)return {};
    return newer(*a,*b)?1:newer(*b,*a)?-1:0;
}
Release check(std::stop_token stop){
    const auto lease=updateLease(platform::executable().parent_path());
    auto result=localRelease(stop);
    const auto dir=staging();struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{dir};
    try{
    download("https://api.github.com/repos/nikimagic/MagicControl/releases?per_page=100",dir/"releases.json",2*1024*1024,stop);
    const auto root=JsonReader::parse(read(dir/"releases.json"));
    if(!std::holds_alternative<Json::Array>(root.value))throw std::runtime_error("GitHub вернул неожиданный ответ");
    const auto current=version(MAGICCONTROL_VERSION_STRING);if(!current)throw std::runtime_error("Версия приложения не распознана");
    Version best=result.available?*version(result.version):*current;
    for(const auto& item:root.array()){
        cancel(stop);if(item.at("draft").flag())continue;const auto tag=item.at("tag_name").text();const auto v=version(tag);
        if(!v)continue;
        Release candidate;candidate.tag=tag;candidate.version=tag.starts_with('v')?tag.substr(1):tag;
        candidate.pageUrl=std::string(repository)+"/releases/tag/"+tag;candidate.preview=item.at("prerelease").flag();
        const std::string prefix=std::string(repository)+"/releases/download/"+tag+"/";
        for(const auto& asset:item.at("assets").array()){
            const auto name=asset.at("name").text(),url=asset.at("browser_download_url").text();
            if(asset.at("state").text()!="uploaded"||url!=prefix+name)continue;
            if(name.starts_with("MagicControl-")&&name.ends_with(suffix)&&name.find('/')==std::string::npos){
                candidate.archiveName=name;candidate.archiveUrl=url;candidate.digest=asset.at("digest").text();
            }else if(name=="SHA256SUMS.txt")candidate.checksumUrl=url;
        }
        if(candidate.archiveUrl.empty()||candidate.checksumUrl.empty())continue;
        candidate.version=candidate.archiveName.substr(std::string("MagicControl-").size(),candidate.archiveName.size()-std::string("MagicControl-").size()-std::string(suffix).size());
        const auto assetVersion=version(candidate.version);if(!assetVersion||!newer(*assetVersion,best))continue;
        candidate.available=true;result=candidate;best=*assetVersion;
    }
    }catch(...){cancel(stop);if(!result.available||result.localArchive.empty())throw;result.githubUnavailable=true;}
    return result;
}
void prepareAndLaunch(const Release& release,std::stop_token stop,const std::function<void(std::string)>& progress){
    const auto lease=updateLease(platform::executable().parent_path());
    if(!release.available)throw std::runtime_error("Нет новой версии для установки");
    const auto order=compareVersions(release.version,MAGICCONTROL_VERSION_STRING);
    const auto named=archiveVersion(release.archiveName),requested=version(release.version);
    if(!order||*order<=0||!named||!requested||newer(*named,*requested)||newer(*requested,*named)||!relativeSafe(release.archiveName))throw std::runtime_error("Пакет не является новой версией");
    const auto self=platform::executable();if(self.filename()!=appName)throw std::runtime_error("Верните стандартное имя файла MagicControl перед обновлением");
    const auto dir=staging();const auto archive=dir/pathFromText(release.archiveName);
    std::string expected;
    if(!release.localArchive.empty()){
        const auto base=fs::canonical(release.localArchive.parent_path());
        if(base.parent_path()!=fs::canonical(self.parent_path())||!updateFolderName(base.filename())||platform::linkLike(release.localArchive.parent_path())||
           !fs::is_regular_file(fs::symlink_status(release.localArchive))||
           fs::canonical(release.localArchive).parent_path()!=base||pathText(release.localArchive.filename())!=release.archiveName)
            throw std::runtime_error("Неверный путь локального пакета");
        if(!release.digest.starts_with("sha256:")||!hashValid(release.digest.substr(7)))throw std::runtime_error("Нет SHA256 локального пакета");
        expected=release.digest.substr(7);
        if(fs::file_size(release.localArchive)>maxPackage)throw std::runtime_error("Файл обновления слишком большой");
        cancel(stop);progress("Копирование локального пакета…");fs::copy_file(release.localArchive,archive);cancel(stop);
    }else{
    progress("Загрузка контрольных сумм…");download(release.checksumUrl,dir/"SHA256SUMS.txt",65536,stop);
    std::string line;std::istringstream sums(read(dir/"SHA256SUMS.txt"));
    while(std::getline(sums,line)){std::istringstream row(line);std::string hash,name;row>>hash>>name;if(name==release.archiveName){if(!expected.empty())throw std::runtime_error("Повтор SHA256 в списке");expected=hash;}}
    if(!hashValid(expected))throw std::runtime_error("В выпуске нет SHA256 для этого пакета");
    if(!release.digest.empty()&&release.digest!="sha256:"+expected)throw std::runtime_error("SHA256 GitHub и списка не совпадают");
    progress("Загрузка версии "+release.version+"…");download(release.archiveUrl,archive,maxPackage,stop);
    }
    progress("Проверка SHA256…");if(platform::sha256(archive)!=expected)throw std::runtime_error("SHA256 архива не совпадает; установка отменена");
    cancel(stop);fs::create_directory(dir/"unpacked");progress("Подготовка файлов…");platform::extract(archive,dir/"unpacked",stop);
    std::vector<fs::path> roots;for(const auto& entry:fs::directory_iterator(dir/"unpacked"))roots.push_back(entry.path());
    if(roots.size()!=1||!fs::is_directory(roots[0])||fs::is_symlink(fs::symlink_status(roots[0])))throw std::runtime_error("Неверная структура portable-пакета");
    const auto rootName=roots[0].filename().string();if(!packageRootSafe(rootName))throw std::runtime_error("Неверное имя пакета");
    for(const auto& entry:fs::recursive_directory_iterator(roots[0]))if(fs::is_symlink(entry.symlink_status()))throw std::runtime_error("Ссылки в пакете недопустимы");
    static_cast<void>(manifest(roots[0],release.version));
    write(dir/"package-root.txt",rootName);write(dir/"parent-pid.txt",platform::pid());write(dir/"version.txt",release.version);
    // Verify destination permissions before asking the running app to exit.
    cancel(stop);
#ifdef _WIN32
    startWindowsInstaller(dir,release.version,stop);
#else
    platform::launch(self,{"--apply-update",pathText(dir)});
#endif
}
std::string lastResult(){try{return read(platform::executable().parent_path()/"update-result.txt",8192);}catch(...){return {};}}
std::optional<int> runHelper(const std::vector<std::string>& args){
    if(args.size()==1){finishSuccessfulUpdate("magiccontrol-update");finishSuccessfulUpdate(".magiccontrol-update");return {};}
    if(args.size()<2||args[1]!="--apply-update")return {};
    if(args.size()!=3)return 2;
#ifdef _WIN32
    // Compatibility with already deployed/manual adapters: the same prepared
    // plan and script, not a second Windows file-install engine.
    try{const auto dir=fs::canonical(pathFromText(args[2]));
        const auto self=fs::canonical(platform::executable());
        if(self.parent_path()!=dir&&self.parent_path()!=dir.parent_path().parent_path())return 2;
        startWindowsInstaller(dir,read(dir/"version.txt",128));return 0;
    }catch(...){return 1;}
#else
    fs::path dir,root;bool parentExited=false;std::shared_ptr<void> lease;
    std::vector<std::pair<fs::path,bool>> changed;
    try{
        dir=fs::canonical(pathFromText(args[2]));root=dir.parent_path().parent_path();
        if(!updateFolderName(dir.parent_path().filename())||fs::is_symlink(fs::symlink_status(dir)))throw std::runtime_error("Неверный путь обновления");
        const auto self=fs::canonical(platform::executable());
        if(self.parent_path()!=root)throw std::runtime_error("Неверное расположение приложения");
        if(!platform::waitForParent(read(dir/"parent-pid.txt",32)))throw std::runtime_error("Программа не завершилась; обновление отменено");
        parentExited=true;const auto name=read(dir/"package-root.txt",256);if(!packageRootSafe(name))throw std::runtime_error("Неверный путь пакета");
        lease=updateLease(root);
        const auto incoming=read(dir/"version.txt",128);const auto incomingVersion=version(incoming),currentVersion=version(MAGICCONTROL_VERSION_STRING);
        if(!incomingVersion||!currentVersion||!newer(*incomingVersion,*currentVersion))throw std::runtime_error("Пакет не является новой версией");
        const auto package=dir/"unpacked"/pathFromText(name);const auto files=manifest(package,incoming);
        fs::create_directory(dir/"backup");
        for(const auto& file:files){
            if(preserve(file.relative))continue;
            rejectLinks(root,file.relative);
            rejectLinks(package,file.relative);
            const auto target=root/file.relative,backup=dir/"backup"/file.relative;
            if(fs::exists(target)&&!fs::is_regular_file(target))throw std::runtime_error("Целевой путь не является файлом");
            fs::create_directories(target.parent_path());fs::create_directories(backup.parent_path());
            const bool existed=fs::exists(target);if(existed)fs::copy_file(target,backup);
            const auto next=dir/"next"/file.relative;fs::create_directories(next.parent_path());
            fs::copy_file(package/file.relative,next);changed.emplace_back(file.relative,existed);
            platform::replace(next,target);
        }
        write(root/"update-result.txt","Обновление установлено. Настройки и кривые сохранены.");
        platform::launch(root/appName,{});return 0;
    }catch(const std::exception& e){
        bool restored=true;
        for(auto it=changed.rbegin();it!=changed.rend();++it){try{const auto target=root/it->first;
            if(it->second){const auto back=dir/"restore"/it->first;fs::create_directories(back.parent_path());fs::copy_file(dir/"backup"/it->first,back);platform::replace(back,target);}
            else if(fs::exists(target))fs::remove(target);
        }catch(...){restored=false;}}
        try{if(parentExited){write(root/"update-result.txt",std::string("Обновление не установлено: ")+e.what()+(restored?"\nПредыдущая версия восстановлена.":"\nВосстановление неполное; резервная копия находится в "+pathText(dir/"backup")));platform::launch(root/appName,{});}}catch(...){}
        return 1;
    }
#endif
}
struct Operations::Impl {
    ResourceWorker worker;std::stop_source stop;std::mutex gate;OperationState state;
    void progress(std::string text){std::lock_guard lock(gate);state.message=std::move(text);state.releaseMessage=false;}
};
Operations::Operations():impl_(std::make_unique<Impl>()){}
Operations::~Operations(){shutdown();}
void Operations::cancel(){impl_->stop.request_stop();}
void Operations::shutdown(){cancel();impl_->worker.join();}
bool Operations::start(bool install){
    auto& i=*impl_;if(i.worker.busy())return false;
    i.stop=std::stop_source{};
    Release selected;
    {std::lock_guard lock(i.gate);selected=i.state.release;i.state.busy=true;i.state.done=false;
     i.state.message=install?"Подготовка обновления…":"Проверяю папку обновлений и GitHub…";i.state.releaseMessage=false;}
    try{i.worker.launch([&i,install,selected=std::move(selected),token=i.stop.get_token()]{
        try{
            if(install){prepareAndLaunch(selected,token,[&i](std::string text){i.progress(std::move(text));});
                std::lock_guard lock(i.gate);i.state.ready=true;i.state.message="Пакет проверен. Перезапускаю приложение…";i.state.releaseMessage=false;}
            else{auto release=check(token);std::lock_guard lock(i.gate);i.state.release=std::move(release);i.state.releaseMessage=true;}
        }catch(const std::exception& error){i.progress(error.what());}
        catch(...){i.progress("Ошибка обработчика обновления.");}
        std::lock_guard lock(i.gate);i.state.done=true;
    },{},ResourceWorkClass::Interactive);}
    catch(...){std::lock_guard lock(i.gate);i.state.busy=false;i.state.done=true;i.state.message="Не удалось запустить обработчик обновления.";}
    return true;
}
OperationState Operations::snapshot(){
    auto& i=*impl_;
    if(i.worker.takeCompletion()){
        std::lock_guard lock(i.gate);i.state.busy=false;
        if(i.worker.failure()){i.state.done=true;i.state.ready=false;i.state.releaseMessage=false;i.state.message="Ошибка обработчика обновления.";}
    }
    std::lock_guard lock(i.gate);return i.state;
}

}
