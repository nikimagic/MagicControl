$ErrorActionPreference='Stop'
$out=Join-Path $PSScriptRoot 'age-read-only.json'
try {
  $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  if(!$admin){throw 'Administrative token required'}
  $board=(Get-CimInstance Win32_BaseBoard).Product
  if($board -ne 'WUJIE Series-X5SP4NAG'){throw 'SMBIOS board mismatch'}
  Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public sealed class WujieAgeReadOnly:IDisposable {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateMutexExW(IntPtr p,string n,uint f,uint a);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(IntPtr h,uint code,byte[] input,uint inputSize,byte[] output,uint outputSize,out uint returned,IntPtr p);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint timeout);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReleaseMutex(IntPtr h);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 IntPtr device,gate;
 static readonly ushort[] addresses={0x454,0x456,0x740,0x314,0x315,0x36c,0x36d,0x39f,0x3a1,0x3a3,0x490,0x491,0x4a6,0x4a7,0x726,0x72d,0x72e,0x741,0x7a6,0x7c3,0x7cc,0x7f2,0x7f3,0x9c9,0x9ca,0x30e,0x30f,0x522,0x523,0x402,0x403,0x404,0x405};
 public WujieAgeReadOnly(){
  device=CreateFileW(@"\\.\ACPIDriver",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
  if(device==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
  gate=CreateMutexExW(IntPtr.Zero,@"Global\MagicControl.EcTransactions.v2",0,0x100001);
  if(gate==IntPtr.Zero){int e=Marshal.GetLastWin32Error();CloseHandle(device);device=IntPtr.Zero;throw new Win32Exception(e);}
 }
 byte Read(ushort address){byte[] input=new byte[8],output=new byte[16];Array.Copy(BitConverter.GetBytes((uint)address),input,4);uint n;
  if(!DeviceIoControl(device,0x9c40a488,input,8,output,16,out n,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error());
  if(n==0||n>16)throw new Exception("Invalid EC reply length");return output[0];}
 public Dictionary<string,int> Sample(){uint w=WaitForSingleObject(gate,5000);if(w!=0&&w!=0x80)throw new Exception("EC mutex unavailable");
  try{if(Read(0x454)!=82||Read(0x456)!=72||Read(0x740)!=26)throw new Exception("EC identity mismatch");
   var r=new Dictionary<string,int>();foreach(var a in addresses)r.Add("0x"+a.ToString("X3"),Read(a));return r;
  }finally{ReleaseMutex(gate);}}
 public void Dispose(){if(device!=IntPtr.Zero&&device!=new IntPtr(-1))CloseHandle(device);if(gate!=IntPtr.Zero)CloseHandle(gate);device=gate=IntPtr.Zero;}
}
'@
  $reader=[WujieAgeReadOnly]::new()
  try {$first=$reader.Sample();Start-Sleep -Milliseconds 300;$second=$reader.Sample()} finally {$reader.Dispose()}
  @{success=$true;utc=[DateTime]::UtcNow.ToString('o');board=$board;read_only=$true;ec_write_calls=0;first=$first;second=$second} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $out -Encoding utf8
} catch {
  @{success=$false;utc=[DateTime]::UtcNow.ToString('o');read_only=$true;ec_write_calls=0;error=$_.ToString()} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $out -Encoding utf8
}
