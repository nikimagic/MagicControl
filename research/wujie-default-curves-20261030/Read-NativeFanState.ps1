# Passive exact-Wujie register snapshots only. No write IOCTL, mailbox,
# mode switch, driver install, GUI launch or fan-table activation.
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class WujieNativeFanRead : IDisposable {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string path,uint access,uint share,IntPtr sa,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateMutexExW(IntPtr sa,string name,uint flags,uint access);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(IntPtr h,uint code,byte[] input,uint inputLength,byte[] output,uint outputLength,out uint returned,IntPtr overlapped);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint ms);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReleaseMutex(IntPtr h);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 IntPtr device,mutex;
 static readonly ushort[] addresses={0x454,0x456,0x740,0x6e6,0x741,0x7c3,0x770,0x74c,0xd41,0x458,
  0x751,0x726,0x727,0x7c5,0x7c6,0x490,0x49f,0x787,0x8e7,0x8e8,0x43e,0x44f,0x9178,
  0x460,0x461,0x468,0x469,0x670,0x75b,0x75c,0x464,0x465,0x46c,0x46d,0x672,0x673,0x986,0x989,0x914c,
  0x906d,0x906e,0x7a5,0x4a2,0x4a3,0x440,0x913f,0x8eb,0x9168,0x9169,0x910a};
 public WujieNativeFanRead(){
  device=CreateFileW(@"\\.\ACPIDriver",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
  if(device==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
  mutex=CreateMutexExW(IntPtr.Zero,@"Global\MagicControl.EcTransactions.v2",0,0x100001);
  if(mutex==IntPtr.Zero){int e=Marshal.GetLastWin32Error();CloseHandle(device);device=IntPtr.Zero;throw new Win32Exception(e);}
 }
 byte Read(ushort address){
  byte[] input=new byte[8],output=new byte[16];Array.Copy(BitConverter.GetBytes((uint)address),input,4);uint n;
  if(!DeviceIoControl(device,0x9c40a488,input,8,output,16,out n,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error());
  if(n==0||n>16)throw new Exception("Invalid read length="+n);return output[0];
 }
 public Dictionary<string,int> Sample(){
  uint wait=WaitForSingleObject(mutex,5000);if(wait!=0&&wait!=0x80)throw new Exception("EC mutex wait="+wait);
  try{
   if(Read(0x454)!=82||Read(0x456)!=72||Read(0x740)!=26)throw new Exception("Exact Wujie82/72/26 mismatch");
   var result=new Dictionary<string,int>();
   foreach(ushort a in addresses)result.Add("0x"+a.ToString("X4"),Read(a));
   return result;
  }finally{ReleaseMutex(mutex);}
 }
 public void Dispose(){if(device!=IntPtr.Zero&&device!=new IntPtr(-1))CloseHandle(device);if(mutex!=IntPtr.Zero)CloseHandle(mutex);device=mutex=IntPtr.Zero;}
}
'@
$reader=[WujieNativeFanRead]::new()
try {
    $rows=@(for($i=0;$i -lt 4;$i++) {
        [ordered]@{utc=[DateTime]::UtcNow.ToString('o');raw=$reader.Sample();ecWriteCalls=0}
        if($i -lt 3){Start-Sleep -Milliseconds 500}
    })
    $rows | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'live-native-fans.json') -Encoding utf8
    $rows | ConvertTo-Json -Depth 5 -Compress
} finally {$reader.Dispose()}
