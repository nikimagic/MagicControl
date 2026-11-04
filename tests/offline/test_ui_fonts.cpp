// Isolated GDI/DIB audit with one invisible message-only menu owner.
// No application, displayed UI, drivers or EC.
#include "src/gui/windows/Layout.h"
#include "src/gui/windows/ControlTheme.h"
#include "src/gui/common/OsdIcon.h"
#include <fstream>
#include <iostream>
#include <set>
#include <vector>

int wmain(int argc,wchar_t** argv){
    namespace ui=mc::gui::ui;namespace osd=mc::gui::osd;
    if(argc!=3)return 2;
    if(!mc::gui::registerBundledUiFonts(std::filesystem::path(argv[1])/L"src/gui/assets/fonts"))return 3;
    std::filesystem::create_directories(argv[2]);
    HDC dc=CreateCompatibleDC(nullptr);int failures=0,fontChecks=0,glyphChecks=0,iconChecks=0;
    for(unsigned language=0;language<4;++language){ui::setLanguage(language);
        std::set<wchar_t> chars;
        for(const auto& e:ui::translations()){
            const auto text=language==0?e.russian:language==1?e.english:language==2?e.simplified:e.traditional;
            const int n=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
            std::wstring wide(n,0);MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),wide.data(),n);
            for(wchar_t c:wide)if(c>32)chars.insert(c);
        }
        const std::wstring glyphs(chars.begin(),chars.end());
        for(int dpi:{96,144,192,240})for(int role=0;role<static_cast<int>(ui::FontRole::Count);++role){
            auto font=mc::gui::createUiFont(static_cast<ui::FontRole>(role),dpi);auto old=SelectObject(dc,font);
            wchar_t face[LF_FACESIZE]{};GetTextFaceW(dc,LF_FACESIZE,face);TEXTMETRICW tm{};
            if(!GetTextMetricsW(dc,&tm)||tm.tmHeight<=0||std::wstring(face)!=ui::selectedFontFamilyWindows(static_cast<ui::Language>(language)))++failures;
            ++fontChecks;
            if(role==static_cast<int>(ui::FontRole::ClassicBody)){
                std::vector<WORD> indices(glyphs.size());
                if(GetGlyphIndicesW(dc,glyphs.data(),static_cast<int>(glyphs.size()),indices.data(),GGI_MARK_NONEXISTING_GLYPHS)==GDI_ERROR)++failures;
                else for(std::size_t i=0;i<indices.size();++i){++glyphChecks;if(indices[i]==0xffff){++failures;std::cerr<<"missing language="<<language<<" glyph U+"<<std::hex<<static_cast<unsigned>(glyphs[i])<<std::dec<<'\n';}}
            }
            SelectObject(dc,old);DeleteObject(font);
        }
    }
    ui::setLanguage(0);
    for(int dpi:{96,144,192,240}){
        const auto s=[&](int n){return MulDiv(n,dpi,96);};const int cell=s(56),w=cell*9,h=cell*2;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* bits{};auto bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto oldBitmap=SelectObject(dc,bmp);
        RECT all{0,0,w,h};SetDCBrushColor(dc,RGB(22,21,41));FillRect(dc,&all,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        auto pen=CreatePen(PS_SOLID,std::max(1,s(1)),RGB(160,144,255));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        for(int icon=0;icon<=static_cast<int>(osd::Icon::Notifications);++icon){
            const int side=s(35),x=(icon%9)*cell+(cell-side)/2,y=(icon/9)*cell+(cell-side)/2;
            const auto px=[&](int n){if(n<0||n>32)++failures;return x+MulDiv(n,side,32);};
            const auto py=[&](int n){if(n<0||n>32)++failures;return y+MulDiv(n,side,32);};
            osd::drawIcon(static_cast<osd::Icon>(icon),[&](int a,int b,int c,int d){MoveToEx(dc,px(a),py(b),nullptr);LineTo(dc,px(c),py(d));},
                [&](int a,int b,int c,int d){Rectangle(dc,px(a),py(b),px(c),py(d));},[&](int a,int b,int c,int d){Ellipse(dc,px(a),py(b),px(c),py(d));});++iconChecks;
        }
        SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);GdiFlush();
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+w*h*4;
        std::ofstream out(std::filesystem::path(argv[2])/("icons-"+std::to_string(dpi)+".bmp"),std::ios::binary);
        out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(static_cast<const char*>(bits),w*h*4);
        SelectObject(dc,oldBitmap);DeleteObject(bmp);
    }
    // Invisible message-only owner: exercise native menu measure and type
    // restoration without opening a menu or running application code.
    HWND owner=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Батарея");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    if(!owner)++failures;
    else {mc::gui::control::PopupTypography typography(owner,menu);
        MENUITEMINFOW separator{sizeof(separator)};separator.fMask=MIIM_FTYPE;GetMenuItemInfoW(menu,1,TRUE,&separator);
        if(!(separator.fType&MFT_SEPARATOR))++failures;
        MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_DATA;GetMenuItemInfoW(menu,0,TRUE,&item);
        MEASUREITEMSTRUCT measure{};measure.CtlType=ODT_MENU;measure.itemData=item.dwItemData;
        SendMessageW(owner,WM_MEASUREITEM,0,reinterpret_cast<LPARAM>(&measure));if(!measure.itemWidth||!measure.itemHeight)++failures;
    }
    MENUITEMINFOW restored{sizeof(restored)};restored.fMask=MIIM_FTYPE|MIIM_DATA;GetMenuItemInfoW(menu,0,TRUE,&restored);
    if((restored.fType&MFT_OWNERDRAW)||restored.dwItemData)++failures;
    DestroyMenu(menu);if(owner)DestroyWindow(owner);
    DeleteDC(dc);std::cout<<"fonts="<<fontChecks<<" translated_glyphs="<<glyphChecks<<" icons="<<iconChecks<<" menu_checks=3 failures="<<failures<<'\n';
    return failures?1:0;
}
