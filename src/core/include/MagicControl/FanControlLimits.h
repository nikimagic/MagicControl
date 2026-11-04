#pragma once
#include <algorithm>
#include <cstddef>
namespace mc::fanInput {
// ПРАВИЛО ВЛАДЕЛЬЦА, 27.10.2026 — ДЛЯ ВСЕХ УСТРОЙСТВ WINDOWS И LINUX:
// поля температуры20–100°C ВКЛЮЧИТЕЛЬНО; PWM/проценты оборотов5–100% ВКЛЮЧИТЕЛЬНО.
// НЕ МЕНЯТЬ без нового ПРЯМОГО указания владельца. НЕ возвращать30–80°C,
// 3–85°C, минимумы7/25/36/38%,0% или255 как допустимый пользовательский ввод.
// Создание полей, обновление, стрелки/колесо и commit используют ТОЛЬКО эти
// константы. Аппаратная MAX-защита и raw-данные EC — отдельная логика;
// ими НЕЛЬЗЯ сужать диапазон пользовательских полей по модели устройства.
// Это ТОЛЬКО границы КОНТРОЛОВ. НЕ менять ради них значения/число точек,
// алгоритм кривых, пресеты или аппаратный способ управления вентиляторами.
inline constexpr int temperatureMinimum=20,temperatureMaximum=100;
inline constexpr int percentMinimum=5,percentMaximum=100;
static_assert(temperatureMinimum==20&&temperatureMaximum==100&&percentMinimum==5&&percentMaximum==100,
    "Owner contract: ALL devices, inclusive20..100C /5..100%; change only on a new explicit owner request");
constexpr int temperature(int value){return std::clamp(value,temperatureMinimum,temperatureMaximum);}
constexpr int percent(int value){return std::clamp(value,percentMinimum,percentMaximum);}
constexpr bool validTemperature(int value){return value>=temperatureMinimum&&value<=temperatureMaximum;}
constexpr bool validPercent(int value){return value>=percentMinimum&&value<=percentMaximum;}
template<class Table>void normalizeDraft(Table& table){
    const auto original=table;for(auto& p:table){p.temperature=255;p.duty=100;}
    std::size_t count{};
    for(auto p:original){if(p.temperature==255)continue;p.temperature=temperature(p.temperature);p.duty=percent(p.duty);
        if(count&&table[count-1].temperature==p.temperature)table[count-1].duty=std::max(table[count-1].duty,p.duty);
        else table[count++]=p;
    }
}
// Actual EC readouts and protocol-only255/0markers are not editable user values.
}
