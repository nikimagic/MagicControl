#pragma once
#include <cstdio>
#include <cstdlib>
#include <exception>
inline unsigned offlineChecks{};
inline void offlineCheck(bool value,const char* expression,int line){
    ++offlineChecks;if(!value){std::fprintf(stderr,"FAIL line %d: %s\n",line,expression);std::exit(1);}
}
#define CHECK(...) offlineCheck((__VA_ARGS__),#__VA_ARGS__,__LINE__)
template<class F> bool rejected(F action){try{action();}catch(const std::exception&){return true;}return false;}
