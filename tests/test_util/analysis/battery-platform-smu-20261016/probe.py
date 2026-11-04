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
write_addresses={0x7c3}
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
 try:return {hex(a):read_ec(a) for a in [0x454,0x456,0x740,*fan_addresses,0x783,0x784,0x785,0x7c3,0x770,0x742,0x7d0,0x7b9,0x7cc,0x7cd,0x7ce]}
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
def write_ec(address,value,restoring=False):
 assert address in write_addresses and 0<=value<=255
 lock_ec()
 try:
  before=read_ec(address)
  expected=own.get(address,original[address])
  if before!=expected:raise RuntimeError('External override change; no blind write '+hex(address))
  if before==value:return
  # Mark before IOCTL so finally also handles a write with a failed readback.
  own[address]=value
  packet=(W.DWORD*2)(address,value);buf=(C.c_ubyte*16)();n=W.DWORD()
  if not k.DeviceIoControl(h,0x9c40a48c,packet,8,buf,16,C.byref(n),None):raise C.WinError(C.get_last_error())
  time.sleep(.03);actual=read_ec(address)
  emit('ec_write',address=hex(address),before=before,requested=value,actual=actual,restoring=restoring,fan_writes=0)
  if actual!=value:raise RuntimeError('Write readback mismatch '+hex(address))
 finally:k.ReleaseMutex(m)
def collect(phase,seconds):
 stop=time.monotonic()+seconds
 while time.monotonic()<stop:
  ec=guard();check_supply();values=smu();emit('sample',phase=phase,ec=ec,smu=values)
  time.sleep(1)
import hashlib
class PowerStatus(C.Structure):
 _fields_=[('ac',W.BYTE),('flags',W.BYTE),('percent',W.BYTE),('reserved',W.BYTE),('life',W.DWORD),('full',W.DWORD)]
k.GetSystemPowerStatus.argtypes=[C.POINTER(PowerStatus)];k.GetSystemPowerStatus.restype=W.BOOL
expected_ac=1 if 'ac-' in out.name else 0
def check_supply():
 status=PowerStatus()
 if not k.GetSystemPowerStatus(C.byref(status)) or status.ac!=expected_ac:raise RuntimeError('Supply changed; stop and restore platform')
def table_hash():
 lock_ec()
 try:return hashlib.sha256(bytes(read_ec(a) for a in range(0xf00,0xf60))).hexdigest()
 finally:k.ReleaseMutex(m)
try:
 check_supply();first=snapshot()
 if [first[hex(a)] for a in [0x454,0x456,0x740]]!=[82,72,26]:raise RuntimeError('Wrong board')
 if first['0x7c3']!=4 or first['0x770']!=255:raise RuntimeError('Expected platform 4, ROM FF')
 if any(first[hex(a)] for a in [0x783,0x784,0x785]):raise RuntimeError('Manual power overrides are active; no writes performed')
 original={0x7c3:4};baseline_flags={hex(a):first[hex(a)] for a in fan_addresses}
 tables_before=table_hash()
 backup=dict(utc=stamp(),original_ec=first,tables_sha256=tables_before,write_allowlist=['0x7c3'],fan_writes=0,smu_setters=False,expected_ac=expected_ac)
 with (out/'before.json').open('w',encoding='utf-8') as f:
  json.dump(backup,f,indent=2);f.flush();os.fsync(f.fileno())
 if lib.pawnio_open(C.byref(module))<0:raise RuntimeError('PawnIO open failed')
 data=(runtime/'RyzenSMU.bin').read_bytes();blob=C.create_string_buffer(data)
 if lib.pawnio_load(module,blob,len(data))<0:raise RuntimeError('PawnIO load failed')
 collect('platform4_baseline',10)
 for platform,label,seconds in [(7,'platform7_first',20),(4,'platform4_return',20),(7,'platform7_repeat',20),(4,'platform4_final',12)]:
  guard();check_supply();write_ec(0x7c3,platform,restoring=platform==4);collect(label,seconds)
 after=table_hash();emit('completed',tables_before=tables_before,tables_after=after,tables_unchanged=after==tables_before,fan_writes=0,smu_setters=False)
except BaseException as error:
 emit('error',error=str(error),traceback=traceback.format_exc())
finally:
 if 0x7c3 in own and own[0x7c3]!=original[0x7c3]:
  try:write_ec(0x7c3,original[0x7c3],restoring=True)
  except BaseException as e:emit('restore_error',error=str(e))
 try:emit('final',ec=snapshot(),smu=smu() if module else None,tables_sha256=table_hash())
 except BaseException as e:emit('final_read_error',error=str(e))
 if module:lib.pawnio_close(module)
 k.CloseHandle(pci);k.CloseHandle(m);k.CloseHandle(h);log.close()

