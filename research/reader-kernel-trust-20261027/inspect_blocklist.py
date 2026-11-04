"""Compare exact and Authenticode hashes with the downloaded Microsoft deny XML.
This is a bounded static comparison, NOT a full Code Integrity admission test.
"""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import hashlib,json,xml.etree.ElementTree as ET
OUT=Path(__file__).resolve().parent
k=C.WinDLL('kernel32',use_last_error=True);w=C.WinDLL('wintrust',use_last_error=True)
k.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,C.c_void_p,W.DWORD,W.DWORD,W.HANDLE];k.CreateFileW.restype=W.HANDLE
k.CloseHandle.argtypes=[W.HANDLE]
w.CryptCATAdminAcquireContext2.argtypes=[C.POINTER(C.c_void_p),C.c_void_p,W.LPCWSTR,C.c_void_p,W.DWORD];w.CryptCATAdminAcquireContext2.restype=W.BOOL
w.CryptCATAdminCalcHashFromFileHandle2.argtypes=[C.c_void_p,W.HANDLE,C.POINTER(W.DWORD),C.c_void_p,W.DWORD];w.CryptCATAdminCalcHashFromFileHandle2.restype=W.BOOL
w.CryptCATAdminReleaseContext.argtypes=[C.c_void_p,W.DWORD]
def authhash(path,algorithm):
    context=C.c_void_p()
    if not w.CryptCATAdminAcquireContext2(C.byref(context),None,algorithm,None,0):raise C.WinError(C.get_last_error())
    handle=k.CreateFileW(str(path),0x80000000,1,None,3,0x80,None)
    if handle==C.c_void_p(-1).value:raise C.WinError(C.get_last_error())
    try:
        size=W.DWORD()
        if not w.CryptCATAdminCalcHashFromFileHandle2(context,handle,C.byref(size),None,0):raise C.WinError(C.get_last_error())
        buffer=(C.c_ubyte*size.value)()
        if not w.CryptCATAdminCalcHashFromFileHandle2(context,handle,C.byref(size),buffer,0):raise C.WinError(C.get_last_error())
        return bytes(buffer).hex().upper()
    finally:k.CloseHandle(handle);w.CryptCATAdminReleaseContext(context,0)
xml=ET.parse(OUT/'DriverPolicy_Enforced.xml');ns={'ci':'urn:schemas-microsoft-com:sipolicy'}
deny=[d.attrib for d in xml.findall('.//ci:Deny',ns)]
trust=json.loads((OUT/'TRUST.json').read_text())
paths={r['file']:Path(r['path']) for r in trust['files']}
records=[]
for name,path in paths.items():
    data=path.read_bytes()
    hashes=dict(flat_sha1=hashlib.sha1(data).hexdigest().upper(),flat_sha256=hashlib.sha256(data).hexdigest().upper(),
                authenticode_sha1=authhash(path,'SHA1'),authenticode_sha256=authhash(path,'SHA256'))
    hits=[d for d in deny if d.get('Hash','').upper() in hashes.values()]
    records.append(dict(file=name,path=str(path),hashes=hashes,exact_hash_deny_matches=hits))
result=dict(policy_version=xml.find('ci:VersionEx',ns).text,deny_count=len(deny),applied=False,
            scope='Exact SHA1/SHA256 and Authenticode hash rules only; no page-hash, signer/version or HVCI execution inference',files=records)
(OUT/'BLOCKLIST.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
for r in records:print(r['file'],'exact hash denies',len(r['exact_hash_deny_matches']))
