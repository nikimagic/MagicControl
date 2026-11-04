#pragma once
#include "MagicControl/BoardIdentity.h"

namespace mc {
inline constexpr wchar_t kBiosSupportUrl[] = L"https://www.mechrevo.com/cn/products/wu-jie-15-xpro-bao-feng-xue/downloads";
inline constexpr wchar_t kNahimicUrl[] = L"https://www.nahimic.com/updates/";
struct BiosSupportLinks {
    const char* supportCaption{"Поддержка производителя"};
    const wchar_t* supportUrl{};
    const char* catalogueNote{};
};
// Resolve from an explicit board descriptor, never from a copied inventory cache.
// UI may supply a presentation board; firmware queries still require real SMBIOS.
// Nahimic is independent of the laptop brand and remains a separate link.
inline BiosSupportLinks biosSupportLinks(const BoardDescriptor& board) {
    if(board.manufacturer=="LENOVO"&&board.baseBoard=="LNVNB161216"){
        if(board.productName=="82WK")return {"Поддержка Lenovo · 82WK",L"https://pcsupport.lenovo.com/us/en/products/laptops-and-netbooks/legion-series/legion-pro-5-16irx8/82wk/downloads",
            "Официальные драйверы и BIOS для кода модели 82WK, сообщённого ноутбуком."};
        if(board.productName=="82WQ")return {"Поддержка Lenovo · 82WQ",L"https://pcsupport.lenovo.com/us/en/products/laptops-and-netbooks/legion-series/legion-pro-7-16irx8h/82wq/downloads",
            "Официальные драйверы и BIOS для кода модели 82WQ, сообщённого ноутбуком."};
    }
    if(board.manufacturer=="LENOVO")return {"Поддержка Lenovo",L"https://pcsupport.lenovo.com/"};
    if(board.manufacturer=="Micro Computer (HK) Tech Limited"&&board.baseBoard=="HPBSD"&&board.productName=="EliteMini Series")
        return {"Поддержка MINISFORUM",L"https://www.minisforum.com/pages/product-info",
            "EliteMini HPBSD · официальный раздел UM880 Pro / UM890 Pro. BIOS и драйверы; отдельный Control Center для этой платы в каталоге не указан."};
    if(board.manufacturer=="MACHENIKE"&&board.baseBoard=="L16"&&board.productName=="L16")
        return {"Поддержка MACHENIKE L16 S",L"https://www.machenike.com/driver/download/?id=342",
            "L16 S · ID5/ID6 DR/FR · AMD 7745HX/7945HX + RTX 50: официальный каталог MACHENIKE. Дамп EC доступен; управление ожидает проверки возможностей платы."};
    if(board.manufacturer=="Eluktronics Inc."&&board.baseBoard=="HYDROC-16"&&board.productName=="HYDROC-16 G2")
        return {"Поддержка HYDROC-16 G2",L"https://www.eluktronics.com/hydroc-16-g2-drivers/",
            "HYDROC-16 G2 · диагностика EC через установленный родной UWACPIDriver. Управление FANs/подсветкой/MUX пока не включено."};
    if(board.manufacturer=="HUAWEI"&&board.productName=="HLYL-WXX9"&&board.baseBoard=="HLYL-WXX9-PCB")
        return {"Поддержка HONOR",L"https://www.honor.com/ru/support/"};
    if(board.manufacturer==kReferenceManufacturer&&board.baseBoard==kReferenceBaseBoard)
        return {"Поддержка MECHREVO",kBiosSupportUrl};
    if(board.manufacturer=="MECHREVO" && board.baseBoard=="GM6IR0C" &&
       board.productName=="JiguangX Series GM6IR0C")
        // Checked 2026-10-20: the generic product page now favours 2026.
        // Pin the official catalogue's legacy IDC group (AR/PR,18 drivers).
        // AQ/IQ currently lists the same18 file URLs. The saved exact-board
        // OEM SupportInfo reports BIOS_PROJECT_ID=IDC; this is NOT a flash URL
        // or proof that another board's firmware can be installed on GM6IR0C.
        return {"Поддержка GM6IR0C",L"https://www.mechrevo.com/cn/downloads?category=VGMSYq&serial=rR6Snr&display_product=rlESpy&author=277",
            "GM6IR0C · IDC · 极光 X → AR/PR: официальный каталог драйверов2024. Это ссылка на драйверы; автоматическая проверка BIOS пока недоступна."};
    if(board.manufacturer=="MECHREVO" && board.baseBoard=="GM5HG0A" &&
       board.productName=="Yilong15Pro Series GM5HG0A")
        // Checked 2026-10-22: product169/VKlSN7 was renamed to2026, but
        // author90 still selects the eight original2024 YL15P R7 drivers.
        // Keep the legacy hardware filter; this is not a firmware flash URL.
        return {"Поддержка GM5HG0A",L"https://www.mechrevo.com/cn/downloads?category=VGMSYq&serial=rwvS4q&display_product=VKlSN7&author=90",
            "GM5HG0A · 翼龙15 Pro → R7 4060/4070: официальный каталог драйверов YL15P 2024. Автоматическая проверка BIOS пока недоступна."};
    if(board.manufacturer=="MECHREVO")
        return {"Поддержка MECHREVO",L"https://www.mechrevo.com/"};
    return {}; // No model-specific firmware link is guessed for another vendor.
}
}
