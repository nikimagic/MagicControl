# Single dependency list for the build output, portable staging and releases.
# RyzenSMU is required by ApuPowerReader even when no APU button was clicked.
set(MAGICCONTROL_PAWNIO_REQUIRED_FILES
    PawnIOLib.dll
    AMDFamily17.bin
    SmbusPIIX4.bin
    SmbusI801.bin
    SmbusI801-NOTICE.txt
    RyzenSMU.bin
    IntelMSR.bin
    IntelMSR-NOTICE.txt
    IntelMCHBAR.bin
    IntelMCHBAR.p
    IntelMCHBAR-NOTICE.txt
    IntelPCHThermal.bin
    IntelPCHThermal-NOTICE.txt
    IntelPCHThermal.p
    LpcIO.bin
    LpcIO-NOTICE.txt
    COPYING
    RyzenSMU-NOTICE.txt)

function(magiccontrol_validate_pawnio directory)
    foreach(_file IN LISTS MAGICCONTROL_PAWNIO_REQUIRED_FILES)
        if(NOT EXISTS "${directory}/${_file}")
            message(FATAL_ERROR "PawnIO runtime is missing required file: ${_file}")
        endif()
    endforeach()
    # Keep the signed upstream module and its pinned provenance in sync.
    file(READ "${directory}/RyzenSMU-NOTICE.txt" _notice)
    string(REGEX MATCH "SHA256: ([0-9A-Fa-f]+)" _match "${_notice}")
    string(TOLOWER "${CMAKE_MATCH_1}" _expected)
    string(LENGTH "${_expected}" _length)
    file(SHA256 "${directory}/RyzenSMU.bin" _actual)
    if(NOT _length EQUAL 64 OR NOT _actual STREQUAL _expected)
        message(FATAL_ERROR "RyzenSMU.bin does not match its provenance SHA256")
    endif()
    file(SHA256 "${directory}/SmbusI801.bin" _i801_actual)
    if(NOT _i801_actual STREQUAL "a0f7d066e7efda28c0e754c1f52dbd8dc280d388ba8575b514a80cb67490530c")
        message(FATAL_ERROR "SmbusI801.bin differs from official signed0.2.11 module")
    endif()
    file(SHA256 "${directory}/IntelMSR.bin" _intel_actual)
    if(NOT _intel_actual STREQUAL "d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f")
        message(FATAL_ERROR "IntelMSR.bin differs from the official signed0.2.11 module")
    endif()
    file(SHA256 "${directory}/IntelMCHBAR.bin" _mch_actual)
    if(NOT _mch_actual STREQUAL "3f82b832d99b4aac37d2a20fdb7c9baa2a3bc0488612c9019c9484eb0e8a6eae")
        message(FATAL_ERROR "IntelMCHBAR.bin differs from official signed 0.2.11 module")
    endif()
    file(SHA256 "${directory}/IntelPCHThermal.bin" _pch_actual)
    if(NOT _pch_actual STREQUAL "478500ae78aebef3ae7a0b5fe4848d2d5737e8f7c5d51200c284a61c21d7865d")
        message(FATAL_ERROR "IntelPCHThermal.bin differs from the official signed0.2.11 module")
    endif()
    file(SHA256 "${directory}/LpcIO.bin" _lpc_actual)
    if(NOT _lpc_actual STREQUAL "b3896a1cab0d808fca31fe2ebcae045d59dac690da87b17c858bb8da357eb45e")
        message(FATAL_ERROR "LpcIO.bin differs from the verified HLYL runtime")
    endif()
endfunction()
