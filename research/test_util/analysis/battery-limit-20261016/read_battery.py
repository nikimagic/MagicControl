import ctypes as C, ctypes.wintypes as W, pathlib,json,time,datetime,argparse
ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);args=ap.parse_args()
out=pathlib.Path(args.output);out.mkdir(parents=True,exist_ok=False)
k=C.WinDLL('kernel32',use_last_error=True)
k.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,C.c_void_p,W.DWORD,W.DWORD,W.HANDLE];k.CreateFileW.restype=W.HANDLE
k.CreateMutexW.argtypes=[C.c_void_p,W.BOOL,W.LPCWSTR];k.CreateMutexW.restype=W.HANDLE
k.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];k.WaitForSingleObject.restype=W.DWORD
k.ReleaseMutex.argtypes=[W.HANDLE];k.CloseHandle.argtypes=[W.HANDLE]
k.DeviceIoControl.argtypes=[W.HANDLE,W.DWORD,C.c_void_p,W.DWORD,C.c_void_p,W.DWORD,C.POINTER(W.DWORD),C.c_void_p];k.DeviceIoControl.restype=W.BOOL
allowed={0x9c40a488,0x9c40a490,2621482192,2621482196,2621482200}
h=k.CreateFileW(r'\\.\ACPIDriver',0xc0000000,3,None,3,0,None)
if h==C.c_void_p(-1).value:raise C.WinError(C.get_last_error())
m=k.CreateMutexW(None,False,r'Local\MagicControl.EcTransactions.v1')
if not m:raise C.WinError(C.get_last_error())
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def query(code,address):
 assert code in allowed
 packet=(W.DWORD*2)(address,0);buf=(C.c_ubyte*16)();n=W.DWORD();C.set_last_error(0);start=time.perf_counter()
 ok=k.DeviceIoControl(h,code,packet,C.sizeof(packet),buf,16,C.byref(n),None)
 return dict(utc=stamp(),ioctl=hex(code),address=hex(address),ok=bool(ok),error=0 if ok else C.get_last_error(),returned=n.value,raw_hex=bytes(buf[:min(n.value,16)]).hex(),u8=buf[0] if ok and n.value else None,u32=int.from_bytes(bytes(buf[:4]),'little') if ok and n.value>=4 else None,duration_ms=round((time.perf_counter()-start)*1000,3))
try:
 with (out/'samples.jsonl').open('w',encoding='utf-8',buffering=1) as f:
  for sample in range(3):
   locked=k.WaitForSingleObject(m,3000)
   if locked not in (0,128):raise RuntimeError('EC busy')
   try:
    reads=[]
    for a in [0x7a6,0x7b9,0x7d0,0x7c3,0x770,0x742,0x44c,0x7cc,0x726,0x727,0x741,0x7c5,0x7c6]:
     reads.append(query(0x9c40a488,a))
     if a in [0x7a6,0x7b9,0x7d0]:reads.append(query(0x9c40a490,0xfed50000+a))
    f.write(json.dumps(dict(sample=sample,utc=stamp(),reads=reads))+'\n')
   finally:k.ReleaseMutex(m)
   time.sleep(1)
finally:k.CloseHandle(m);k.CloseHandle(h)
print('DONE READ ONLY')

