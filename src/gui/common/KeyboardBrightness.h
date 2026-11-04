#pragma once
#include <string>
namespace mc::gui::ui {
inline constexpr char keyboardBrightnessDefaultCaption[]="Default";
inline std::string keyboardBrightnessButtonCaption(int level){return level==0?"Выкл.":std::to_string(level);}
}
