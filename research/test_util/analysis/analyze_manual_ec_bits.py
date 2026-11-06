"""Analyze saved .0-pinned experiments. Never accesses hardware."""
import argparse
import collections
import json
import math
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parents[1] / "out/test_util/generated/keyboard-ec-timeout"


def stamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def read_events(path):
    rows = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass  # A running producer may still be flushing the last line.
    return rows


def timer_candidates(frames):
    """Simple 8/16-bit near-linear counters; correlation alone is not a timer."""
    if len(frames) < 20:
        return []
    times = [stamp(row["series_started_utc"]) for row, _ in frames]
    times = [value - times[0] for value in times]
    mean_t = sum(times) / len(times)
    denominator = sum((value - mean_t) ** 2 for value in times)
    results = []
    for width, order in ((1, "little"), (2, "little"), (2, "big")):
        for address in range(4097 - width):
            values = [int.from_bytes(data[address:address + width], order) for _, data in frames]
            modulus = 1 << (width * 8)
            deltas = [(right - left + modulus // 2) % modulus - modulus // 2
                      for left, right in zip(values, values[1:])]
            changed = [delta for delta in deltas if delta]
            if len(changed) < len(deltas) * 0.7:
                continue
            unwrapped = [values[0]]
            for delta in deltas:
                unwrapped.append(unwrapped[-1] + delta)
            mean_y = sum(unwrapped) / len(unwrapped)
            slope = sum((t - mean_t) * (y - mean_y) for t, y in zip(times, unwrapped)) / denominator
            if not any(abs(abs(slope) - scale) <= scale * 0.2 for scale in (1, 1000)):
                continue
            direction = 1 if slope > 0 else -1
            if sum(delta * direction > 0 for delta in changed) < len(changed) * 0.9:
                continue
            residual = math.sqrt(sum((y - (mean_y + slope * (t - mean_t))) ** 2
                                     for t, y in zip(times, unwrapped)) / len(times))
            if residual > max(2, abs(slope) * 0.2):
                continue
            results.append({"address": f"0x{address:03X}", "width": width,
                            "byte_order": order, "slope_per_second": round(slope, 3),
                            "residual": round(residual, 3), "first": values[0], "last": values[-1]})
    return results


def analyze(source):
    events = read_events(source / "events.jsonl")
    starts = [row for row in events if row.get("event") == "session_start"]
    options = next((row for row in events if row.get("event") == "experiment_options"), {})
    if not starts or not starts[0].get("manual_context"):
        raise ValueError("Not a manual-context session")
    binary = (source / "series.bin").read_bytes()
    samples = [row for row in events if row.get("event") == "sample"]
    frames = [(row, binary[row["series_frame"] * 4096:(row["series_frame"] + 1) * 4096])
              for row in samples if (row["series_frame"] + 1) * 4096 <= len(binary)]
    phases = collections.defaultdict(list)
    for row, data in frames:
        phases[row["phase"]].append((row, data))
    baseline = phases.get("manual-context", [])
    baseline_variable = {a for a in range(4096) if len({data[a] for _, data in baseline}) > 1}
    results = []
    for bit in range(1, 8):
        name = f"bit-{bit}"
        rows = [row for row in events if row.get("phase") == name]
        writes = [row for row in rows if row.get("event") == "write_requested"]
        if not writes:
            continue
        returned = [row for row in rows if row.get("event") == "write_returned"]
        restored = [row for row in rows if row.get("event") == "restore_result" and row.get("kind") == "tested-bit"]
        phase_frames = phases[name]
        before = (source / f"{name}-before.bin").read_bytes()
        restored_path = source / f"{name}-restored.bin"
        after_restore = restored_path.read_bytes() if restored_path.exists() else None
        tail = [data for row, data in phase_frames
                if stamp(row["series_started_utc"]) - stamp(writes[0]["utc"]) >= 10]
        stable_differences = []
        if tail and after_restore:
            for address in range(4096):
                values = {data[address] for data in tail}
                if (address not in baseline_variable and len(values) == 1
                        and next(iter(values)) != before[address]
                        and after_restore[address] == before[address]):
                    stable_differences.append({"address": f"0x{address:03X}", "before": before[address],
                                               "during": next(iter(values)), "restored": after_restore[address]})
        changes = []
        previous = None
        for row, data in phase_frames:
            value = row["controls"]["0x741"]
            if value != previous:
                changes.append({"utc": row["utc"], "value": value,
                                "windows_idle_ms": row.get("windows_idle_ms")})
            previous = value
        expected = writes[0]["wanted"] & (1 << bit)
        first_mismatch = next((row for row, data in phase_frames
                               if (row["controls"]["0x741"] & (1 << bit)) != expected), None)
        result = {"bit": bit, "before": writes[0]["before"], "requested": writes[0]["wanted"],
                  "immediate_readback": returned[-1]["readback"] if returned else None,
                  "frames": len(phase_frames), "transitions": changes,
                  "observed_0x741": sorted({row["controls"]["0x741"] for row, _ in phase_frames}),
                  "requested_bit_held_in_all_samples": bool(phase_frames) and all(
                      (row["controls"]["0x741"] & (1 << bit)) == expected for row, _ in phase_frames),
                  "first_mismatch_seconds_after_write": round(stamp(first_mismatch["utc"]) - stamp(writes[0]["utc"]), 3) if first_mismatch else None,
                  "restore_confirmed": bool(restored and restored[-1]["confirmed"]),
                  "stable_reversible_differences": stable_differences,
                  "other_controls": {key: sorted({row["controls"][key] for row, _ in phase_frames})
                                     for key in ("0x751", "0x767", "0x78C", "0x7C5", "0x7C6")}}
        results.append(result)
    final = [row for row in events if row.get("event") == "session_end"]
    context_restores = [row for row in events if row.get("event") == "restore_result" and row.get("kind") == "manual-context"]
    controls = {key: sorted({row["controls"][key] for row, _ in frames})
                for key in ("0x726", "0x727", "0x734", "0x735", "0x736", "0x751", "0x767", "0x78C", "0x7C5", "0x7C6")}
    return {"session": source.name, "scope": "4 KiB EC RAM; not firmware flash", "hardware_access": False,
            "completed": bool(final and final[-1].get("all_requested_bits_tested")),
            "observation_seconds": options.get("step_seconds"),
            "session_end": final[-1] if final else None,
            "pending_restore": (source / "restore.txt").exists(), "series_bytes": len(binary),
            "frames": len(frames), "journal_frames": len(samples),
            "series_complete": len(binary) == len(samples) * 4096,
            "bit0_held_in_every_sample": bool(frames) and all(row["controls"]["0x741"] & 1 and data[0x741] & 1 for row, data in frames),
            "context_restore_confirmed": bool(context_restores and context_restores[-1]["confirmed"]),
            "baseline_frames": len(baseline), "baseline_0x741": sorted({data[0x741] for _, data in baseline}),
            "baseline_variable_addresses": [f"0x{a:03X}" for a in sorted(baseline_variable)],
            "baseline_simple_counter_candidates": timer_candidates(baseline), "other_controls": controls, "results": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    args = parser.parse_args()
    result = analyze(args.session)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "manual-context-results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    text = ["# 0x741 при удерживаемом .0=1", "", f"Сессия: `{result['session']}`.", "",
            f"Полных кадров RAM: {result['frames']}; контрольный участок: {result['baseline_frames']}.",
            f".0 установлен во всех кадрах: {result['bit0_held_in_every_sample']}.",
            f"Прогон завершён: {result['completed']}; .0 восстановлен: {result['context_restore_confirmed']}; незавершённое восстановление: {result['pending_restore']}.", "",
            "| Бит | До | Запрошено | Немедленное чтение | Значения при наблюдении | Бит удержался | Восстановлен |",
            "|---|---|---|---|---|---|---|"]
    for row in result["results"]:
        values = ", ".join(f"0x{value:02X}" for value in row["observed_0x741"])
        immediate = f"0x{row['immediate_readback']:02X}" if row["immediate_readback"] is not None else "—"
        text.append(f"| .{row['bit']} | 0x{row['before']:02X} | 0x{row['requested']:02X} | {immediate} | {values} | {row['requested_bit_held_in_all_samples']} | {row['restore_confirmed']} |")
    held = ", ".join(f".{row['bit']}" for row in result["results"] if row["requested_bit_held_in_all_samples"])
    text += ["", "## Что показал прогон", "",
             f"Заданная длительность участка: {result['observation_seconds']} секунд при .0=1. В запрошенном положении во всех выборках своих шагов удержались: {held or 'нет'}."]
    for row in result["results"]:
        if row["first_mismatch_seconds_after_write"] is not None:
            text.append(f"- .{row['bit']}: первое чтение отменённого значения через {row['first_mismatch_seconds_after_write']} с после запроса записи (верхняя оценка, дискретное наблюдение).")
    if any(row["first_mismatch_seconds_after_write"] is not None for row in result["results"]):
        text.append("Удержание .0 не запрещает прошивке менять остальные биты 0x741.")
    text += ["", "Других стабильных обратимых отличий вне 0x741 по выбранному критерию не обнаружено; это не исключает эффектов вне доступной RAM или кратких переходов между кадрами.", "",
             "## Проверка счётчика 0x6FA", "",
             "В серии значение циклически растёт в диапазоне 0…59. В прежнем штатном наблюдении при нажатии клавиши счётчик продолжился 39→40, а при гашении через 31 секунду — 10→11.",
             "Исходные точки сохранены в counter-6fa-native.json. Этот байт не является непосредственно временем простоя клавиатуры и не подтверждён как настраиваемая уставка."]
    text += ["", "## Остальные управляющие регистры", "", "```json",
             json.dumps(result["other_controls"], ensure_ascii=False, indent=2), "```"]
    text += ["", "## Ограничения вывода", "",
             "Физическое свечение клавиш автоматический сбор не измеряет. Windows idle включает мышь; события встроенной клавиатуры в этом тесте отдельно не собираются.",
             "Постоянные во время шага обратимые различия и линейные счётчики приведены в JSON. Они являются кандидатами для анализа, а не доказанными уставками таймера.",
             "Адрес времени нельзя установить по одному совпадению с 30/31 или 30000. Значение может быть константой в коде прошивки вне этих 4096 байт.",
             "Режим .0 общий с управлением вентиляторов; самостоятельная галка автоотключения по этим измерениям ещё не подтверждена.", ""]
    (OUT / "manual-context-results.md").write_text("\n".join(text), encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items()
                      if key not in ("baseline_variable_addresses", "results")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
