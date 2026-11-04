# Restricted correction of the already requested 681/682 identity probe.
# NO WIOP: installed driver helper 1DC0 overwrites its second ACPI argument.
# Two RIOP calls write only host address selectors, never data port 683.
# No EC RAM, SMBus controller, battery or persistent-device writes.
$ErrorActionPreference='Stop'
$output=Join-Path $PSScriptRoot 'i2ec-two-argument-probe.json'
if(Test-Path -LiteralPath $output){throw 'Existing result must not be overwritten'}
$expectedHash='6E360EE4A0C42B2A4EAF46208C1592C87F8CD71ACA2A6C52831A2AD356E61235'
$result=[ordered]@{success=$false;started_utc=[DateTime]::UtcNow.ToString('o');
  method='two RIOP calls; no three-argument helper';ec_data_port_writes=0;
  ec_memory_write_calls=0;smbus_transaction_calls=0}
$probe=$null
try {
  $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  if(!$admin){throw 'Administrator token required'}
  $result.board=(Get-CimInstance Win32_BaseBoard).Product
  if($result.board -ne 'WUJIE Series-X5SP4NAG'){throw 'SMBIOS board mismatch'}
  $result.driver_sha256=(Get-FileHash -LiteralPath "$env:WINDIR\System32\drivers\UWACPIDriver.sys" -Algorithm SHA256).Hash
  if($result.driver_sha256 -ne $expectedHash){throw 'Unreviewed driver hash'}
  Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public sealed class WujieI2ecTwoArgumentProbe:IDisposable {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateMutexExW(IntPtr p,string n,uint f,uint a);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(IntPtr h,uint code,byte[] input,uint inputSize,byte[] output,uint outputSize,out uint returned,IntPtr p);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint timeout);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReleaseMutex(IntPtr h);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 IntPtr device,gate;
 public readonly List<Dictionary<string,object>> Calls=new List<Dictionary<string,object>>();
 public readonly Dictionary<string,int> Values=new Dictionary<string,int>();
 public int SelectorWrites;
 public WujieI2ecTwoArgumentProbe(){
  device=CreateFileW(@"\\.\ACPIDriver",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
  if(device==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
  gate=CreateMutexExW(IntPtr.Zero,@"Global\MagicControl.EcTransactions.v2",0,0x100001);
  if(gate==IntPtr.Zero){int e=Marshal.GetLastWin32Error();CloseHandle(device);device=IntPtr.Zero;throw new Win32Exception(e);}
 }
 byte Invoke(uint code,uint first,uint second){
  byte[] input=new byte[8],output=new byte[16];uint returned;
  Array.Copy(BitConverter.GetBytes(first),0,input,0,4);
  Array.Copy(BitConverter.GetBytes(second),0,input,4,4);
  var call=new Dictionary<string,object>{{"utc",DateTime.UtcNow.ToString("o")},{"ioctl",code.ToString("X8")},
    {"input",BitConverter.ToString(input)},{"input_size",8}};
  Calls.Add(call);
  if(code==0x9c40a4c8)SelectorWrites++;
  bool ok=DeviceIoControl(device,code,input,8,output,16,out returned,IntPtr.Zero);
  int error=ok?0:Marshal.GetLastWin32Error();
  call.Add("success",ok);call.Add("returned",returned);call.Add("win32_error",error);
  call.Add("output",BitConverter.ToString(output,0,(int)Math.Min(returned,16u)));
  if(!ok)throw new Win32Exception(error);
  if(returned==0||returned>16)throw new Exception("Invalid IOCTL reply length");
  return output[0];
 }
 byte EcRead(ushort address){return Invoke(0x9c40a488,address,0);}
 byte I2ecRead(ushort address){
  Invoke(0x9c40a4c8,0x681,(uint)(address>>8));
  return Invoke(0x9c40a4c8,0x682,(uint)(address&255));
 }
 public void Run(){
  uint w=WaitForSingleObject(gate,5000);if(w!=0&&w!=0x80)throw new Exception("EC mutex unavailable");
  try {
   ushort[] ids={0x454,0x456,0x740};byte[] expected={82,72,26};
   for(int i=0;i<ids.Length;i++)if(EcRead(ids[i])!=expected[i])throw new Exception("Standard EC identity mismatch");
   for(int i=0;i<ids.Length;i++){
    byte actual=I2ecRead(ids[i]);Values.Add("ec_"+ids[i].ToString("X3"),actual);
    if(actual!=expected[i])throw new Exception("I2EC mismatch at "+ids[i].ToString("X3")+
     "; expected="+expected[i].ToString("X2")+"; actual="+actual.ToString("X2"));
   }
   foreach(ushort address in new ushort[]{0x200d,0x2012,0x2014,0x2015})Values.Add("ec_"+address.ToString("X4"),I2ecRead(address));
  }finally{ReleaseMutex(gate);}
 }
 public void Dispose(){if(device!=IntPtr.Zero&&device!=new IntPtr(-1))CloseHandle(device);if(gate!=IntPtr.Zero)CloseHandle(gate);device=gate=IntPtr.Zero;}
}
'@
  $probe=[WujieI2ecTwoArgumentProbe]::new()
  $probe.Run()
  $result.success=$true
}catch {$result.error=$_.ToString()}
finally {
  if($null -ne $probe){
    $result.calls=$probe.Calls;$result.values=$probe.Values
    $result.host_selector_writes=$probe.SelectorWrites;$probe.Dispose()
  }
  $result.completed_utc=[DateTime]::UtcNow.ToString('o')
  $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $output -Encoding utf8
}
