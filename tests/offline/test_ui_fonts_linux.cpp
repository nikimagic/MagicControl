// Fontconfig/Pango only: no GTK windows, application, backend or hardware.
#include "src/gui/common/Fonts.h"
#include "src/gui/common/Localization.h"
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <pango/pangofc-font.h>
#include <filesystem>
#include <iostream>
int main(int argc,char** argv){
    if(argc!=2)return 2;namespace ui=mc::gui::ui;
    const auto directory=std::filesystem::canonical(std::filesystem::path(argv[1])/"src/gui/assets/fonts");
    auto* config=FcConfigCreate();
    for(const auto* name:{"NotoSansSC.ttf","NotoSansTC.ttf"})if(!FcConfigAppFontAddFile(config,reinterpret_cast<const FcChar8*>((directory/name).c_str())))return 3;
    FcConfigBuildFonts(config);auto* map=pango_cairo_font_map_new();pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(map),config);
    int failures=0,runs=0;
    for(unsigned language=0;language<4;++language)for(int dpi:{96,144,192,240}){
        auto* context=pango_font_map_create_context(map);pango_context_set_language(context,pango_language_from_string(ui::languageTag(static_cast<ui::Language>(language))));
        auto* description=pango_font_description_new();pango_font_description_set_family(description,ui::selectedFontFamily(static_cast<ui::Language>(language)));
        pango_font_description_set_absolute_size(description,ui::fontHeight(ui::FontRole::ClassicBody,dpi)*PANGO_SCALE);
        auto* layout=pango_layout_new(context);pango_layout_set_font_description(layout,description);
        std::string text;for(const auto& e:ui::translations()){text+=language==0?e.russian:language==1?e.english:language==2?e.simplified:e.traditional;text+=' ';}
        pango_layout_set_text(layout,text.c_str(),static_cast<int>(text.size()));
        if(pango_layout_get_unknown_glyphs_count(layout))++failures;
        auto* iter=pango_layout_get_iter(layout);do{auto* run=pango_layout_iter_get_run_readonly(iter);if(!run)continue;
            FcChar8* path{};auto* pattern=pango_fc_font_get_pattern(PANGO_FC_FONT(run->item->analysis.font));
            if(FcPatternGetString(pattern,FC_FILE,0,&path)!=FcResultMatch||
                std::filesystem::path(reinterpret_cast<const char*>(path))!=directory/(language==3?"NotoSansTC.ttf":"NotoSansSC.ttf"))++failures;
            ++runs;
        }while(pango_layout_iter_next_run(iter));
        pango_layout_iter_free(iter);g_object_unref(layout);pango_font_description_free(description);g_object_unref(context);
    }
    g_object_unref(map);FcConfigDestroy(config);std::cout<<"language_dpi_cases=16 shaped_runs="<<runs<<" failures="<<failures<<'\n';return failures?1:0;
}
