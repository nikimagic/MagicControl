"""Read PE RCDATA as data only; never execute a reader or the product."""
from pathlib import Path
import ctypes as c
import hashlib, json, re

root=Path(__file__).resolve().parents[2]
k=c.WinDLL('kernel32',use_last_error=True)
k.LoadLibraryExW.argtypes=[c.c_wchar_p,c.c_void_p,c.c_uint32];k.LoadLibraryExW.restype=c.c_void_p
k.FindResourceW.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p];k.FindResourceW.restype=c.c_void_p
k.SizeofResource.argtypes=[c.c_void_p,c.c_void_p];k.SizeofResource.restype=c.c_uint32
k.LoadResource.argtypes=[c.c_void_p,c.c_void_p];k.LoadResource.restype=c.c_void_p
k.LockResource.argtypes=[c.c_void_p];k.LockResource.restype=c.c_void_p
k.FreeLibrary.argtypes=[c.c_void_p];k.FreeLibrary.restype=c.c_int
header=(root/'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text(encoding='utf-8')
entries=re.findall(r'\{(\d+),"(AmiAfu|IntelFpt|IntelFpt19|NvFlash|InsydeH2offt)","([^"]+)","([0-9a-f]{64})",(\d+)\}',header)
records=[]
for name in ['MagicControl.exe','mctl.exe']:
    path=root/'MagicControl/windows'/name
    handle=k.LoadLibraryExW(str(path),None,2) # LOAD_LIBRARY_AS_DATAFILE: no DllMain/entry point.
    if not handle:raise c.WinError(c.get_last_error())
    try:
        for rid,group,filename,expected,size in entries:
            resource=k.FindResourceW(handle,c.c_void_p(int(rid)),c.c_void_p(10))
            if not resource:raise c.WinError(c.get_last_error())
            actual_size=k.SizeofResource(handle,resource)
            loaded=k.LoadResource(handle,resource);pointer=k.LockResource(loaded)
            if not pointer:raise c.WinError(c.get_last_error())
            data=c.string_at(pointer,actual_size)
            sha=hashlib.sha256(data).hexdigest()
            source=root/'src/platform/windows/runtime'/group/filename
            source_sha=hashlib.sha256(source.read_bytes()).hexdigest()
            records.append(dict(product=name,package=group,file=filename,bytes=actual_size,
                                sha256=sha,expected_sha256=expected,source_sha256=source_sha,
                                matches=actual_size==int(size) and sha==expected==source_sha))
    finally:k.FreeLibrary(handle)
linux=root/'src/platform/linux/runtime/NvFlash/nvflash'
result=dict(readers_executed=False,drivers_loaded=False,embedded=records,
            linux_nvflash=dict(bytes=linux.stat().st_size,sha256=hashlib.sha256(linux.read_bytes()).hexdigest(),
                               format='ELF',authenticode_applicable=False))
(Path(__file__).parent/'EMBEDDED.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(f'{len(records)} embedded files; matches={sum(r["matches"] for r in records)}')
if not entries or not all(r['matches'] for r in records):raise SystemExit(1)
