#pragma once
#include "OsdModel.h"
#include <initializer_list>

namespace mc::gui::osd {
// Shared 32x32 vector strokes. No private-use glyphs or OS icon fonts.
template<class Line,class Box,class Oval>
void drawIcon(Icon icon,Line line,Box box,Oval oval){
    const auto speaker=[&]{box(3,13,8,20);line(8,13,15,7);line(15,7,15,26);line(15,26,8,20);};
    switch(icon){
    case Icon::Keyboard:
        box(2,7,30,25);for(int y:{12,17})for(int x:{7,13,19,25})line(x-1,y,x+1,y);line(9,21,23,21);break;
    case Icon::Sound:case Icon::Muted:
        speaker();if(icon==Icon::Muted){line(21,12,29,22);line(29,12,21,22);}
        else{line(20,11,24,16);line(24,16,20,22);line(25,7,30,16);line(30,16,25,26);}break;
    case Icon::Brightness:
        oval(10,10,22,22);for(int x:{5,27})line(x,16,x+(x<16?3:-3),16);
        line(16,2,16,6);line(16,26,16,30);line(5,5,8,8);line(24,24,27,27);line(5,27,8,24);line(24,8,27,5);break;
    case Icon::Microphone:
        box(12,3,20,20);line(7,14,7,20);line(7,20,12,25);line(12,25,20,25);line(20,25,25,20);line(25,20,25,14);line(16,25,16,30);line(11,30,21,30);break;
    case Icon::Touchpad:box(3,5,29,27);line(3,21,29,21);line(16,21,16,27);break;
    case Icon::Lock:box(6,14,26,29);box(10,3,22,14);line(16,20,16,24);break;
    case Icon::Battery:box(2,9,27,24);box(27,13,30,20);for(int x:{7,13,19})line(x,13,x,20);break;
    case Icon::Power:line(16,2,16,17);line(9,7,4,13);line(4,13,4,23);line(4,23,11,29);line(11,29,21,29);line(21,29,28,23);line(28,23,28,13);line(28,13,23,7);break;
    case Icon::Camera:box(2,9,30,27);box(9,5,22,9);oval(10,12,22,24);break;
    case Icon::Wifi:line(2,10,9,6);line(9,6,23,6);line(23,6,30,10);line(7,16,12,12);line(12,12,20,12);line(20,12,25,16);line(11,22,16,18);line(16,18,21,22);oval(15,26,17,28);break;
    case Icon::Display:box(2,4,30,24);line(16,24,16,29);line(9,29,23,29);break;
    case Icon::Bluetooth:line(14,2,14,30);line(14,2,25,11);line(25,11,5,26);line(5,6,25,22);line(25,22,14,30);break;
    case Icon::Windows:box(3,3,14,14);box(18,3,29,14);box(3,18,14,29);box(18,18,29,29);break;
    case Icon::Usb:line(16,29,16,3);line(12,7,16,3);line(16,3,20,7);line(16,22,7,16);line(7,16,7,9);oval(5,5,9,9);line(16,18,25,12);box(23,7,27,11);oval(14,27,18,31);break;
    case Icon::Notifications:box(7,8,25,25);line(4,25,28,25);line(16,3,16,8);oval(13,27,19,31);break;
    case Icon::Fan:
        oval(3,3,29,29);oval(13,13,19,19);
        line(14,13,9,7);line(9,7,17,5);line(17,5,19,13);
        line(19,14,25,9);line(25,9,27,17);line(27,17,19,19);
        line(18,19,23,25);line(23,25,15,27);line(15,27,13,19);
        line(13,18,7,23);line(7,23,5,15);line(5,15,13,13);break;
    default:oval(3,3,29,29);line(16,14,16,24);oval(15,8,17,10);break;
    }
}
}
