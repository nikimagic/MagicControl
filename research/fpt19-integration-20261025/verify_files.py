"""Offline build/portable/resource receipts. Never executes the application."""
from pathlib import Path
import hashlib, json, re, struct
root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
sha = lambda b: hashlib.sha256(b).hexdigest()
build = json.loads((root/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert build['status'] == 'complete'
receipt = dict(version=build['version'], hardware_executed=False, application_executed=False,
               build_status=build['status'], source_fingerprint=build['source_fingerprint'],
               application_input_count=len(build['application_inputs']), platforms={}, resources=[])
changed = [name for name, digest in build['application_inputs'].items() if sha((root/name).read_bytes()) != digest]
assert not changed, changed
receipt['application_inputs_match_built_sources'] = True
for platform, binary, produced in [('windows','MagicControl.exe','build/windows/bin/Release/MagicControl.exe'),
                                  ('linux','MagicControl','build/linux/bin/MagicControl')]:
    portable = root/'MagicControl'/platform
    data = (portable/binary).read_bytes()
    assert data == (root/produced).read_bytes()
    version = (portable/'build-version.txt').read_text(encoding='utf-8-sig').strip()
    assert version == build['version']
    assert sha(data).lower() == build['platforms'][platform]['sha256'].lower()
    receipt['platforms'][platform] = dict(portable=str(portable/binary), version=version, bytes=len(data), sha256=sha(data), byte_equal_to_build=True)

# Parse actual RT_RCDATA entries, not merely a byte search in the EXE.
pe = (root/'MagicControl/windows/MagicControl.exe').read_bytes()
u16 = lambda at: struct.unpack_from('<H',pe,at)[0]
u32 = lambda at: struct.unpack_from('<I',pe,at)[0]
nt = u32(0x3c); optional = nt+24
assert pe[nt:nt+4] == b'PE\0\0' and u16(optional) == 0x20b
sections = optional+u16(nt+20)
def offset(rva):
    for n in range(u16(nt+6)):
        at=sections+40*n
        size,base,raw_size,raw = struct.unpack_from('<IIII',pe,at+8)
        if base <= rva < base+max(size,raw_size):return raw+rva-base
    raise ValueError('Unmapped PE RVA')
resources = offset(u32(optional+112+16))
def directory(at):
    return [struct.unpack_from('<II',pe,at+16+i*8) for i in range(u16(at+12)+u16(at+14))]
def resource(resource_id):
    at=resources
    for wanted in [10,resource_id]:
        matches=[entry for entry in directory(at) if entry[0]==wanted]
        assert len(matches)==1 and matches[0][1]&0x80000000
        at=resources+(matches[0][1]&0x7fffffff)
    languages=directory(at);assert len(languages)==1 and not languages[0][1]&0x80000000
    entry=resources+languages[0][1];rva,size=struct.unpack_from('<II',pe,entry)
    start=offset(rva);return pe[start:start+size]
catalogue = (root/'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text()
for identity, group, name, digest, size in re.findall(r'\{(\d+),"(IntelFpt19|IntelFpt|AmiAfu)","([^"]+)","([a-f0-9]{64})",(\d+)\}',catalogue):
    data=resource(int(identity))
    assert len(data)==int(size) and sha(data)==digest
    assert data==(root/'src/platform/windows/runtime'/group/name).read_bytes()
    receipt['resources'].append(dict(id=int(identity),group=group,name=name,bytes=len(data),sha256=digest,original_bytes=True))
assert len([r for r in receipt['resources'] if r['group']=='IntelFpt'])==4
assert len([r for r in receipt['resources'] if r['group']=='IntelFpt19'])==4
assert len([r for r in receipt['resources'] if r['group']=='AmiAfu'])==5
for notice in ['NOTICE.txt','PROVENANCE.json']:
    assert (root/'MagicControl/windows/licenses/Intel-FPT'/notice).read_bytes()==(root/'src/platform/windows/runtime/IntelFpt'/notice).read_bytes()
for notice in ['NOTICE.txt','PROVENANCE.json']:
    assert (root/'MagicControl/windows/licenses/Intel-FPT19'/notice).read_bytes()==(root/'src/platform/windows/runtime/IntelFpt19'/notice).read_bytes()
receipt['intel_notices_staged'] = True
receipt['tests_run'] = False
(out/'verification.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in receipt.items() if k!='resources'},ensure_ascii=False,indent=2))
