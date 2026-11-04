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
write_addresses={0x7c3,0x751,0x783,0x784,0x785}
own={};original={};baseline_flags={};deadline=time.monotonic()+230
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
def write_ec(address,value,restoring=False):
 assert address in write_addresses and 0<=value<=255
 if address==0x751:assert (value & ~0xb0)==(original[address] & ~0xb0)
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
  ec=guard();check_supply();values=smu();battery=battery_sample()
  if battery['percent'] is not None and battery['percent']>=87 and battery['watts']>1:raise RuntimeError('Charge rose to 87%; stop and restore platform')
  emit('sample',phase=phase,ec=ec,smu=values,battery=battery)
  time.sleep(1)
import hashlib
expected_ac=0 if out.name.startswith('dc') else 1
class PowerStatus(C.Structure):
 _fields_=[('ac',W.BYTE),('flags',W.BYTE),('percent',W.BYTE),('reserved',W.BYTE),('life',W.DWORD),('full',W.DWORD)]
k.GetSystemPowerStatus.argtypes=[C.POINTER(PowerStatus)];k.GetSystemPowerStatus.restype=W.BOOL
def check_supply():
 status=PowerStatus()
 if not k.GetSystemPowerStatus(C.byref(status)) or status.ac!=expected_ac:raise RuntimeError('Supply changed; stop and restore')
def table_hash():
 lock_ec()
 try:return hashlib.sha256(bytes(read_ec(a) for a in range(0xf00,0xf60))).hexdigest()
 finally:k.ReleaseMutex(m)
def profile(code):
 guard();check_supply()
 value=(read_ec(0x751)&~0xb0)|code
 write_ec(0x751,value,restoring=value==original[0x751])
 baseline_flags['0x751']=value
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
import uuid
class Guid(C.Structure):_fields_=[('bytes',C.c_ubyte*16)]
class Interface(C.Structure):_fields_=[('cb',W.DWORD),('guid',Guid),('flags',W.DWORD),('reserved',C.c_size_t)]
setup=C.WinDLL('setupapi',use_last_error=True)
setup.SetupDiGetClassDevsW.argtypes=[C.POINTER(Guid),W.LPCWSTR,W.HWND,W.DWORD];setup.SetupDiGetClassDevsW.restype=W.HANDLE
setup.SetupDiEnumDeviceInterfaces.argtypes=[W.HANDLE,C.c_void_p,C.POINTER(Guid),W.DWORD,C.POINTER(Interface)];setup.SetupDiEnumDeviceInterfaces.restype=W.BOOL
setup.SetupDiGetDeviceInterfaceDetailW.argtypes=[W.HANDLE,C.POINTER(Interface),C.c_void_p,W.DWORD,C.POINTER(W.DWORD),C.c_void_p];setup.SetupDiGetDeviceInterfaceDetailW.restype=W.BOOL
setup.SetupDiDestroyDeviceInfoList.argtypes=[W.HANDLE];setup.SetupDiDestroyDeviceInfoList.restype=W.BOOL
bat_handle=None
def battery_ioctl(handle,code,words,size):
 assert code in [0x294040,0x294044,0x29404c]
 packet=(W.DWORD*len(words))(*words);buf=(C.c_ubyte*size)();n=W.DWORD()
 if not k.DeviceIoControl(handle,code,packet,C.sizeof(packet),buf,size,C.byref(n),None) or n.value<size:raise C.WinError(C.get_last_error())
 return bytes(buf)
def battery_open():
 global bat_handle
 guid=Guid();C.memmove(C.byref(guid),uuid.UUID('72631e54-78a4-11d0-bcf7-00aa00b7b32a').bytes_le,16)
 devices=setup.SetupDiGetClassDevsW(C.byref(guid),None,None,0x12)
 if devices==C.c_void_p(-1).value:raise C.WinError(C.get_last_error())
 try:
  for i in range(16):
   entry=Interface();entry.cb=C.sizeof(entry)
   if not setup.SetupDiEnumDeviceInterfaces(devices,None,C.byref(guid),i,C.byref(entry)):break
   required=W.DWORD();setup.SetupDiGetDeviceInterfaceDetailW(devices,C.byref(entry),None,0,C.byref(required),None)
   if not 8<=required.value<=65536:continue
   detail=C.create_string_buffer(required.value);C.cast(detail,C.POINTER(W.DWORD))[0]=8
   if not setup.SetupDiGetDeviceInterfaceDetailW(devices,C.byref(entry),detail,required.value,None,None):continue
   path=C.wstring_at(C.addressof(detail)+4)
   candidate=k.CreateFileW(path,0x80000000,3,None,3,0,None)
   if candidate==C.c_void_p(-1).value:continue
   try:
    tag=struct.unpack('<I',battery_ioctl(candidate,0x294040,[0],4))[0]
    info=battery_ioctl(candidate,0x294044,[tag,0,0],36);caps=struct.unpack_from('<I',info)[0]
    if tag and caps&0x80000000 and not caps&0x20000000:
     bat_handle=candidate;return
   except Exception:pass
   k.CloseHandle(candidate)
 finally:setup.SetupDiDestroyDeviceInfoList(devices)
 raise RuntimeError('Native Battery IOCTL source unavailable; no charge test')
def battery_sample():
 if bat_handle is None:battery_open()
 tag=struct.unpack('<I',battery_ioctl(bat_handle,0x294040,[0],4))[0]
 info=battery_ioctl(bat_handle,0x294044,[tag,0,0],36)
 status=battery_ioctl(bat_handle,0x29404c,[tag,0,0,0,0],16)
 caps=struct.unpack_from('<I',info)[0];state,capacity,voltage,rate=struct.unpack('<IIIi',status)
 full=struct.unpack_from('<I',info,16)[0]
 if caps&0x40000000 or rate==-2147483648:raise RuntimeError('Battery rate is not reported in physical watts')
 power=PowerStatus();k.GetSystemPowerStatus(C.byref(power))
 return dict(percent=100.0*capacity/full if full else None,windows_percent=power.percent,state=state,
  online=bool(state&1),charging=bool(state&4),discharging=bool(state&2),watts=rate/1000.0,
  volts=voltage/1000.0,milliamps=rate*1000.0/voltage if voltage else None,
  capacity_mwh=capacity,full_mwh=full,source='Windows Battery Class IOCTL')

try:
 check_supply();first=snapshot()
 if [first[hex(a)] for a in [0x454,0x456,0x740]]!=[82,72,26]:raise RuntimeError('Wrong board')
 if expected_ac and max(first['0x7cd'],first['0x7ce'])!=100:raise RuntimeError('Expected 100W adapter')
 if (first['0x7b9']&127)!=85 or (first['0x7d0']&127)!=80:raise RuntimeError('Expected upper=85 lower=80; no settings changed')
 battery_sample()
 if first['0x7c3']!=4 or first['0x770']!=255:raise RuntimeError('Expected platform 4, ROM FF')
 if any(first[hex(a)] for a in [0x783,0x784,0x785]):raise RuntimeError('Manual overrides active')
 original={0x7c3:4,0x751:first['0x751'],0x783:0,0x784:0,0x785:0}
 baseline_flags={hex(a):first[hex(a)] for a in [*fan_addresses,0x783,0x784,0x785]}
 tables_before=table_hash()
 with (out/'before.json').open('w',encoding='utf-8') as f:
  json.dump(dict(utc=stamp(),original_ec=first,tables_sha256=tables_before,write_allowlist=['0x7c3','0x751 mask 0xb0 ONLY','0x783','0x784','0x785'],fan_writes=0,smu_setters=False),f,indent=2);f.flush();os.fsync(f.fileno())
 emit('battery_initial',battery=battery_sample(),ec=first)
 if lib.pawnio_open(C.byref(module))<0:raise RuntimeError('PawnIO open failed')
 data=(runtime/'RyzenSMU.bin').read_bytes();blob=C.create_string_buffer(data)
 if lib.pawnio_load(module,blob,len(data))<0:raise RuntimeError('PawnIO load failed')
 for code,name in [(0xa0,'office'),(0,'balanced'),(0x10,'turbo')]:
  profile(code);collect(name+'_platform4',10)
  guard();check_supply();write_ec(0x7c3,7);collect(name+'_platform7',15)
  write_ec(0x7c3,4,restoring=True);collect(name+'_platform4_return',10)
 profile(original[0x751]&0xb0);collect('original_profile_platform4',5)
 # Manual comparison uses a conservative fixed set in both platform states.
 for address,value in [(0x783,15),(0x784,20),(0x785,25)]:
  guard();check_supply();write_ec(address,value);baseline_flags[hex(address)]=value
 collect('manual_platform4',10)
 guard();check_supply();write_ec(0x7c3,7);collect('manual_platform7',15)
 write_ec(0x7c3,4,restoring=True);collect('manual_platform4_return',10)
 for address in [0x783,0x784,0x785]:
  write_ec(address,original[address],restoring=True);baseline_flags[hex(address)]=original[address]
 collect('restored_original',10)
 after=table_hash();emit('completed',tables_before=tables_before,tables_after=after,tables_unchanged=after==tables_before,fan_writes=0,smu_setters=False)
except BaseException as error:emit('error',error=str(error),traceback=traceback.format_exc())
finally:
 for address in [0x7c3,0x751,0x783,0x784,0x785]:
  if address in own and own[address]!=original[address]:
   try:write_ec(address,original[address],restoring=True)
   except BaseException as e:emit('restore_error',address=hex(address),error=str(e))
 try:emit('final',ec=snapshot(),smu=smu() if module else None,tables_sha256=table_hash(),battery=battery_sample())
 except BaseException as e:emit('final_read_error',error=str(e))
 if bat_handle:k.CloseHandle(bat_handle)
 if module:lib.pawnio_close(module)
 k.CloseHandle(pci);k.CloseHandle(m);k.CloseHandle(h);log.close()

