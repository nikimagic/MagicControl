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

import struct,os,math,traceback
runtime=pathlib.Path(r'C:\Users\admin\Documents\mechrevo_dsh\MagicControl\MagicControl\windows\PawnIO')
lib=C.WinDLL(str(runtime/'PawnIOLib.dll'),winmode=0x1100)
lib.pawnio_open.argtypes=[C.POINTER(C.c_void_p)];lib.pawnio_open.restype=C.c_long
lib.pawnio_load.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t];lib.pawnio_load.restype=C.c_long
lib.pawnio_execute.argtypes=[C.c_void_p,C.c_char_p,C.POINTER(C.c_uint64),C.c_size_t,C.POINTER(C.c_uint64),C.c_size_t,C.POINTER(C.c_size_t)];lib.pawnio_execute.restype=C.c_long
lib.pawnio_close.argtypes=[C.c_void_p];lib.pawnio_close.restype=C.c_long
module=C.c_void_p();pci=k.CreateMutexW(None,False,r'Global\Access_PCI')
fan_addresses=[0x726,0x727,0x741,0x751,0x7c5,0x7c6]
write_addresses=set()
own={};original={};baseline_flags={};deadline=time.monotonic()+160
log=(out/'samples.jsonl').open('w',encoding='utf-8',buffering=1)
def emit(event,**data):
 row=dict(utc=stamp(),event=event,**data);log.write(json.dumps(row)+'\n');print(json.dumps(row),flush=True)
def lock_ec():
 if k.WaitForSingleObject(m,3000) not in (0,128):raise RuntimeError('EC transaction busy')
def read_ec(address):
 r=query(0x9c40a488,address)
 if not r['ok'] or r['u8'] is None:raise RuntimeError('EC read failed '+hex(address))
 return r['u8']
def snapshot():
 lock_ec()
 try:return {hex(a):read_ec(a) for a in [0x454,0x456,0x740,*fan_addresses,0x783,0x784,0x785,0x7c3,0x770,0x742,0x7d0,0x7b9,0x7cc,0x7cd,0x7ce,0x7a5,0x7a6,0x782,0x753,0x754,0x786,0x49f,0x74c]}
 finally:k.ReleaseMutex(m)
def command(name,count):
 # Allowlist contains telemetry transfers only. NO SMU limit setters.
 assert name in ['ioctl_resolve_pm_table','ioctl_update_pm_table','ioctl_read_pm_table']
 buf=(C.c_uint64*max(1,count))();returned=C.c_size_t()
 hr=lib.pawnio_execute(module,name.encode(),None,0,buf if count else None,count,C.byref(returned))
 if hr<0 or returned.value<count:raise RuntimeError('PawnIO '+name+' failed '+hex(hr&0xffffffff))
 return list(buf)[:count]
def smu():
 if k.WaitForSingleObject(pci,3000) not in (0,128):raise RuntimeError('PCI busy')
 try:
  version=command('ioctl_resolve_pm_table',2)[0]
  if version!=0x5d0009:raise RuntimeError('Unsupported PM version '+hex(version))
  command('ioctl_update_pm_table',0)
  values=struct.unpack('<8f',struct.pack('<4Q',*command('ioctl_read_pm_table',4)))
  if any(not math.isfinite(values[i]) or not 0<values[i]<500 for i in [0,2,4]):raise RuntimeError('Invalid SMU limits')
  return dict(version=hex(version),stapm=values[0],fast=values[2],slow=values[4],apu_power=values[3],prefix_floats=values)
 finally:k.ReleaseMutex(pci)
def guard():
 if (out/'STOP').exists() or time.monotonic()>deadline:raise RuntimeError('Probe stopped/deadline')
 current=snapshot()
 for a,v in baseline_flags.items():
  if current[a]!=v:raise RuntimeError('Fan/mode flag changed externally: '+a)
 for a,v in original.items():
  if current[hex(a)]!=own.get(a,v):raise RuntimeError('Power override changed externally: '+hex(a))
 return current
def collect(phase,seconds):
 stop=time.monotonic()+seconds
 while time.monotonic()<stop:
  ec=guard();check_supply();values=smu();emit('sample',phase=phase,ec=ec,smu=values)
  time.sleep(1)
class PowerStatus(C.Structure):
 _fields_=[('ac',W.BYTE),('flags',W.BYTE),('percent',W.BYTE),('reserved',W.BYTE),('life',W.DWORD),('full',W.DWORD)]
k.GetSystemPowerStatus.argtypes=[C.POINTER(PowerStatus)];k.GetSystemPowerStatus.restype=W.BOOL
def check_supply():
 status=PowerStatus()
 if not k.GetSystemPowerStatus(C.byref(status)) or status.ac!=1:raise RuntimeError('Expected AC')
def smart_read():
 # OEM ReadSmartApcTableByKey: BB is read, AA (write) is NEVER used here.
 lock_ec()
 try:
  result=[]
  for offset in range(12):
   packet=(C.c_ubyte*2)(0xbb,offset);buf=(C.c_ubyte*4)();n=W.DWORD()
   ok=k.DeviceIoControl(h,0x9c40a500,packet,2,buf,4,C.byref(n),None)
   result.append(dict(offset=offset,ok=bool(ok),returned=n.value,raw=bytes(buf).hex(),error=0 if ok else C.get_last_error()))
  return result
 finally:k.ReleaseMutex(m)
try:
 check_supply();first=snapshot()
 if [first[hex(a)] for a in [0x454,0x456,0x740]]!=[82,72,26]:raise RuntimeError('Wrong board')
 baseline_flags={hex(a):first[hex(a)] for a in [*fan_addresses,0x783,0x784,0x785,0x7c3,0x7cd,0x7ce]}
 if lib.pawnio_open(C.byref(module))<0:raise RuntimeError('PawnIO open failed')
 data=(runtime/'RyzenSMU.bin').read_bytes();blob=C.create_string_buffer(data)
 if lib.pawnio_load(module,blob,len(data))<0:raise RuntimeError('PawnIO load failed')
 emit('start',ec=first,hardware_settings_writes=False,smartapc=smart_read())
 collect('control',8)
 emit('completed',ec=snapshot(),hardware_settings_writes=False)
except BaseException as error:emit('error',error=str(error),traceback=traceback.format_exc())
finally:
 if module:lib.pawnio_close(module)
 k.CloseHandle(pci);k.CloseHandle(m);k.CloseHandle(h);log.close()

