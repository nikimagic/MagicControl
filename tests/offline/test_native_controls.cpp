#include "OfflineTest.h"
#include "src/gui/windows/DiscreteSlider.h"
#include "src/gui/windows/NumericFieldTheme.h"
#include <vector>
#include <string>
using namespace mc::gui;
HWND slider{};unsigned editChanges{};
LRESULT CALLBACK parentProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_CTLCOLOREDIT||message==WM_CTLCOLORSTATIC){if(auto brush=numericFieldColors(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)))return reinterpret_cast<LRESULT>(brush);SetBkColor(reinterpret_cast<HDC>(wp),theme::kSurface);SetDCBrushColor(reinterpret_cast<HDC>(wp),theme::kSurface);return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));}
    if(message==WM_COMMAND&&HIWORD(wp)==EN_CHANGE)++editChanges;
    return DefWindowProcW(window,message,wp,lp);
}
struct Image {
    HDC dc{};HBITMAP bitmap{};HGDIOBJ old{};std::uint32_t* pixels{};int width{},height{};
    Image(int w,int h):width(w),height(h){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;dc=CreateCompatibleDC(nullptr);bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);CHECK(bitmap&&dc);old=SelectObject(dc,bitmap);PatBlt(dc,0,0,w,h,WHITENESS);}
    ~Image(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}
    unsigned count(COLORREF color){const auto rgb=(GetRValue(color)<<16)|(GetGValue(color)<<8)|GetBValue(color);unsigned n{};GdiFlush();for(int i=0;i<width*height;++i)if((pixels[i]&0xFFFFFF)==rgb)++n;return n;}
};
int main(){
    WNDCLASSW wc{};wc.lpfnWndProc=parentProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"MagicControl.OfflineControls";CHECK(RegisterClassW(&wc)!=0);
    HWND parent=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN,-32000,-32000,600,300,nullptr,nullptr,wc.hInstance,nullptr);CHECK(parent);
    slider=createDiscreteSlider(parent,10,4);SetWindowPos(slider,nullptr,0,0,240,26,SWP_NOZORDER|SWP_NOACTIVATE);
    auto field=createNumericFieldWindows(parent,11,12,0,255,32);placeNumericFieldWindows(field,{0,50,64,72},14);
    HFONT font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");SendMessageW(field.edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    for(bool enabled:{true,false})for(int position=0;position<=4;++position){EnableWindow(slider,enabled);SendMessageW(slider,TBM_SETPOS,TRUE,position);Image image(240,26);SendMessageW(slider,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT|PRF_ERASEBKGND);const auto accents=image.count(enabled?theme::kAccent:theme::kTextMuted);const auto white=image.count(RGB(255,255,255));std::printf("Slider enabled=%d pos=%d accent=%u white=%u\n",enabled,position,accents,white);CHECK(accents>0);CHECK(white==0);SendMessageW(slider,WM_ERASEBKGND,reinterpret_cast<WPARAM>(image.dc),0);CHECK(image.count(enabled?theme::kAccent:theme::kTextMuted)==accents);}
    ValidateRect(field.edit,nullptr);ValidateRect(field.spin,nullptr);RECT beforeRect{},afterRect{};
    const auto dirtyBefore=GetUpdateRect(field.edit,&beforeRect,FALSE);const auto before=editChanges;
    for(int i=0;i<100;++i){SendMessageW(field.spin,UDM_SETPOS32,0,32);SendMessageW(field.spin,UDM_SETRANGE32,0,255);}
    CHECK(editChanges==before);CHECK(GetUpdateRect(field.edit,&afterRect,FALSE)==dirtyBefore);CHECK(EqualRect(&beforeRect,&afterRect));CHECK(!GetUpdateRect(field.spin,nullptr,FALSE));
    RECT client{};GetClientRect(field.edit,&client);Image digits(client.right,client.bottom);
    SendMessageW(field.edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(digits.dc),PRF_CLIENT|PRF_ERASEBKGND);
    const auto glyphs=digits.count(theme::kNumericText);std::printf("Numeric glyphs=%u white=%u redundant EN_CHANGE=%u\n",glyphs,digits.count(RGB(255,255,255)),editChanges-before);
    CHECK(glyphs>0&&digits.count(RGB(255,255,255))==0);
    SendMessageW(field.edit,WM_ERASEBKGND,reinterpret_cast<WPARAM>(digits.dc),0);CHECK(digits.count(theme::kNumericText)==glyphs);
    SetWindowTextW(field.edit,L"42");CHECK(editChanges>before);
    const auto afterChange=editChanges;SetWindowTextW(field.edit,L"42");CHECK(editChanges==afterChange);
    SetWindowTextW(field.edit,L"999");SendMessageW(field.spin,UDM_SETPOS32,0,999);wchar_t clamped[16]{};GetWindowTextW(field.edit,clamped,16);CHECK(std::wstring_view(clamped)==L"255");
    std::vector<HFONT> fonts;
    for(int dpi:{96,144,192}){
        const auto scaled=[&](int value){return MulDiv(value,dpi,96);};
        const auto next=CreateFontW(-scaled(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");fonts.push_back(next);
        SendMessageW(field.edit,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);placeNumericFieldWindows(field,{0,50,scaled(64),50+scaled(22)},scaled(14));
        RECT rect{};GetClientRect(field.edit,&rect);Image image(rect.right,rect.bottom);SendMessageW(field.edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT|PRF_ERASEBKGND);
        const auto textPixels=image.count(theme::kNumericText);CHECK(textPixels>0&&image.count(RGB(255,255,255))==0);
        SendMessageW(field.edit,WM_ERASEBKGND,reinterpret_cast<WPARAM>(image.dc),0);CHECK(image.count(theme::kNumericText)==textPixels);
    }
    DestroyWindow(parent);DeleteObject(font);for(auto used:fonts)DeleteObject(used);std::printf("Native hidden controls: %u checks; application/backend=0\n",offlineChecks);
}
