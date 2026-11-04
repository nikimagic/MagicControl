"""Read-only per-signature Windows trust-provider inspection. No driver load."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json,uuid
ROOT=Path(__file__).resolve().parents[2]
class GUID(C.Structure):
    _fields_=[('a',W.DWORD),('b',W.WORD),('c',W.WORD),('d',C.c_ubyte*8)]
    @classmethod
    def parse(cls,v):return cls.from_buffer_copy(uuid.UUID(v).bytes_le)
class SIG(C.Structure):
    _fields_=[('cb',W.DWORD),('index',W.DWORD),('flags',W.DWORD),('secondary',W.DWORD),('verified',W.DWORD),('crypto',C.c_void_p)]
class FILE(C.Structure):
    _fields_=[('cb',W.DWORD),('path',W.LPCWSTR),('handle',W.HANDLE),('known',C.c_void_p)]
class TRUST(C.Structure):
    _fields_=[('cb',W.DWORD),('policy',C.c_void_p),('sip',C.c_void_p),('ui',W.DWORD),('revocation',W.DWORD),('choice',W.DWORD),('file',C.POINTER(FILE)),('action',W.DWORD),('state',W.HANDLE),('url',W.LPCWSTR),('flags',W.DWORD),('context',W.DWORD),('settings',C.POINTER(SIG))]
class OSVER(C.Structure):
    _fields_=[('major',W.DWORD),('minor',W.DWORD)]
class DRIVER(C.Structure):
    _fields_=[('cb',W.DWORD),('reserved1',C.c_size_t),('reserved2',C.c_size_t),('platform',W.DWORD),('version',W.DWORD),('ver',W.WCHAR*260),('signer',W.WCHAR*260),('cert',C.c_void_p),('low',OSVER),('high',OSVER),('build_low',W.DWORD),('build_high',W.DWORD)]
wintrust=C.WinDLL('wintrust',use_last_error=True)
wintrust.WinVerifyTrust.argtypes=[W.HWND,C.POINTER(GUID),C.POINTER(TRUST)]
wintrust.WinVerifyTrust.restype=W.LONG
crypt=C.WinDLL('crypt32');crypt.CertFreeCertificateContext.argtypes=[C.c_void_p]
POLICIES={'authenticode':'00aac56b-cd44-11d0-8cc2-00c04fc295ee',
          'driver_whql':'f750e6c3-38ee-11d1-85e5-00c04fc295ee'}
files={
'AMI':ROOT/'src/platform/windows/runtime/AmiAfu/amigendrv64.sys',
'FPT16':ROOT/'research/reader-signatures-20261027/driver-images/IntelFpt-driver-reference.sys',
'FPT19':ROOT/'research/reader-signatures-20261027/driver-images/IntelFpt19-driver-reference.sys',
'NVFlash1':ROOT/'research/reader-signatures-20261027/driver-images/NvFlash-driver-1.reference.sys',
'NVFlash2':ROOT/'research/reader-signatures-20261027/driver-images/NvFlash-driver-2.reference.sys',
'Insyde':ROOT/'src/platform/windows/runtime/InsydeH2offt/H2OFFT64.sys'}
records=[]
for name,path in files.items():
    for policy,identifier in POLICIES.items():
        for index in (None,0,1):
            sig=SIG();sig.cb=C.sizeof(sig);sig.index=index or 0;sig.flags=3
            file=FILE();file.cb=C.sizeof(file);file.path=str(path)
            info=DRIVER();info.cb=C.sizeof(info)
            trust=TRUST();trust.cb=C.sizeof(trust);trust.ui=2;trust.choice=1
            trust.file=C.pointer(file);trust.action=1;trust.flags=0x1000
            if index is not None:trust.settings=C.pointer(sig)
            if policy=='driver_whql':trust.policy=C.addressof(info)
            guid=GUID.parse(identifier)
            code=wintrust.WinVerifyTrust(None,C.byref(guid),C.byref(trust))
            record=dict(file=name,path=str(path),policy=policy,index=index,status=f'0x{code&0xffffffff:08X}',
                        secondary_count=sig.secondary if index is not None else None,
                        verified_index=sig.verified if index is not None else None,driver_signer=info.signer)
            records.append(record);print(' '.join(map(str,(name,policy,index,record['status'],info.signer))).rstrip())
            trust.action=2;wintrust.WinVerifyTrust(None,C.byref(guid),C.byref(trust))
            if info.cert:crypt.CertFreeCertificateContext(info.cert)
(Path(__file__).parent/'TRUST.json').write_text(json.dumps(dict(drivers_loaded=False,files=records),indent=2),encoding='utf-8')
