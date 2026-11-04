#pragma once
#include "../../../protocols/uniwill/gm6ir0c/Registers.h"
// Exact templates from owner archive9.ControlCenter_5.52.49.6.zip:
// installer ControlCenter_5.52.50.0_MBB.exe / UserFanTables/DefaultFanTable_*.json.
// These are this OEM packet defaults, not a claim about immutable EC ROM tables.
// Generated from all16 logical points; use the shared RamFan1.5 serializer.
namespace mc::gm5 {
inline constexpr std::array<gm6::Curves,3> factoryCurves{{
    gm6::Curves{{
        gm6::Curve{{gm6::Point{0,48,0},gm6::Point{54,50,30},gm6::Point{58,62,30},gm6::Point{61,64,35},gm6::Point{63,65,45},gm6::Point{64,67,45},gm6::Point{66,69,50},gm6::Point{68,71,55},gm6::Point{70,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55}}},
        gm6::Curve{{gm6::Point{0,48,0},gm6::Point{53,55,30},gm6::Point{54,59,30},gm6::Point{58,61,35},gm6::Point{60,63,45},gm6::Point{62,65,45},gm6::Point{64,67,50},gm6::Point{66,69,55},gm6::Point{68,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55},gm6::Point{255,255,55}}}
    }},
    gm6::Curves{{
        gm6::Curve{{gm6::Point{0,48,0},gm6::Point{54,50,30},gm6::Point{59,64,30},gm6::Point{63,68,35},gm6::Point{67,68,45},gm6::Point{67,71,45},gm6::Point{70,74,50},gm6::Point{73,77,55},gm6::Point{76,82,70},gm6::Point{81,84,80},gm6::Point{83,255,90},gm6::Point{255,255,90},gm6::Point{255,255,90},gm6::Point{255,255,90},gm6::Point{255,255,90},gm6::Point{255,255,90}}},
        gm6::Curve{{gm6::Point{0,48,0},gm6::Point{54,48,0},gm6::Point{54,57,30},gm6::Point{58,61,35},gm6::Point{62,61,45},gm6::Point{62,65,45},gm6::Point{64,68,50},gm6::Point{67,71,55},gm6::Point{70,74,70},gm6::Point{73,77,80},gm6::Point{76,255,90},gm6::Point{255,255,90},gm6::Point{255,255,90},gm6::Point{255,255,90},gm6::Point{255,255,90},gm6::Point{255,255,90}}}
    }},
    gm6::Curves{{
        gm6::Curve{{gm6::Point{0,48,0},gm6::Point{54,50,30},gm6::Point{59,64,30},gm6::Point{63,68,35},gm6::Point{67,68,45},gm6::Point{67,71,45},gm6::Point{70,74,50},gm6::Point{73,77,55},gm6::Point{76,82,70},gm6::Point{81,84,80},gm6::Point{83,255,100},gm6::Point{255,255,100},gm6::Point{255,255,100},gm6::Point{255,255,100},gm6::Point{255,255,100},gm6::Point{255,255,100}}},
        gm6::Curve{{gm6::Point{0,48,0},gm6::Point{54,48,0},gm6::Point{54,57,30},gm6::Point{58,61,35},gm6::Point{62,61,45},gm6::Point{62,65,45},gm6::Point{64,68,50},gm6::Point{67,71,55},gm6::Point{70,74,70},gm6::Point{73,77,80},gm6::Point{76,255,100},gm6::Point{255,255,100},gm6::Point{255,255,100},gm6::Point{255,255,100},gm6::Point{255,255,100},gm6::Point{255,255,100}}}
    }}
}};
}
