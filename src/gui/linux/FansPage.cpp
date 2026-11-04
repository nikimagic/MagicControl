#include "Pages.h"
#include "../common/FooterStatus.h"
#include "../common/DefaultFanGraph.h"
#include "MagicControl/Logging.h"
#include "Ui.h"
#include "NumericField.h"
#include "OsdService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <optional>

#include "MagicControl/PortableSettings.h"
#include "MagicControl/FanControlLimits.h"
#include "MagicControl/CustomPresets.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/ControlAudit.h"
#include "../../platform/linux/FansServicesLinux.h"
#include "../common/MonitoringCards.h"
#include "../common/DeviceHeader.h"

namespace mc::gtkui {
namespace {

struct FansState;
int requiredFanHeight(int width,int dpi,const mc::MonitoringSnapshot& sample,const std::string& displayDevice,const ResourceState& resources);

// GDK objects are main-thread only. Copy the connector before starting a worker;
// an absent connector is unknown, never a guessed renderer/boot GPU identity.
std::string gpuDisplayDevice(GtkWidget* widget) {
    auto* native = widget ? gtk_widget_get_native(widget) : nullptr;
    auto* surface = native ? gtk_native_get_surface(native) : nullptr;
    auto* display = widget ? gtk_widget_get_display(widget) : nullptr;
    auto* monitor = surface && display ? gdk_display_get_monitor_at_surface(display, surface) : nullptr;
    const char* connector = monitor ? gdk_monitor_get_connector(monitor) : nullptr;
    return connector ? connector : "";
}
struct MonitoringLife { FansState* owner; };
struct FansState {
    mc::linux_platform::FansOperationsLinux model;
    decltype(mc::linux_platform::FansOperationsLinux::Presentation::resourceBoard) resourceBoard{model.presentation().resourceBoard};
    decltype(mc::linux_platform::FansOperationsLinux::Presentation::preview) preview{model.presentation().preview};
    decltype(mc::linux_platform::FansOperationsLinux::Presentation::profiles) profiles{model.presentation().profiles};
    decltype(mc::linux_platform::FansOperationsLinux::Presentation::monitoring) monitoring{model.presentation().monitoring};
    decltype(mc::linux_platform::FansOperationsLinux::Presentation::resourceState) resourceState{model.presentation().resourceState};

    explicit FansState(std::function<MonitoringSnapshot()> source):model(ui::presentationBoard(),ui::laptopPreview(),std::move(source)){}
    OsdService* osd{};
    Canvas* canvas{};
    std::array<int, 2> pages{};
    std::array<std::array<GtkWidget*, 8>, 2> temperatures{}, duties{};
    std::array<GtkWidget*, 2> previous{}, next{};
    std::array<std::array<GtkWidget*, mc::kFanCurvePresetOrder.size()>, 2> presets{};
    GtkWidget* linkCurves{};
    std::string status{"Локальные кривые загружены. Linux EC-транспорт не подключён."};
    void setStatus(std::string value){if(value==status)return;status=std::move(value);ui::footerStatuses().publish("fans",status);}
    bool updating{};
    std::string displayDevice;
    guint monitoringTimer{};
    int lastFanHeight{};
    std::shared_ptr<MonitoringLife> monitoringLife{std::make_shared<MonitoringLife>(MonitoringLife{this})};
    ~FansState() {
        monitoringLife->owner = nullptr;
        if (monitoringTimer) g_source_remove(monitoringTimer);
        model.shutdown();
    }
};

void pollMonitoring(FansState& state) {
    if(state.preview)return;
    const auto displayDevice = gpuDisplayDevice(state.canvas->widget());
    if (displayDevice != state.displayDevice) {
        state.displayDevice = displayDevice;
        gtk_widget_queue_resize(state.canvas->widget());
        state.canvas->redraw();
    }
    const auto life=state.monitoringLife;
    state.model.poll([life]{
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,+[](gpointer data)->gboolean{
            const auto& life=*static_cast<std::shared_ptr<MonitoringLife>*>(data);
            if(auto* state=life->owner)if(const auto changed=state->model.take();changed&&*changed){
                const int width=static_cast<int>(std::lround(gtk_widget_get_width(state->canvas->widget())*state->canvas->scale()));
                if(requiredFanHeight(width,state->canvas->dpi(),state->monitoring,state->displayDevice,state->resourceState)!=state->lastFanHeight)gtk_widget_queue_resize(state->canvas->widget());
                state->canvas->redraw();
            }
            return G_SOURCE_REMOVE;
        },new std::shared_ptr<MonitoringLife>(life),+[](gpointer data){delete static_cast<std::shared_ptr<MonitoringLife>*>(data);});
    });
}

void save(FansState& state) {
    if(state.preview)return;
    state.setStatus(state.model.saveDraft());
    state.canvas->redraw();
}
void update(FansState& state, int channel) {
    state.updating = true;
    state.model.normalizeEditorDraft(channel);
    for (int i = 0; i < 8; ++i) {
        const bool active=state.pages[channel]*8+i<state.profiles.activePointCount(channel);
        gtk_widget_set_visible(state.temperatures[channel][i],active);gtk_widget_set_visible(state.duties[channel][i],active);
        const auto& point = state.profiles.current(channel)[state.pages[channel] * 8 + i];
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(state.temperatures[channel][i]),mc::fanInput::temperatureMinimum,mc::fanInput::temperatureMaximum);
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(state.duties[channel][i]),mc::fanInput::percentMinimum,mc::fanInput::percentMaximum);
        if(mc::fanInput::validTemperature(point.temperature))gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.temperatures[channel][i]),point.temperature);
        else gtk_editable_set_text(GTK_EDITABLE(state.temperatures[channel][i]),point.temperature==255?"":std::to_string(point.temperature).c_str());
        if(mc::fanInput::validPercent(point.duty))gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.duties[channel][i]),point.duty);
        else gtk_editable_set_text(GTK_EDITABLE(state.duties[channel][i]),std::to_string(point.duty).c_str());
        const bool editable = state.profiles.ecManual || state.profiles.currentPreset(channel) == mc::FanCurvePreset::Manual;
        gtk_widget_set_sensitive(state.temperatures[channel][i], editable);
        gtk_widget_set_sensitive(state.duties[channel][i], editable);
    }
    // Saved intent is editable data, never evidence of applied Linux FANs.
    for(std::size_t i=0;i<state.presets[channel].size();++i){auto* button=state.presets[channel][i];
        gtk_widget_set_sensitive(button,state.preview);
        if(state.preview&&!state.profiles.ecManual&&state.profiles.currentPreset(channel)==mc::kFanCurvePresetOrder[i])gtk_widget_add_css_class(button,"primary");
        else gtk_widget_remove_css_class(button,"primary");
    }
    gtk_widget_set_sensitive(state.linkCurves,state.preview);
    gtk_button_set_label(GTK_BUTTON(state.linkCurves),state.profiles.linked?"CPU=GPU":"CPU≠GPU");
    gtk_widget_remove_css_class(state.linkCurves,"primary");
    state.updating = false;
    state.canvas->redraw();
}
struct SpinBinding { FansState* state; int channel, column; bool temperature; };
void valueChanged(GtkSpinButton* spin, gpointer data) {
    const auto& binding = *static_cast<SpinBinding*>(data);
    auto& state = *binding.state;
    if (state.updating) return;
    if (!state.profiles.ecManual && state.profiles.currentPreset(binding.channel) != mc::FanCurvePreset::Manual) return;
    auto curve = state.profiles.current(binding.channel);
    auto& point = curve[state.pages[binding.channel] * 8 + binding.column];
    if (binding.temperature) point.temperature = mc::fanInput::temperature(gtk_spin_button_get_value_as_int(spin));
    else point.duty = mc::fanInput::percent(gtk_spin_button_get_value_as_int(spin));
    state.model.editCurveDraft(binding.channel,curve);
    update(state,binding.channel);
    if(state.profiles.ecManual||state.profiles.linked)update(state,1-binding.channel);
    save(state);
}

ui::ProcessorCard processorText(const mc::MonitoringSnapshot& sample,int channel,const std::string& displayDevice,const ResourcePresentationView* resources) {
    const auto& source=sample.processors[channel].fanSource;
    return ui::processorCard(sample,ui::monitoringFanReading(sample,channel,resources),channel,source.empty()?"Linux · hwmon":source,displayDevice,resources);
}
ui::MetricLineLayout summaryLayout(const ui::ProcessorCard& card,Rect bounds,int dpi) {
    return ui::metricLineLayout(card.summary,bounds,ui::textMetrics(Font::MonitorCaption,dpi).height,
        [&](const std::string& value) { return textWidth(value,Font::MonitorCaption,dpi); });
}
std::array<int,3> informationHeights(const ui::ProcessorCard& card,int cardWidth,int dpi) {
    const auto g=ui::fanReadoutGeometry({0,0,cardWidth,ui::Scale{dpi}(mc::gui::kFanCardHeight)},{dpi});
    std::array<int,3> heights{};
    heights[0]=summaryLayout(card,{g.information.left+ui::Scale{dpi}(8),0,g.information.right-ui::Scale{dpi}(8),0},dpi).height;
    for(int i=1;i<3;++i) heights[i]=textHeight(card.information[i],Font::MonitorCaption,
        g.information.width()-ui::Scale{dpi}(16),dpi);
    return heights;
}
int requiredFanHeight(int width,int dpi,const mc::MonitoringSnapshot& sample,const std::string& displayDevice,const ResourceState& state) {
    const ResourcePresentationView resources(state);
    const ui::Scale s{dpi};const auto base=ui::fansGeometry(width,s);
    int height=s(mc::gui::kFanCardHeight);
    for(int channel=0;channel<2;++channel) {
        const auto card=processorText(sample,channel,displayDevice,&resources);
        const Font secondaryFont=channel==0?Font::FanSecondaryCompact:Font::FanSecondary;
        const auto g=ui::fanReadoutGeometry({0,0,base.fans[channel].width(),height},s);
        const int contentWidth=g.secondary.width()-s(16);
        const int primaryWidth=textWidth(card.secondaryTitle,Font::MonitorCaption,dpi)+s(8)+
            textWidth(card.secondaryValue,secondaryFont,dpi)+s(6)+textWidth(card.secondaryUnit,Font::MonitorCaption,dpi);
        const bool wrap=primaryWidth>contentWidth;
        height=std::max(height,ui::processorCardHeight(informationHeights(card,base.fans[channel].width(),dpi),
            textHeight(card.secondarySource,Font::MonitorCaption,contentWidth,dpi),wrap,s,
            ui::textMetrics(Font::MonitorCaption,dpi),ui::textMetrics(secondaryFont,dpi),
            !card.secondaryExtraTitle.empty()));
    }
    return height;
}
void fanReadout(cairo_t* cr,Rect bounds,int channel,int dpi,const mc::MonitoringSnapshot& sample,const std::string& displayDevice,const ResourcePresentationView& resources) {
    const auto card=processorText(sample,channel,displayDevice,&resources);
    const Font secondaryFont=channel==0?Font::FanSecondaryCompact:Font::FanSecondary;
    std::array<int,3> valueWidths{}, unitWidths{};
    for(int i=0;i<3;++i) {
        valueWidths[i]=textWidth(card.values[i],Font::FanMetric,dpi);
        if(i>0)unitWidths[i]=textWidth(card.valueUnits[i],Font::ClassicBody,dpi);
    }
    const auto g=ui::fanReadoutGeometry(bounds,{dpi},informationHeights(card,bounds.width(),dpi),
        textWidth(card.secondaryTitle,Font::MonitorCaption,dpi),textWidth(card.secondaryValue,secondaryFont,dpi),
        textWidth(card.secondaryUnit,Font::MonitorCaption,dpi),valueWidths,unitWidths,
        ui::textMetrics(Font::MonitorCaption,dpi),ui::textMetrics(secondaryFont,dpi),{}, {},
        textWidth(card.secondaryExtraTitle,Font::MonitorCaption,dpi),
        textWidth(card.secondaryExtraValue,secondaryFont,dpi),
        textWidth(card.secondaryExtraUnit,Font::MonitorCaption,dpi));
    fill(cr,bounds,theme::kMonitorSurface);
    text(cr,card.title,g.heading,Font::FanTitle,theme::kMonitorText,dpi);
    const char* units[]{"об/мин","PWM","Температура"};
    for(int i=0;i<3;++i) {
        text(cr,card.values[i],g.values[i],Font::FanMetric,theme::kFanValueText,dpi);
        text(cr,units[i],g.units[i],Font::MonitorCaption,theme::kMonitorText,dpi,Horizontal::Center);
        text(cr,card.sources[i],g.sources[i],Font::MonitorCaption,theme::kMonitorSource,dpi,Horizontal::Center,Vertical::Top);
        if(i>0)text(cr,card.valueUnits[i],g.valueUnits[i],Font::ClassicBody,theme::kFanValueText,dpi);
    }
    fill(cr,g.information,theme::kFanNestedSurface);fill(cr,g.secondary,theme::kFanNestedSurface);
    for(const auto& part : summaryLayout(card,g.informationLines[0],dpi).parts)
        text(cr,part.text,part.bounds,Font::MonitorCaption,part.value?theme::kFanValueText:theme::kMonitorText,
            dpi,Horizontal::Left,Vertical::Top);
    for(int i=1;i<3;++i)text(cr,card.information[i],g.informationLines[i],Font::MonitorCaption,
        theme::kMonitorSource,dpi,Horizontal::Left,Vertical::Top,true);
    text(cr,card.secondaryTitle,g.secondaryTitle,Font::MonitorCaption,theme::kMonitorText,dpi,Horizontal::Center);
    text(cr,card.secondaryValue,g.secondaryValue,secondaryFont,theme::kTextPrimary,dpi);
    text(cr,card.secondaryUnit,g.secondaryUnit,Font::MonitorCaption,theme::kTextPrimary,dpi);
    text(cr,card.secondaryExtraTitle,g.secondaryExtraTitle,Font::MonitorCaption,theme::kMonitorText,dpi,Horizontal::Center);
    text(cr,card.secondaryExtraValue,g.secondaryExtraValue,secondaryFont,theme::kTextPrimary,dpi);
    text(cr,card.secondaryExtraUnit,g.secondaryExtraUnit,Font::MonitorCaption,theme::kTextPrimary,dpi);
    text(cr,card.secondarySource,g.secondarySource,Font::MonitorCaption,theme::kMonitorSource,dpi,Horizontal::Center,Vertical::Top,true);
}
void monitor(cairo_t* cr, Rect bounds, int index, int dpi, const mc::MonitoringSnapshot& snapshot,const ResourcePresentationView& resources) {
    if(bounds.width()<=0||bounds.height()<=0)return;
    if (index == 0) {
        const auto card = ui::batteryCard(snapshot.battery,&resources);
        std::array<int, 3> labelWidths{}, valueWidths{}, rightWidths{};
        for (int row = 0; row < 3; ++row) {
            labelWidths[row] = textWidth(card.left[row].label, Font::ClassicBody, dpi);
            valueWidths[row] = textWidth(card.left[row].value, Font::ClassicBody, dpi);
            rightWidths[row] = textWidth(card.right[row], Font::ClassicBody, dpi);
        }
        const auto g = ui::batteryCardGeometry(bounds, {dpi}, labelWidths, valueWidths, rightWidths);
        fill(cr, bounds, theme::kMonitorSurface);
        for (int row = 0; row < 3; ++row) {
            const auto color = row == 1 && card.flow == mc::BatteryFlow::Charging ? theme::kBatteryCharging :
                row == 1 && card.flow == mc::BatteryFlow::Discharging ? theme::kBatteryDischarging : theme::kMonitorText;
            text(cr, card.left[row].label, g.labels[row], Font::ClassicBody, color, dpi);
            text(cr, card.left[row].value, g.values[row], Font::ClassicBody, theme::kTextPrimary, dpi);
            text(cr, card.right[row], g.rightValues[row], Font::ClassicBody, theme::kTextPrimary, dpi);
        }
        return;
    }
    const auto card = ui::monitoringCard(snapshot, index,&resources);
    const auto g = ui::monitorGeometry(bounds, {dpi}, textWidth(card.title, Font::ClassicBody, dpi),
        textWidth(card.value, Font::ClassicBody, dpi), textWidth(card.unit, Font::ClassicBody, dpi));
    fill(cr, bounds, theme::kMonitorSurface);
    text(cr, card.title, g.title, Font::ClassicBody, theme::kMonitorText, dpi);
    text(cr, card.value, g.value, Font::ClassicBody, theme::kTextPrimary, dpi);
    text(cr, card.unit, g.unit, Font::ClassicBody, theme::kTextPrimary, dpi);
    const auto detailColor = card.flow == mc::BatteryFlow::Charging ? theme::kBatteryCharging :
        card.flow == mc::BatteryFlow::Discharging ? theme::kBatteryDischarging : theme::kMonitorText;
    text(cr, card.detail, g.detail, Font::MonitorCaption, detailColor, dpi);
    text(cr, card.source, g.source, Font::MonitorCaption, theme::kMonitorSource, dpi);
}
int plotX(Rect plot, double temperature) {
    return plot.left + static_cast<int>(std::lround((std::clamp(temperature, 20.0, 100.0) - 20) * plot.width() / 80));
}
int plotY(Rect plot, double duty) { return plot.bottom - static_cast<int>(std::clamp(duty, 0., 100.) * plot.height() / 100.); }
void graph(cairo_t* cr, Rect bounds, int channel, std::span<const mc::FanCurveCell> curve, mc::FanCurvePreset preset,
           int dpi, const mc::MonitoringSnapshot& sample,const ResourcePresentationView& resources,bool nativeSelected,bool preview) {
    const ui::Scale s{dpi};
    const auto native=ui::defaultFanGraph(resources,channel,nativeSelected);
    std::string titleText=channel==0?"CPU вентилятор":"GPU вентилятор";
    if(native.curve)titleText=ui::translate(titleText)+" · Default "+native.curve->profile;
    if(native.curve&&native.curve->rpm)titleText=std::string(channel?"Вентилятор 2":"Вентилятор 1")+" · Default "+native.curve->profile;
    const auto g = ui::graphGeometry(bounds, s,textWidth(native.axis,Font::Caption,dpi));
    text(cr, titleText, g.title, Font::Caption, theme::kTextMuted, dpi,Horizontal::Center,Vertical::Top);
    text(cr, native.axis, g.axis, Font::Caption, theme::kTextMuted, dpi,Horizontal::Left,Vertical::Top);
    text(cr, "°C", g.temperatureUnit, Font::Caption, theme::kTextMuted, dpi, Horizontal::Right);
    const auto plot = g.plot;
    const auto graphY=[&](double value){return plotY(plot,value*100/native.maximum);};
    for (int duty = 0; duty <= 100; duty += 25) {
        const int y = graphY(duty*native.maximum/100);
        line(cr, plot.left, y, plot.right, y, theme::kDivider);
        text(cr, native.axisLabel(duty), {bounds.left + s(2), y - s(7), plot.left - s(4), y + s(7)},
             Font::Caption, theme::kTextMuted, dpi, Horizontal::Right);
    }
    for (int temperature = 20; temperature <= 100; temperature += 20) {
        const int x = plotX(plot, temperature);
        line(cr, x, plot.top, x, plot.bottom, theme::kDivider);
        text(cr, std::to_string(temperature), {x - s(12), plot.bottom + s(2), x + s(12), plot.bottom + s(15)},
             Font::Caption, theme::kTextMuted, dpi, Horizontal::Center);
    }
    const Color color = channel == 0 ? theme::kAccent : theme::kAccentSecondary;
    double previousTemperature = 20, previousDuty = 0;
    bool started = false;
    std::vector<NativeFanGraphPoint> shown;
    if(nativeSelected){if(native.curve)shown=native.curve->points;}
    else for(const auto& point:curve)if(point.temperature!=255)shown.push_back({static_cast<double>(point.temperature),static_cast<double>(point.duty)});
    for (const auto& point : shown) {
        if (!started) { previousDuty = point.duty; started = true; }
        const int x = plotX(plot, point.temperature);
        line(cr, plotX(plot, previousTemperature), graphY(previousDuty), x,
             graphY(previousDuty), color, s(2));
        line(cr, x, graphY(previousDuty), x, graphY(point.duty), color, s(2));
        previousTemperature = point.temperature; previousDuty = point.duty;
    }
    if (started) line(cr, plotX(plot, previousTemperature), graphY(previousDuty),
                       plot.right, graphY(previousDuty), color, s(2));
    else text(cr, preview?"Пример · просмотр":preset == mc::FanCurvePreset::Default ? "Default · управление прошивкой EC" : "Кривая EC не задана",
        plot, Font::Caption, theme::kTextMuted, dpi, Horizontal::Center);
    const auto reading=ui::monitoringFanReading(sample,channel,&resources);
    if(nativeSelected){
        if(native.curve&&native.temperature&&native.requested){
            cairo_set_source_rgb(cr,(color&255)/255.,((color>>8)&255)/255.,((color>>16)&255)/255.);
            cairo_set_line_width(cr,std::max(1,s(1)));
            cairo_arc(cr,plotX(plot,*native.temperature),graphY(*native.requested),s(6),0,6.283185307);cairo_stroke(cr);
        }
        if(native.curve&&native.temperature&&native.actual){
            cairo_set_source_rgb(cr,1,1,1);cairo_arc(cr,plotX(plot,*native.temperature),graphY(*native.actual),s(4),0,6.283185307);cairo_fill(cr);
        }
        text(cr,native.curve?native.legend:ui::translate("Default · управляющая температура EC недоступна"),g.legend,Font::Caption,theme::kMonitorText,dpi);
        return;
    }
    if(preview){text(cr,"Пример · просмотр",g.legend,Font::Caption,theme::kMonitorText,dpi);return;}
    if(reading.temperatureAvailable&&reading.pwmAvailable) {
        cairo_set_source_rgb(cr,1,1,1);cairo_arc(cr,plotX(plot,reading.temperatureCelsius),graphY(static_cast<int>(std::lround(reading.dutyPercent))),s(4),0,6.283185307);cairo_fill(cr);
    }
    text(cr,"Не применена · Факт: "+ui::monitoringMetric(sample.processors[channel].celsius,1,true)+" °C · "+
        ui::monitoringMetric(sample.processors[channel].fanPwm)+"%",g.legend,Font::Caption,theme::kMonitorText,dpi);
}
ui::EditorGeometry modeGeometry(Rect r,int dpi,bool withLink) {
    const ui::Scale s{dpi};auto g=ui::editorGeometry(r,s,withLink,std::max(textWidth("Temp °C",Font::Caption,dpi),textWidth("PWM %",Font::Caption,dpi)));
    int x=g.presets[0].left;const int top=g.presets[0].top,bottom=g.presets[0].bottom;
    const int widths[]{54,52,40,52};
    for(int i=0;i<4;++i){g.presets[i]={x,top,x+s(widths[i]),bottom};x=g.presets[i].right+s(i==2?16:4);}
    if(withLink){x=g.presets[3].right+s(16);g.linkCurves={x,top,x+s(76),bottom};x=g.linkCurves.right+s(8);}
    g.count.left=x;return g;
}
void editor(cairo_t* cr, Rect bounds, const FansState& state, int channel, int dpi) {
    const auto g = modeGeometry(bounds,dpi,channel==1);
    text(cr, "Temp °C", g.temperatureLabel, Font::Caption, theme::kTextSecondary, dpi);
    text(cr, "PWM %", g.dutyLabel, Font::Caption, theme::kTextSecondary, dpi);
    for (int i = 0; i < 8&&state.pages[channel]*8+i<state.profiles.activePointCount(channel); ++i)
        text(cr, std::to_string(state.pages[channel] * 8 + i + 1), g.numbers[i],
             Font::Caption, theme::kTextMuted, dpi, Horizontal::Center);
    const auto native=ui::defaultFanGraph(ResourcePresentationView(state.resourceState),channel,
        !state.preview&&!state.profiles.ecManual&&state.profiles.currentPreset(channel)==FanCurvePreset::Default);
    text(cr, state.preview?"Пример · просмотр":native.curve?native.source:state.profiles.ecManual?"DownT −3 °C · общая кривая Manual":"DownT −3 °C · локальная кривая", g.source, Font::Caption, theme::kTextSecondary, dpi);
    text(cr, std::to_string(state.pages[channel]*8+1)+"–"+std::to_string(std::min((state.pages[channel]+1)*8,state.profiles.activePointCount(channel)))+" / "+std::to_string(state.profiles.activePointCount(channel)), g.page,
         Font::Caption, theme::kTextSecondary, dpi, Horizontal::Center);
    const auto& curve = state.profiles.current(channel);
    const int count = static_cast<int>(std::count_if(curve.begin(), curve.begin()+state.profiles.activePointCount(channel),
        [](const auto& point) { return point.temperature != 255; }));
    text(cr, !state.preview&&!state.profiles.ecManual && state.profiles.currentPreset(channel) == mc::FanCurvePreset::Default ? (channel == 1 ? "EC" : "Штатный EC") :
        std::string(channel == 1 ? "" : "Задано: ") + std::to_string(count) + " / "+std::to_string(state.profiles.activePointCount(channel)), g.count,
         Font::Caption, theme::kTextMuted, dpi, Horizontal::Right);
}
}  // namespace

GtkWidget* createFansPage(OsdService* osd,std::function<mc::MonitoringSnapshot()> source) {
    auto* state = new FansState(std::move(source));
    if(state->preview&&ui::discretePreviewModel())state->model.seedPreviewModels(ui::discretePreviewVendors(),ui::nvidiaPreviewProcessorModels());

    state->osd=osd;
    state->canvas = Canvas::create(
        [state](Canvas& canvas, int width, int) {
            state->lastFanHeight=requiredFanHeight(width,canvas.dpi(),state->monitoring,state->displayDevice,state->resourceState);
            const auto g = ui::fansGeometry(width, {canvas.dpi()},state->lastFanHeight,ui::monitorBatteryVisible(deviceProfile(ui::presentationBoard()),state->monitoring));
            for (int channel = 0; channel < 2; ++channel) {
                const auto e = modeGeometry(g.editors[channel],canvas.dpi(),channel==1);
                if (channel == 1) canvas.place(state->linkCurves, e.linkCurves);
                for (int column = 0; column < 8; ++column) {
                    const int arrows = std::max(1, static_cast<int>(
                        e.temperatureArrows[column].width() / canvas.scale() + 0.5));
                    setNumericArrowWidth(state->temperatures[channel][column], arrows);
                    setNumericArrowWidth(state->duties[channel][column], arrows);
                    canvas.place(state->temperatures[channel][column], e.temperatures[column]);
                    canvas.place(state->duties[channel][column], e.duties[column]);
                    const bool active=state->pages[channel]*8+column<state->profiles.activePointCount(channel);
                    gtk_widget_set_visible(state->temperatures[channel][column],active);gtk_widget_set_visible(state->duties[channel][column],active);
                }
                canvas.place(state->previous[channel], e.previous);
                canvas.place(state->next[channel], e.next);
                for (std::size_t preset = 0; preset < mc::kFanCurvePresetOrder.size(); ++preset)
                    canvas.place(state->presets[channel][preset], e.presets[preset]);
            }
            return g.extent;
        },
        [state](Canvas& canvas, cairo_t* cr, int width, int height) {
            fill(cr, {0,0,width,height}, theme::kBackground);
            const int dpi = canvas.dpi();
            const ResourcePresentationView resources(state->resourceState);
            const auto g = ui::fansGeometry(width, {dpi},requiredFanHeight(width,dpi,state->monitoring,state->displayDevice,state->resourceState),ui::monitorBatteryVisible(deviceProfile(ui::presentationBoard()),state->monitoring));
            for (int channel = 0; channel < 2; ++channel) {
                fanReadout(cr, g.fans[channel], channel, dpi,state->monitoring,state->displayDevice,resources);
                fill(cr, g.curves[channel], theme::kSurface);
                graph(cr, g.graphs[channel], channel, std::span(state->profiles.current(channel)).first(state->profiles.activePointCount(channel)),
                      state->profiles.ecManual ? mc::FanCurvePreset::Manual : state->profiles.currentPreset(channel), dpi,state->monitoring,resources,
                      !state->preview&&!state->profiles.ecManual&&state->profiles.currentPreset(channel)==FanCurvePreset::Default,state->preview);
                editor(cr, g.editors[channel], *state, channel, dpi);
            }
            for (int i = 0; i < 5; ++i) monitor(cr, g.monitors[i], i, dpi, state->monitoring,resources);

        });
    g_object_set_data_full(G_OBJECT(state->canvas->widget()), "magiccontrol-fans", state,
        [](gpointer data) { delete static_cast<FansState*>(data); });
    for (int channel = 0; channel < 2; ++channel) {
        for (int column = 0; column < 8; ++column) {
            GtkWidget* temperature = createNumericField(mc::fanInput::temperatureMinimum,mc::fanInput::temperatureMaximum,1);
            GtkWidget* duty = createNumericField(mc::fanInput::percentMinimum,mc::fanInput::percentMaximum,1);
            state->temperatures[channel][column] = temperature;
            state->duties[channel][column] = duty;
            for (auto [widget, isTemperature] : std::array<std::pair<GtkWidget*,bool>,2>{{{temperature,true},{duty,false}}}) {
                auto* binding = new SpinBinding{state, channel, column, isTemperature};
                g_object_set_data_full(G_OBJECT(widget), "magiccontrol-spin-binding", binding,
                    [](gpointer data) { delete static_cast<SpinBinding*>(data); });
                g_signal_connect(widget, "value-changed", G_CALLBACK(valueChanged), binding);
                state->canvas->add(widget);
            }
        }
        const auto turn = [state, channel](int direction) {
            state->pages[channel] = std::clamp(state->pages[channel] + direction, 0, (state->profiles.activePointCount(channel)-1)/8);
            update(*state, channel);
        };
        state->previous[channel] = actionButton("‹", [turn] { turn(-1); });
        state->next[channel] = actionButton("›", [turn] { turn(1); });
        state->canvas->add(state->previous[channel]); state->canvas->add(state->next[channel]);
        for (std::size_t preset=0;preset<4;++preset) {
            const auto manual=mc::customManualCaption(deviceProfile(ui::presentationBoard()).id,false);
            const char* names[]{"Default",manual.c_str(),"Max","Manual\nonline"};
            const auto previewName=std::string(mc::kFanCurvePresetNames[static_cast<std::size_t>(mc::kFanCurvePresetOrder[preset])]);
            state->presets[channel][preset]=actionButton(state->preview?previewName.c_str():names[preset],[state,channel,preset]{
                // Also refuse programmatic activation of an unavailable setter.
                state->setStatus(state->model.requestPreset(channel,mc::kFanCurvePresetOrder[preset]));
                if(state->preview){update(*state,0);update(*state,1);}
                state->canvas->redraw();
            });
            gtk_widget_add_css_class(state->presets[channel][preset], "curve-preset");
            state->canvas->add(state->presets[channel][preset]);
        }
    }
    state->linkCurves = actionButton("CPU≠GPU", [state] {
        state->setStatus(state->model.requestLink());
        if(state->preview){update(*state,0);update(*state,1);}
        state->canvas->redraw();
    });
    gtk_widget_add_css_class(state->linkCurves, "curve-preset");
    state->canvas->add(state->linkCurves);
    state->updating=true;const auto error=state->model.loadDraft();state->updating=false;
    if(!error.empty())state->setStatus(error);
    update(*state, 0); update(*state, 1);
    if(state->preview)state->setStatus("Просмотр профиля · аппаратное управление отключено");
    g_signal_connect(state->canvas->widget(), "map", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto& state = *static_cast<FansState*>(data);
        pollMonitoring(state);
        if (!state.monitoringTimer) state.monitoringTimer = g_timeout_add_seconds(1, +[](gpointer data)->gboolean {
            pollMonitoring(*static_cast<FansState*>(data)); return G_SOURCE_CONTINUE;
        }, &state);
    }), state);
    g_signal_connect(state->canvas->widget(), "unmap", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto& state = *static_cast<FansState*>(data);
        if (!mc::batteryLoggingEnabled() && state.monitoringTimer) { g_source_remove(state.monitoringTimer); state.monitoringTimer = 0; }
    }), state);
    return scrolledPage(state->canvas);
}

}  // namespace mc::gtkui
