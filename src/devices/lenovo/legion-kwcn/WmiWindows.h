#pragma once
#include "../../../protocols/lenovo/LegionWmi.h"
#include <memory>
#include <initializer_list>
namespace mc::windows::legion {
std::string readWmiEvidence(); // All selected capability rows + current values; getters only.
// One apartment-bound firmware session, constructed inside the resource owner.
// Discovery never applies a mode or starts/installs vendor software.
class Session {
public:
    Session();~Session();
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    std::vector<lenovo::Row> query(const char* name);
    lenovo::Row call(const char* name,const char* method,const lenovo::Row& input={});
    int mode();void mode(int);
    int feature(std::uint32_t);void feature(std::uint32_t,int);
    lenovo::Catalog catalog();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
