# Profiles carry board evidence; no OS calls belong in the registry.
target_sources(magiccontrol_core PRIVATE
    DeviceRegistry.cpp
    lenovo/legion-kwcn/Profile.cpp
    unknown/Profile.cpp
    eluktronics/hydroc-16-g2/Profile.cpp
    huawei/hlyl-wxx9/Profile.cpp
    mechrevo/x5sp4nag/Profile.cpp
    mechrevo/gm6ir0c/Profile.cpp
    mechrevo/ReportedProfiles.cpp
    mechrevo/gm6px7x/Profile.cpp
    mechrevo/kuangshi-jiguang-series-x6xx55xw-b2/Profile.cpp
    mechrevo/canglong-series-x6xr55xy/Profile.cpp
    mechrevo/gm6bg0x/Profile.cpp
    mechrevo/kuangshi-jiguang-series-x6xx57tw/Profile.cpp
    mechrevo/jiaolong-series-x6dr55xx-b2/Profile.cpp
    mechrevo/gx6hg0u-f/Profile.cpp
    mechrevo/canglong-series-x8xr55xp-b2/Profile.cpp
    mechrevo/yaoshi-series-x6ar55xy/Profile.cpp
    mechrevo/gm6ix9b/Profile.cpp
    mechrevo/gm5hg0a/Profile.cpp
    machenike/l16/Profile.cpp
    minisforum/hpbsd/Profile.cpp
)
