"""Enumerate declared DSDT OperationRegion, Field, BankField and IndexField items."""
from pathlib import Path
import csv
import json
import re
import collections

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "out/test_util/generated/firmware_temperatures"
SRC = ROOT / "out/test_util/analysis/firmware_temperatures/DSDT-0171f11732dc.dsl"
text = SRC.read_text(encoding="utf-8", errors="replace")
OUT.mkdir(parents=True, exist_ok=True)
lines = text.splitlines()


def scrub(line, in_string=False):
    result, quoted, escaped, i = [], in_string, False, 0
    while i < len(line):
        c = line[i]
        if quoted:
            if c == '"' and not escaped:
                quoted = False
            escaped = c == "\\" and not escaped
            result.append(" ")
            i += 1
            continue
        if c == '"':
            quoted, escaped = True, False
            result.append(" ")
            i += 1
            continue
        if c == "/" and i + 1 < len(line) and line[i + 1] == "/":
            break
        result.append(c)
        i += 1
    return "".join(result), quoted


def braces(s):
    return s.count("{") - s.count("}")


def const(s):
    s = s.strip()
    if s in ("Zero", "0"):
        return 0
    if s in ("One", "1"):
        return 1
    if re.fullmatch(r"0x[0-9A-Fa-f]+", s):
        return int(s, 16)
    if s.isdecimal():
        return int(s)
    return None


def resolve_expr(s, symbols):
    value = 0
    parts = re.split(r"\s*\+\s*", s.strip())
    for part in parts:
        if part in symbols:
            item = symbols[part]
        else:
            item = const(part)
        if item is None:
            return None
        value += item
    return value


def consume_parens(s, start):
    depth, i = 0, start
    while i < len(s):
        if s[i] == "(":
            depth += 1
        elif s[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unclosed field header/directive")


def block_at(index):
    # Collect the declaration through its opening brace and the matching close.
    raw, clean, quote = [], [], False
    i, level, opened = index, 0, False
    while i < len(lines):
        s, quote = scrub(lines[i], quote)
        raw.append(lines[i])
        clean.append(s)
        for c in s:
            if c == "{":
                level += 1
                opened = True
            elif c == "}":
                level -= 1
                if opened and level == 0:
                    return "\n".join(raw), "\n".join(clean), i + 1
        i += 1
    return "\n".join(raw), "\n".join(clean), i


def split_args(s):
    out, start, depth = [], 0, 0
    for i, c in enumerate(s):
        if c == "(": depth += 1
        elif c == ")": depth -= 1
        elif c == "," and depth == 0:
            out.append(s[start:i].strip()); start = i + 1
    out.append(s[start:].strip())
    return out


# Resolve simple constant Name() values used as OperationRegion bases/sizes.
symbols = {}
for line in lines:
    s, _ = scrub(line)
    match = re.search(r"\bName\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([^,)]+)\s*\)", s)
    if match:
        value = const(match.group(2))
        if value is not None:
            symbols[match.group(1)] = value

regions, declarations = [], []
for index, line in enumerate(lines):
    s, _ = scrub(line)
    match = re.search(r"\bOperationRegion\s*\(", s)
    if match:
        _, block, end = block_at(index)
        decl = re.search(r"OperationRegion\s*\((.*?)\)", block, re.S)
        if decl:
            args = split_args(decl.group(1))
            if len(args) >= 4:
                regions.append({"name": args[0], "space": args[1], "base_expression": args[2],
                                "base": resolve_expr(args[2], symbols), "length_expression": args[3],
                                "length": resolve_expr(args[3], symbols), "line": index + 1})
    for kind in ("Field", "BankField", "IndexField"):
        match = re.search(rf"\b{kind}\s*\(", s)
        if not match:
            continue
        _, block, _ = block_at(index)
        head = re.search(rf"{kind}\s*\((.*?)\)\s*\{{", block, re.S)
        if not head:
            continue
        args = split_args(head.group(1))
        owner = {"kind": kind, "line": index + 1, "name": args[0] if args else "",
                 "region_name": args[0] if args else "",
                 "field_access": ", ".join(args[-3:]) if len(args) >= 3 else ""}
        if kind == "BankField" and len(args) > 1:
            owner["bank_register"], owner["bank_value"] = args[1], args[2] if len(args) > 2 else ""
        if kind == "IndexField" and len(args) > 1:
            owner["index_register"] = args[0]
            owner["data_register"] = args[1]
        if kind == "Field":
            body = block[head.end():block.rfind("}")]
            cursor, p, reserved = 0, 0, 0
            token = re.compile(r"\s*(Offset\s*\([^)]*\)|AccessAs\s*\([^)]*\)|Connection\s*\([^)]*\)|0x[0-9A-Fa-f]+|[A-Za-z_][A-Za-z0-9_]*|,|\d+|.)", re.S)
            ts = [m.group(1).strip() for m in token.finditer(body)]
            while p < len(ts):
                t = ts[p]
                if not t:
                    p += 1; continue
                if re.fullmatch(r"Offset\s*\(.*\)", t, re.S):
                    value = re.search(r"\((.*)\)", t, re.S).group(1)
                    n = const(value)
                    cursor = n * 8 if n is not None else None
                    p += 1
                    if p < len(ts) and ts[p] == ",": p += 1
                    continue
                if re.fullmatch(r"(AccessAs|Connection)\s*\(.*\)", t, re.S):
                    p += 1
                    if p < len(ts) and ts[p] == ",": p += 1
                    continue
                if t == ",":
                    reserved += 1
                    name = f"Reserved_{reserved}"
                    p += 1
                else:
                    name = t
                    p += 1
                    if p >= len(ts) or ts[p] != ",":
                        continue
                    p += 1
                if p >= len(ts):
                    break
                size_expr = ts[p]
                bits = const(size_expr)
                p += 1
                if p < len(ts) and ts[p] == ",":
                    p += 1
                if cursor is None:
                    byte_offset, bit_offset = None, None
                else:
                    byte_offset, bit_offset = divmod(cursor, 8)
                row = {**owner, "field": name, "bit_offset": cursor,
                       "byte_offset": byte_offset, "bit_in_byte": bit_offset,
                       "bits": bits, "end_bit": cursor + bits if cursor is not None and bits is not None else None,
                       "reserved": name.startswith("Reserved_")}
                declarations.append(row)
                cursor = cursor + bits if cursor is not None and bits is not None else None
        else:
            declarations.append({**owner, "field": "", "bit_offset": None, "byte_offset": None,
                                 "bit_in_byte": None, "bits": None, "end_bit": None, "reserved": False})

# Reconstruct namespace scope by ASL indentation (scope/device close braces align
# with their opening declarations). This also handles declarations whose opening
# brace is on the next line.
scopes, methods = [], []
scope_pattern = re.compile(r"\b(Device|Scope|ThermalZone|Processor|PowerResource)\s*\(\s*([^)]*)\)")
method_pattern = re.compile(r"\bMethod\s*\(\s*([^,\s)]+)")
declarations_by_line = collections.defaultdict(list)
for declaration in declarations:
    declarations_by_line[declaration["line"]].append(declaration)
regions_by_line = collections.defaultdict(list)
for region in regions:
    regions_by_line[region["line"]].append(region)
for number, raw_line in enumerate(lines, 1):
    clean, _ = scrub(raw_line)
    indent = len(clean) - len(clean.lstrip())
    rest = clean.lstrip()
    while rest.startswith("}"):
        while scopes and scopes[-1][0] >= indent: scopes.pop()
        while methods and methods[-1][0] >= indent: methods.pop()
        rest = rest[1:].lstrip()
    rows = declarations_by_line.get(number, [])
    for row in rows:
        row["scope"] = ".".join(x[1] for x in scopes)
        row["inside_method"] = ".".join(x[1] for x in methods)
    for region in regions_by_line.get(number, []):
        region["scope"] = ".".join(x[1] for x in scopes)
        region["inside_method"] = ".".join(x[1] for x in methods)
    for m in scope_pattern.finditer(clean):
        scopes.append((indent, f"{m.group(1)}({m.group(2).strip()})"))
    matches = list(scope_pattern.finditer(clean))
    method = method_pattern.search(clean)
    if method:
        methods.append((indent, method.group(1)))

region_lookup = collections.defaultdict(list)
for region in regions:
    region_lookup[region["name"]].append(region)
for row in declarations:
    row.setdefault("scope", "")
    row.setdefault("inside_method", "")
    row.setdefault("dynamic_region", False)
    candidates = region_lookup.get(row["region_name"], [])
    # BankField/IndexField first argument names a region or a register, so only
    # ordinary Field has a direct OperationRegion base/address calculation.
    scoped = [r for r in candidates if r.get("scope") == row.get("scope") and
              r.get("inside_method", "") == row.get("inside_method", "")]
    region = scoped[0] if len(scoped) == 1 else candidates[0] if len(candidates) == 1 else None
    row["region"] = row["region_name"]
    row["region_ambiguous"] = len(candidates) > 1 and not scoped
    row["space"] = region["space"] if region else ""
    row["region_base_expression"] = region["base_expression"] if region else ""
    row["region_base"] = region["base"] if region else None
    absolute = region["base"] + row["byte_offset"] if region and region["base"] is not None and row["byte_offset"] is not None else None
    row["absolute_address"] = absolute
    row["address_hex"] = f"0x{absolute:X}" if absolute is not None else ""
    row["dynamic_region"] = bool(row.get("inside_method")) or bool(region and region["base"] is None)
    row["address_kind"] = ("indexed" if row["kind"] == "IndexField" else
                           "banked" if row["kind"] == "BankField" else
                           "ambiguous-region" if row["region_ambiguous"] else
                           "runtime/dynamic" if row.get("dynamic_region") else
                           "fixed" if absolute is not None else "relative/indirect")

fields_path, regions_path = OUT / "dsdt-fields.csv", OUT / "dsdt-regions.csv"
columns = ["scope", "inside_method", "kind", "field", "region", "region_ambiguous", "bank_register", "bank_value", "index_register", "data_register", "field_access", "region_base_expression", "space", "address_hex",
           "byte_offset", "bit_in_byte", "bits", "reserved", "dynamic_region", "address_kind", "line"]
with fields_path.open("w", encoding="utf-8-sig", newline="") as f:
    writer = csv.DictWriter(f, columns, extrasaction="ignore"); writer.writeheader(); writer.writerows(declarations)
with regions_path.open("w", encoding="utf-8-sig", newline="") as f:
    writer = csv.DictWriter(f, ["scope", "name", "space", "base_expression", "base", "length_expression", "length", "line", "inside_method", "dynamic_region"])
    writer.writeheader(); writer.writerows(regions)

by_space = collections.Counter(row["space"] or row["kind"] + " (indirect)" for row in declarations)
markdown = ["# DSDT: все объявленные поля", "",
            f"Источник: `{SRC.name}`; {len(declarations)} записей Field/BankField/IndexField, "
            f"из них {sum(bool(x['field']) and not x['reserved'] for x in declarations)} именованных полей, "
            f"{sum(x['reserved'] for x in declarations)} зарезервированных областей и "
            f"{sum(not x['field'] for x in declarations)} индексных деклараций без фиксированного адреса.", "",
            "Список описывает объявления ASL, а не гарантирует доступность каждого адреса "
            "в работающей ОС. Поля внутри методов, динамические OperationRegion, BankField "
            "и IndexField требуют соответствующей операции AML; абсолютный адрес для них "
            "не угадывается. Резервные промежутки сохранены в CSV для проверки смещений.", "",
            "## Сводка по address space", "", "| Address space | Field entries |", "|---|---:|"]
for space, count in by_space.most_common(): markdown.append(f"| {space} | {count} |")
markdown += ["", "## SystemMemory и EC MMIO", "",
             "| Объект | Регион / пространство | Смещение | Ширина | Адрес | Контекст |", "|---|---|---:|---:|---:|---|"]
for row in declarations:
    if row["reserved"] or row["space"] != "SystemMemory": continue
    offset = "runtime" if row["byte_offset"] is None else f"0x{row['byte_offset']:X}.{row['bit_in_byte']}"
    markdown.append(f"| `{row['field']}` | `{row['region']}` / {row['space']} | "
                    f"{offset} | {row['bits'] if row['bits'] is not None else 'dynamic'} bit | "
                    f"{row['address_hex']} | {row['scope']}{' / '+row['inside_method'] if row['inside_method'] else ''} |")
markdown += ["", "## EmbeddedControl, SystemIO и индексные/банковые поля", "",
             "| Объект | Регион / space | Смещение | Ширина | Context |", "|---|---|---:|---:|---|"]
for row in declarations:
    if row["reserved"] or row["space"] not in ("EmbeddedControl", "SystemIO") and row["kind"] == "Field": continue
    offset = "runtime" if row["byte_offset"] is None else f"0x{row['byte_offset']:X}.{row['bit_in_byte']}"
    markdown.append(f"| `{row['field'] or row['kind']}` | `{row['region']}` / {row['space'] or 'indirect'} | "
                    f"{offset} | {row['bits'] if row['bits'] is not None else 'dynamic'} bit | "
                    f"{row['scope']}{' / '+row['inside_method'] if row['inside_method'] else ''} |")
markdown += ["", "## EC-значения, связанные с температурой", "",
             "| Поле | Адрес / пространство | Что позволяет утверждать DSDT |", "|---|---|---|"]
for row in declarations:
    if row["field"] in ("CPTM", "VGAT", "XHPP", "VDDT", "VDDE", "SOCT", "SOCE"):
        addr = row["address_hex"] or (
            f"{row['region']} + 0x{row['byte_offset']:X}" if row["byte_offset"] is not None
            else f"{row['region']} + динамическое смещение")
        meaning = {"CPTM": "Комментарий/потребитель DSDT обозначает как CPU thermal-control temperature",
                   "VGAT": "Поле есть; само объявление не задаёт единицу или масштаб",
                   "XHPP": "7-битное поле, используемое `ECTZ._TMP`; это не отдельное именованное поле NPU",
                   "VDDT": "Имя поля не доказывает температуру или единицы",
                   "VDDE": "Имя поля не доказывает температуру или единицы",
                   "SOCT": "Имя поля не доказывает температуру SoC или единицы",
                   "SOCE": "Имя поля не доказывает температуру SoC или единицы"}[row['field']]
        markdown.append(f"| `{row['field']}` | {addr} / {row['space']} | {meaning} |")
markdown += ["", "Полный перечень, включая Scope, смещения, зарезервированные биты и строки источника, "
             f"сохранён в [`{fields_path.name}`]({fields_path.name}). Все OperationRegion перечислены в "
             f"[`{regions_path.name}`]({regions_path.name})."]
(OUT / "dsdt-fields.md").write_text("\n".join(markdown) + "\n", encoding="utf-8")
print(json.dumps({"source": str(SRC), "operation_regions": len(regions), "field_entries": len(declarations),
                  "named": sum(bool(x["field"]) and not x["reserved"] for x in declarations),
                  "indirect": sum(not x["field"] for x in declarations),
                  "reserved": sum(x["reserved"] for x in declarations), "by_space": by_space,
                  "fields_csv": str(fields_path), "regions_csv": str(regions_path),
                  "markdown": str(OUT / 'dsdt-fields.md')}, default=lambda x: dict(x), ensure_ascii=False, indent=2))
