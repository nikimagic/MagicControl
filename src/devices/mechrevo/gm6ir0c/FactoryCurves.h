#pragma once
#include "../../../protocols/uniwill/gm6ir0c/Registers.h"
// Exact GM6IR0C OEM DefaultFanTable_{Office,Gaming,Turbo}.json from user archive.
namespace mc::gm6 {
inline constexpr std::array<Curves,3> factoryCurves{{
    Curves{{
        Curve{{Point{0,48,0},Point{54,50,25},Point{58,61,25},Point{60,63,30},Point{62,65,35},Point{64,67,40},Point{66,69,45},Point{68,71,48},Point{70,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50}}},
        Curve{{Point{0,48,0},Point{49,52,0},Point{51,54,25},Point{53,56,30},Point{55,58,35},Point{57,60,40},Point{59,62,45},Point{61,64,48},Point{63,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50},Point{255,255,50}}},
    }},
    Curves{{
        Curve{{Point{0,48,0},Point{54,50,25},Point{59,62,30},Point{63,65,35},Point{66,68,45},Point{69,71,55},Point{72,74,60},Point{75,77,70},Point{78,80,80},Point{81,255,90},Point{255,255,90},Point{255,255,90},Point{255,255,90},Point{255,255,90},Point{255,255,90},Point{255,255,90}}},
        Curve{{Point{0,48,0},Point{56,48,0},Point{56,57,30},Point{58,60,35},Point{61,63,45},Point{64,66,55},Point{67,69,60},Point{70,72,70},Point{73,74,80},Point{75,255,90},Point{255,255,90},Point{255,255,90},Point{255,255,90},Point{255,255,90},Point{255,255,90},Point{255,255,90}}},
    }},
    Curves{{
        Curve{{Point{0,48,0},Point{54,50,25},Point{59,62,30},Point{63,65,35},Point{66,68,45},Point{69,71,55},Point{72,74,60},Point{75,77,70},Point{78,80,80},Point{81,83,90},Point{84,255,95},Point{255,255,95},Point{255,255,95},Point{255,255,95},Point{255,255,95},Point{255,255,95}}},
        Curve{{Point{0,48,0},Point{56,48,0},Point{56,57,30},Point{58,60,35},Point{61,63,45},Point{64,66,55},Point{67,69,60},Point{70,72,70},Point{73,74,80},Point{75,77,90},Point{78,255,95},Point{255,255,95},Point{255,255,95},Point{255,255,95},Point{255,255,95},Point{255,255,95}}},
    }},
}};
}
