#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <span>
#include <string>
#include <stdexcept>
namespace mc::windows::detail {
// Flat SHA256 of bytes, NOT the Authenticode/catalog hash of a PE image.
inline std::string binarySha256(std::span<const char> data){
    BCRYPT_ALG_HANDLE algorithm{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 provider unavailable");
    std::array<unsigned char,32> digest{};
    const auto status=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),static_cast<ULONG>(data.size()),digest.data(),static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)throw std::runtime_error("SHA256 calculation failed");
    constexpr char hex[]="0123456789abcdef";std::string out;
    for(const auto b:digest){out+=hex[b>>4];out+=hex[b&15];}return out;
}
}
