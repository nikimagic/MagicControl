param([switch]$Apply,[string]$Runtime)
# Research probe, not application startup logic. No writes without -Apply.
# BIOS file13A50: 042C.bit1 selects 0532/053C -> PWM1808/1809.
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
if(!$Runtime){throw 'Supply the verified native MagicControl PawnIO runtime directory.'}
$identity=Get-ItemProperty 'HKLM:\HARDWARE\DESCRIPTION\System\BIOS'
if($identity.SystemManufacturer -ne 'HUAWEI' -or $identity.SystemProductName -ne 'HLYL-WXX9' -or $identity.BaseBoardProduct -ne 'HLYL-WXX9-PCB' -or $identity.BIOSVersion -ne '1.09'){throw 'Exact HLYL-WXX9 BIOS1.09 required.'}
$dll=Join-Path $Runtime 'PawnIOLib.dll';$module=Join-Path $Runtime 'LpcIO.bin'
if((Get-FileHash $dll).Hash -ne 'D71F62627D66983BB9F5B1C269F27BCD8C1B8A46E794377A6330F84C198F4443' -or (Get-FileHash $module).Hash -ne 'B3896A1CAB0D808FCA31FE2EBCAE045D59DAC690DA87B17C858BB8DA357EB45E'){throw 'Pinned runtime hash mismatch.'}
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading;
public sealed class HonorFanResearch : IDisposable {
 [DllImport("kernel32",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryEx(string path,IntPtr file,uint flags);
 [DllImport("kernel32")] static extern bool FreeLibrary(IntPtr handle);
 [DllImport("PawnIOLib.dll")] static extern int pawnio_open(out IntPtr handle);
 [DllImport("PawnIOLib.dll")] static extern int pawnio_load(IntPtr handle,byte[] data,UIntPtr size);
 [DllImport("PawnIOLib.dll",CharSet=CharSet.Ansi)] static extern int pawnio_execute(IntPtr handle,string name,ulong[] input,UIntPtr count,ulong[] output,UIntPtr capacity,out UIntPtr returned);
 [DllImport("PawnIOLib.dll")] static extern int pawnio_close(IntPtr handle);
 IntPtr library,handle; Mutex mutex; bool locked,entered,attempted; FileStream dllLock,moduleLock;
 byte original0,original1;const byte target=100;
 ulong Call(string name,ulong[] input,bool output=false){ulong[] values=output?new ulong[1]:new ulong[0];UIntPtr count;int hr=pawnio_execute(handle,name,input,(UIntPtr)input.Length,values,(UIntPtr)values.Length,out count);if(hr<0||count.ToUInt64()!=(ulong)values.Length)throw new Exception(name+" failed: "+hr.ToString("X8"));return output?values[0]:0;}
 void Out(ushort p,byte v){Call("ioctl_pio_outb",new ulong[]{p,v});}
 void Sio(byte r,byte v){Call("ioctl_superio_outb",new ulong[]{r,v});}
 byte Sio(byte r){return (byte)Call("ioctl_superio_inb",new ulong[]{r},true);}
 void Select(ushort a){Sio(0x2e,0x11);Sio(0x2f,(byte)(a>>8));Sio(0x2e,0x10);Sio(0x2f,(byte)a);Sio(0x2e,0x12);}
 public byte Read(ushort a){Select(a);return Sio(0x2f);}
 void Write(ushort a,byte v){if(a!=0x042c&&a!=0x0532&&a!=0x053c)throw new Exception("Outside probe allowlist");Select(a);Sio(0x2f,v);if(Read(a)!=v)throw new Exception("Readback mismatch");}
 static FileStream Pin(string path,string expected){var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read);using(var sha=SHA256.Create()){if(BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","")!=expected){stream.Dispose();throw new Exception("Pinned hash mismatch");}}return stream;}
 public HonorFanResearch(string dll,string module){try {
  dllLock=Pin(dll,"D71F62627D66983BB9F5B1C269F27BCD8C1B8A46E794377A6330F84C198F4443");moduleLock=Pin(module,"B3896A1CAB0D808FCA31FE2EBCAE045D59DAC690DA87B17C858BB8DA357EB45E");
  library=LoadLibraryEx(dll,IntPtr.Zero,0x1100);if(library==IntPtr.Zero)throw new Exception("DLL load failed");
  if(pawnio_open(out handle)<0)throw new Exception("PawnIO open failed");byte[] blob=new byte[(int)moduleLock.Length];moduleLock.Position=0;if(moduleLock.Read(blob,0,blob.Length)!=blob.Length||pawnio_load(handle,blob,(UIntPtr)blob.Length)<0)throw new Exception("LpcIO load failed");
  mutex=new Mutex(false,@"Global\Access_ISABUS.HTP.Method");try{locked=mutex.WaitOne(1000);}catch(AbandonedMutexException){locked=true;}if(!locked)throw new Exception("ISA bus busy");
  Call("ioctl_select_slot",new ulong[]{0});entered=true;foreach(byte v in new byte[]{0x87,1,0x55,0x55})Out(0x2e,v);
  if(Sio(0x20)!=0x89||Sio(0x21)!=0x87||Read(0x2000)!=0x89||Read(0x2001)!=0x87||(Read(0x200d)&3)!=3)throw new Exception("IT8987 I2EC identity/access mismatch");
 } catch {Dispose();throw;}}
 public string State(){return String.Format("flags={0:X2} period={1} duty0={2} duty1={3} target0={4} target1={5}",Read(0x042c),Read(0x1801),Read(0x1808),Read(0x1809),Read(0x0532),Read(0x053c));}
 void Guard(){if((Read(0x042c)&1)!=0||Read(0x1801)!=200||(Read(0x180f)&0xc0)!=0||(Read(0x1823)&2)==0||(Read(0x031a)&1)==0||Read(0x0400)!=0x10)throw new Exception("Power/PWM guard mismatch");}
 public void Begin(){Guard();if((Read(0x042c)&2)!=0)throw new Exception("Manual already active");original0=Read(0x0532);original1=Read(0x053c);var before=State();Thread.Sleep(75);if(before!=State())throw new Exception("Fan state unstable");attempted=true;
  Write(0x0532,target);Guard();Write(0x053c,target);Guard();Write(0x042c,(byte)(Read(0x042c)|2));Thread.Sleep(500);Guard();if((Read(0x042c)&2)==0||Read(0x1808)!=target||Read(0x1809)!=target)throw new Exception("Manual PWM not confirmed");}
 public void Restore(){if(!attempted)return;
  // Clear only our manual bit FIRST, allowing firmware to resume its own curve.
  // Never write PWM duty directly and never restore the entire old flag byte.
  Write(0x042c,(byte)(Read(0x042c)&0xfd));
  byte a=Read(0x0532),b=Read(0x053c);if((a!=target&&a!=original0)||(b!=target&&b!=original1))throw new Exception("Targets changed externally; manual cleared but target rollback refused");
  Write(0x0532,original0);Write(0x053c,original1);
  if((Read(0x042c)&2)!=0||Read(0x0532)!=original0||Read(0x053c)!=original1)throw new Exception("Rollback not confirmed");attempted=false;Console.WriteLine("AUTOMATIC RESTORED; target bytes restored");}
 public void Dispose(){try{Restore();}finally{try{if(entered){Out(0x2e,2);Out(0x2f,2);entered=false;}}finally{if(locked){mutex.ReleaseMutex();locked=false;}if(mutex!=null){mutex.Dispose();mutex=null;}if(handle!=IntPtr.Zero){pawnio_close(handle);handle=IntPtr.Zero;}if(library!=IntPtr.Zero){FreeLibrary(library);library=IntPtr.Zero;}if(moduleLock!=null){moduleLock.Dispose();moduleLock=null;}if(dllLock!=null){dllLock.Dispose();dllLock=null;}}}}
}
'@
$instance=Get-CimInstance -Namespace root/wmi -ClassName OemWMIMethod -OperationTimeoutSec 10 | Where-Object InstanceName -match 'HWMI' | Select-Object -First 1
if(!$instance){throw 'No HWMI instance.'}
function OemRead([byte]$group,[byte]$command,[byte]$index=0){$data=New-Object byte[] 64;$data[0]=$group;$data[1]=$command;$data[2]=$index;$r=Invoke-CimMethod -InputObject $instance -MethodName OemWMIfun -Arguments @{u8Input=$data} -OperationTimeoutSec 3;$v=@($r.u8Output);if($v.Count -lt 4 -or $v[0] -ne 0){throw 'HWMI getter failed'};return $v}
function Sample { $cpu=OemRead 2 2; $a=OemRead 2 8 0; $b=OemRead 2 8 1; $temp=$cpu[2]+$cpu[3]/10.0;if($cpu[1] -ne 0 -or $temp -lt 10 -or $temp -ge 70){throw 'CPU temperature outside 10..70 C probe range'}; Write-Output ('CPU={0}C RPM={1}/{2}' -f $temp,($a[1]+256*$a[2]),($b[1]+256*$b[2])) }
$probe=$null
try {
 Sample
 $probe=[HonorFanResearch]::new([IO.Path]::GetFullPath($dll),[IO.Path]::GetFullPath($module))
 Write-Output ('BEFORE '+$probe.State())
 if($Apply){
  # Research approval must explicitly authorize this 10-second 50% fan probe.
  $probe.Begin();Write-Output ('MANUAL '+$probe.State())
  foreach($i in 1..5){Start-Sleep -Seconds 2;Sample}
  $probe.Restore();Write-Output ('AFTER '+$probe.State())
 } else {Write-Output 'READ ONLY. No fan target/flag writes.'}
} finally {if($probe){$probe.Dispose()}}
