"""Compare saved files only. Requires UEFIExtract all output for the two prefixes.

Saved scratch inputs live in Decompile; new comparison outputs go to out. No firmware APIs,
drivers or hardware executables are used. Variable payloads are not published.
"""
from pathlib import Path
import collections, hashlib, json, re, zipfile, uuid

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out/gm6ir0c-logs15-20261024' / ('nvram-' + uuid.uuid4().hex)
OUT.mkdir(parents=True, exist_ok=False)
SCRATCH = ROOT / '../Decompile/GM6IR0C/GM6_NVAR_Comparison_20261024'
sha = lambda b: hashlib.sha256(b).hexdigest()

def fields(path):
    return dict(re.findall(r'^([^:\r\n]+): (.*)$', path.read_text(encoding='utf-8', errors='replace'), re.M))

def store(image, number):
    folder = next(p for p in (SCRATCH / (image + '-nvram.bin.dump')).iterdir()
                  if p.is_dir() and p.name.startswith(str(number) + ' ')) / '0 NVAR store'
    metadata = fields(folder / 'info.txt')
    entries = {}
    for directory in folder.iterdir():
        if not directory.is_dir() or not (directory / 'info.txt').exists():
            continue
        item = fields(directory / 'info.txt')
        if item.get('Type') != 'NVAR entry':
            continue
        item['directory'] = directory
        entries[int(item['Offset'].split('h')[0], 16)] = item
    values, broken = {}, []
    for offset, root in entries.items():
        if 'Variable GUID' not in root or root['Subtype'] not in ('Full', 'Link'):
            continue
        entry = root
        visited = set()
        while 'Next node at offset' in entry:
            target = int(entry['Next node at offset'].split('h')[0], 16)
            if target in visited or target not in entries:
                broken.append({'name': root.get('Text'), 'target': hex(target)})
                entry = {}
                break
            visited.add(target)
            entry = entries[target]
        if entry.get('Subtype') not in ('Full', 'Data'):
            continue  # Invalidated targets represent deleted/historical chains.
        key = (root['Variable GUID'], root.get('Text', ''))
        assert key not in values, key
        data = (entry['directory'] / 'body.bin').read_bytes()
        values[key] = {'bytes': len(data), 'sha256': sha(data), 'base': entry['Base'],
                       'data': data, 'links_followed': len(visited)}
    return metadata, values, {'parsed_entries': len(entries), 'subtypes': dict(collections.Counter(e['Subtype'] for e in entries.values())),
                              'resolved_variables': len(values), 'unresolved_chains': broken}

old_meta, old, old_summary = store('stock', 0)
new_meta, new, new_summary = store('fresh', 1)
assert old_meta['State'] == new_meta['State'] == 'F0h'
old_summary['store_index'] = 0
new_summary['store_index'] = 1
for summary, metadata in [(old_summary, old_meta), (new_summary, new_meta)]:
    summary['ffs_state'] = metadata['State']
    summary['ffs_header_checksum'] = metadata['Header checksum']
    summary['ffs_data_checksum'] = metadata['Data checksum']

same, changed, removed, added = [], [], [], []
for key in sorted(old.keys() | new.keys()):
    before, after = old.get(key), new.get(key)
    row = {'guid': key[0], 'name': key[1]}
    for label, value in [('stock', before), ('fresh', after)]:
        row[label] = {k: v for k, v in value.items() if k != 'data'} if value else None
    if before and after:
        if before['data'] == after['data']:
            same.append(row)
        else:
            row['different_bytes_in_common_length'] = sum(a != b for a, b in zip(before['data'], after['data']))
            changed.append(row)
    elif before:
        removed.append(row)
    else:
        added.append(row)

uni_key = ('9F33F85C-13CA-4FD1-9C4A-96217722C593', 'UniWillVariable')
uold, unew = old[uni_key]['data'], new[uni_key]['data']
uni = {'bytes': len(uold), 'differences': [{'offset': hex(i), 'stock': f'{a:02X}', 'fresh': f'{b:02X}'}
       for i, (a, b) in enumerate(zip(uold, unew)) if a != b],
       'battery_storage_0x30_0x32': {'stock': uold[0x30:0x33].hex(' '), 'fresh': unew[0x30:0x33].hex(' ')},
       'ec_ram_0x770_not_identified_by_these_fields': True}
layout_evidence = json.loads((ROOT / 'research/ec-platform-catalogue-20261023/uefi-charge-path.json').read_text(encoding='utf-8'))
layout = layout_evidence['struct_layouts'][0]
assert sha(Path(layout['file']).read_bytes()) == layout['sha256']
uni['decompiled_structure'] = {'file': layout['file'], 'sha256': layout['sha256'],
                               'mapping_is_static_struct_layout_not_a_live_firmware_query': True}
for difference in uni['differences']:
    field = next(f for f in layout['fields'] if f['offset'] == int(difference['offset'], 16))
    assert field['size'] == 1
    difference['oem_structure_field'] = field['name']
stock_full = (ROOT / 'research/ec-platform-catalogue-20261023/new-backup-20261023/backup.fd').read_bytes()
patch_full = (ROOT / 'research/ub23/new.fd').read_bytes()
with zipfile.ZipFile(ROOT / 'Users_logs/logs_GM6IR0C (15).zip') as archive:
    fresh = archive.read('logs_GM6IR0C/bios-dumps/bios-20261024-203144-0/pass1.partial.bin')
stock, patched = stock_full[0x1000000:], patch_full[0x1000000:]
result = {'sources': {'stock_full_sha256': sha(stock_full), 'patched_full_sha256': sha(patch_full), 'fresh_region_sha256': sha(fresh)},
          'bios_region_spi_base': '0x1000000', 'bios_region_bytes': len(fresh), 'nvram_prefix_bytes': 0x60000,
          'stock_patched_prefix_identical': stock[:0x60000] == patched[:0x60000],
          'fresh_patched_after_prefix_identical': fresh[0x60000:] == patched[0x60000:],
          'stock_patched_prefix_sha256': sha(stock[:0x60000]),
          'different_bytes_fresh_vs_stock': sum(a != b for a, b in zip(fresh, stock)),
          'different_bytes_fresh_vs_patched': sum(a != b for a, b in zip(fresh, patched)),
          'stores': {'stock': old_summary, 'fresh': new_summary},
          'same': same, 'changed': changed, 'only_stock': removed, 'only_fresh': added, 'uniwill_variable': uni,
          'limitations': ['F0 stores selected by their FFS state; no live GetVariable query was performed.',
                          'No EC e-flash was captured. Host BIOS NVAR is not a direct measurement of EC RAM770.',
                          'UEFIExtract was run on 384KiB excerpts; no Volume Top File in that excerpt is expected.',
                          'Variable additions/deletions alone cannot identify their cause or prove user-data loss.']}
(OUT / 'nvram-comparison.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'stores': result['stores'], 'same': len(same), 'changed': [x['name'] for x in changed],
                  'only_stock': [x['name'] for x in removed], 'only_fresh': [x['name'] for x in added],
                  'uniwill_variable': uni}, ensure_ascii=False, indent=2))
