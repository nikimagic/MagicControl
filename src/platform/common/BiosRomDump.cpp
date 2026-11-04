#include "MagicControl/BiosRomDump.h"
namespace mc {
DiagnosticDumpResult createBiosRomDump(std::stop_token token,std::atomic<std::uint64_t>* progress){
    return createPlatformBiosRomDump(token,progress);
}
}
