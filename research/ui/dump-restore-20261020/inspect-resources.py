"""Static catalog/font/package evidence; does not execute app or hardware code."""
from pathlib import Path
import hashlib
import json
import re
import struct

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent

def u16(data, at=0):
    return struct.unpack_from('>H', data, at)[0]

def u32(data, at=0):
    return struct.unpack_from('>I', data, at)[0]

def font_info(path):
    data = path.read_bytes()
    tables = {}
    for i in range(u16(data, 4)):
        at = 12 + 16 * i
        offset, length = u32(data, at + 8), u32(data, at + 12)
        tables[data[at:at + 4].decode()] = data[offset:offset + length]
    cmap = tables['cmap']
    points = set()
    seen = set()
    for i in range(u16(cmap, 2)):
        at = u32(cmap, 8 + 8 * i)
        if at in seen:
            continue
        seen.add(at)
        fmt = u16(cmap, at)
        if fmt == 12:
            for j in range(u32(cmap, at + 12)):
                first, last, glyph = struct.unpack_from('>III', cmap, at + 16 + j * 12)
                points.update(cp for cp in range(first, last + 1) if glyph + cp - first)
        elif fmt == 4:
            count = u16(cmap, at + 6) // 2
            for j in range(count):
                end = u16(cmap, at + 14 + 2 * j)
                start = u16(cmap, at + 16 + 2 * count + 2 * j)
                delta = u16(cmap, at + 16 + 4 * count + 2 * j)
                position = at + 16 + 6 * count + 2 * j
                distance = u16(cmap, position)
                for cp in range(start, end + 1):
                    glyph = ((cp + delta) & 65535) if not distance else u16(cmap, position + distance + 2 * (cp - start))
                    if glyph and cp != 65535:
                        points.add(cp)
    languages = []
    if 'GSUB' in tables:
        gsub = tables['GSUB']
        scripts = u16(gsub, 4)
        for i in range(u16(gsub, scripts)):
            at = scripts + 2 + i * 6
            script = gsub[at:at + 4].decode()
            record = scripts + u16(gsub, at + 4)
            for j in range(u16(gsub, record + 2)):
                lang_at = record + 4 + j * 6
                languages.append([script, gsub[lang_at:lang_at + 4].decode().strip()])
    return {'path': str(path.relative_to(ROOT)), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest(), 'cmap_count': len(points),
            'gsub_languages': languages}, points

entries = []
literal = r'"(?:[^"\\]|\\.)*"'
entry = re.compile(r'Entry\s*\{\s*(' + literal + r')\s*,\s*(' + literal + r')\s*,\s*(' + literal + r')\s*,\s*(' + literal + r')\s*\}')
for path in sorted(p for p in (ROOT / 'src/gui/common').iterdir()
                   if p.name.startswith(('Localization', 'TranslationCatalog')) and p.suffix in ('.h', '.inc')):
    for match in entry.finditer(path.read_text(encoding='utf-8')):
        entries.append([json.loads(value) for value in match.groups()])
languages = ('ru', 'en', 'zh-CN', 'zh-TW')
catalog = {'entries': len(entries), 'empty_fields': [], 'duplicate_source_keys': [], 'placeholder_mismatches': []}
seen = set()
for row in entries:
    if row[0] in seen:
        catalog['duplicate_source_keys'].append(row[0])
    seen.add(row[0])
    expected = set(re.findall(r'\{([a-zA-Z_]+)\}', row[0]))
    for language, text in zip(languages, row):
        if not text:
            catalog['empty_fields'].append([row[0], language])
        if set(re.findall(r'\{([a-zA-Z_]+)\}', text)) != expected:
            catalog['placeholder_mismatches'].append([row[0], language])
fonts = []
for name, columns in [('NotoSansSC', (0, 1, 2)), ('NotoSansTC', (3,))]:
    metadata, points = font_info(ROOT / f'src/gui/assets/fonts/{name}.ttf')
    metadata['missing_catalog_glyphs'] = {
        languages[i]: sorted({c for row in entries for c in row[i] if not c.isspace() and ord(c) not in points})
        for i in columns}
    metadata['license_present'] = (ROOT / f'src/gui/assets/fonts/OFL-{name}.txt').is_file()
    fonts.append(metadata)
result = {'scope': 'Static resource inspection, not a GUI/shaping/test run.', 'catalog': catalog, 'fonts': fonts}
(OUT / 'resource-evidence.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, ensure_ascii=False, indent=2))
