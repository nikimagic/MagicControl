#pragma once
#include <cstdint>
#include <initializer_list>
#include <memory>
namespace mc::windows {
// Pinned signed PawnIO LpcIO transport, not an EcAccess address window.
// Caller owns Global\Access_ISABUS.HTP.Method while entering/using PNPCFG.
class PawnLpcIo {
public:
    explicit PawnLpcIo(bool traceOperations=true);
    ~PawnLpcIo();
    PawnLpcIo(const PawnLpcIo&)=delete;
    PawnLpcIo& operator=(const PawnLpcIo&)=delete;
    void selectSlot0();
    void selectSlot1(); // Reviewed 0x4E/0x4F slot; never changes slot0 users.
    std::uint8_t readSuperIo(std::uint8_t index);
    void writeSuperIo(std::uint8_t index,std::uint8_t value);
    void writeConfigPort(std::uint16_t port,std::uint8_t value);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
