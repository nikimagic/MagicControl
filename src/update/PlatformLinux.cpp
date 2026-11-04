#include "Platform.h"
#include <glib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <array>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

extern char** environ;

namespace mc::update::platform {
std::filesystem::path executable(){return std::filesystem::read_symlink("/proc/self/exe");}
std::string curl(){return "/usr/bin/curl";}
std::string pid(){return std::to_string(getpid());}
void replace(const std::filesystem::path& from,const std::filesystem::path& to){std::filesystem::rename(from,to);}
std::string run(const std::vector<std::string>& args,std::stop_token stop,int timeout){
    int pipes[2];if(pipe2(pipes,O_CLOEXEC))throw std::runtime_error("Ошибка канала обновления");
    std::vector<char*> argv;for(const auto& a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,pipes[1],STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,pipes[1],STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions,pipes[0]);posix_spawn_file_actions_addclose(&actions,pipes[1]);
    posix_spawnattr_t attr;posix_spawnattr_init(&attr);posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attr,0);
    pid_t child{};const auto launched=posix_spawn(&child,argv[0],&actions,&attr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attr);
    if(launched){close(pipes[0]);close(pipes[1]);throw std::runtime_error("Не удалось запустить инструмент обновления; нужен curl/tar");}
    close(pipes[1]);fcntl(pipes[0],F_SETFL,O_NONBLOCK);std::string output;int status{};bool exited=false;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(timeout);
    for(;;){char buffer[8192];ssize_t n;while((n=::read(pipes[0],buffer,sizeof(buffer)))>0)output.append(buffer,static_cast<std::size_t>(n));
        if(!exited)exited=waitpid(child,&status,WNOHANG)==child;
        if(exited&&n==0)break;
        if(stop.stop_requested()||std::chrono::steady_clock::now()>deadline||output.size()>4*1024*1024){kill(-child,SIGKILL);if(!exited)waitpid(child,&status,0);close(pipes[0]);throw std::runtime_error("Обновление отменено, превышен размер или время ожидания");}
        pollfd p{pipes[0],POLLIN,0};poll(&p,1,30);
    }
    close(pipes[0]);if(!WIFEXITED(status)||WEXITSTATUS(status))throw std::runtime_error("Ошибка загрузки/подготовки. Нужны curl и tar. "+output.substr(0,500));return output;
}
void launch(const std::filesystem::path& exe,const std::vector<std::string>& args){
    std::vector<std::string> all{exe.string()};all.insert(all.end(),args.begin(),args.end());std::vector<char*> argv;
    for(auto& a:all)argv.push_back(a.data());
    argv.push_back(nullptr);GError* error{};
    if(!g_spawn_async(exe.parent_path().c_str(),argv.data(),nullptr,G_SPAWN_DEFAULT,nullptr,nullptr,nullptr,&error)){
        std::string text=error?error->message:"Ошибка запуска";g_clear_error(&error);throw std::runtime_error(text);}
}
bool waitForParent(const std::string& value){
    if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)return false;
    const auto parent=static_cast<pid_t>(std::stol(value));if(parent<=1||parent==getpid())return false;
    for(int i=0;i<1200;++i){if(kill(parent,0)&&errno==ESRCH)return true;std::this_thread::sleep_for(std::chrono::milliseconds(100));}return false;
}
bool linkLike(const std::filesystem::path& path){return std::filesystem::is_symlink(std::filesystem::symlink_status(path));}
void removeEntry(const std::filesystem::path& path){if(!std::filesystem::remove(path))throw std::runtime_error("Не удалось удалить файл обновления: "+path.string());}
bool waitForUpdateHelpers(const std::filesystem::path& folder){
    const auto base=std::filesystem::canonical(folder);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    for(const auto& entry:std::filesystem::directory_iterator("/proc")){
        const auto id=entry.path().filename().string();if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos)continue;
        const auto process=static_cast<pid_t>(std::stol(id));if(process==getpid())continue;
        struct stat status{};if(stat(entry.path().c_str(),&status)||status.st_uid!=geteuid())continue;
        std::ifstream input(entry.path()/"cmdline",std::ios::binary);std::vector<std::string> args;std::string arg;
        while(std::getline(input,arg,'\0'))args.push_back(arg);
        if(args.size()!=3||args[1]!="--apply-update")continue;
        std::error_code error;const auto stage=std::filesystem::weakly_canonical(args[2],error);
        if(error||stage.parent_path()!=base)continue;
        while(!kill(process,0)||errno!=ESRCH){
            if(std::chrono::steady_clock::now()>=deadline)return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    return true;
}
std::string sha256(const std::filesystem::path& path){
    auto* checksum=g_checksum_new(G_CHECKSUM_SHA256);if(!checksum)throw std::runtime_error("SHA256 недоступен");
    struct Cleanup{GChecksum* p;~Cleanup(){g_checksum_free(p);}} cleanup{checksum};std::ifstream f(path,std::ios::binary);
    if(!f)throw std::runtime_error("Не удалось прочитать пакет");
    std::array<char,65536> buffer{};
    while(f){f.read(buffer.data(),buffer.size());if(f.gcount())g_checksum_update(checksum,reinterpret_cast<const guchar*>(buffer.data()),f.gcount());}
    if(!f.eof())throw std::runtime_error("Ошибка чтения SHA256");
    return g_checksum_get_string(checksum);
}
void extract(const std::filesystem::path& archive,const std::filesystem::path& destination,std::stop_token stop){
    const auto names=run({"/usr/bin/tar","-tzf",archive.string()},stop);
    std::istringstream listing(names);std::string name;int count{};
    while(std::getline(listing,name)){
        if(++count>1000||name.empty()||name.front()=='/'||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos)throw std::runtime_error("Неверный путь TAR");
        std::istringstream parts(name);std::string part;while(std::getline(parts,part,'/'))if(part==".."||part==".")throw std::runtime_error("Неверный путь TAR");
    }
    const auto details=run({"/usr/bin/tar","--numeric-owner","--full-time","-tvzf",archive.string()},stop);
    std::istringstream rows(details);std::string line;std::uintmax_t total{};
    while(std::getline(rows,line)){
        if(line.empty()||(line.front()!='-'&&line.front()!='d'))throw std::runtime_error("Ссылки и специальные файлы TAR недопустимы");
        std::istringstream row(line);std::string mode,owner;std::uintmax_t size{};row>>mode>>owner>>size;if(!row)throw std::runtime_error("Неверный список TAR");
        total+=size;if(total>256*1024*1024)throw std::runtime_error("TAR слишком большой");
    }
    run({"/usr/bin/tar","--no-same-owner","--no-same-permissions","-xzf",archive.string(),"-C",destination.string()},stop,120);
}
}
