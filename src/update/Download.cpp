#include "Platform.h"
#include "MagicControl/Version.h"
#include <stdexcept>

namespace mc::update::platform {
void download(const std::string& url,const std::filesystem::path& output,std::uintmax_t limit,std::stop_token stop){
    if(!url.starts_with("https://"))throw std::runtime_error("Обновления доступны только через HTTPS");
    const auto path = output.u8string();
    const std::string target(path.begin(), path.end());
    run({curl(),"--fail","--location","--silent","--show-error","--proto","=https","--proto-redir","=https",
        "--connect-timeout","15","--max-time","180","--max-redirs","5","--max-filesize",std::to_string(limit),
        "--user-agent","MagicControl/" MAGICCONTROL_VERSION_STRING,"--header","Accept: application/vnd.github+json",
        "--output",target,url},stop,190);
    if(std::filesystem::file_size(output)>limit)throw std::runtime_error("Превышен размер загрузки");
}

}
