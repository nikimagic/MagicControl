param([ValidateRange(30,900)][int]$DurationSeconds=90,[ValidateSet('initial','observe','levels')][string]$RunName='initial')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$outDir=Join-Path $PSScriptRoot $RunName
New-Item -ItemType Directory -Path $outDir -Force|Out-Null
$isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
@{started_utc=[DateTime]::UtcNow.ToString('o');elevated=$isAdmin;pid=$PID;ec_write_calls=0;mode='read IOCTL only'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $outDir 'started.json') -Encoding utf8
if(!$isAdmin){throw 'Elevated token required'}
try {
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class WujieIdleReadOnly : IDisposable {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string path,uint access,uint share,IntPtr sa,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateMutexExW(IntPtr sa,string name,uint flags,uint access);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(IntPtr h,uint code,byte[] input,uint inputLength,byte[] output,uint outputLength,out uint returned,IntPtr overlapped);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint ms);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReleaseMutex(IntPtr h);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 [StructLayout(LayoutKind.Sequential)] struct PowerStatus {public byte ac,flags,percent,saver;public uint remaining,full;}
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetSystemPowerStatus(out PowerStatus status);
 IntPtr device,mutex;
 // No write IOCTL or write method. Address whitelist is immutable.
 static readonly ushort[] addresses={0x740,0x741,0x766,0x78c,0x767,0x7c5,0x7c3,0x770,0x6e6,0x8a5,0x8a6,0x86f,0x852,0x7cc,0x73c,0x490,0x825,0x826,0x827};
 public static System.Collections.Generic.Dictionary<string,int> Power(){PowerStatus s;if(!GetSystemPowerStatus(out s))throw new Win32Exception(Marshal.GetLastWin32Error());return new System.Collections.Generic.Dictionary<string,int>{{"ac_line_status",s.ac},{"battery_percent",s.percent},{"battery_flags",s.flags},{"system_saver",s.saver}};}
 public WujieIdleReadOnly(){
  device=CreateFileW(@"\\.\ACPIDriver",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
  if(device==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
  mutex=CreateMutexExW(IntPtr.Zero,@"Global\MagicControl.EcTransactions.v2",0,0x100001);
  if(mutex==IntPtr.Zero){int e=Marshal.GetLastWin32Error();CloseHandle(device);device=IntPtr.Zero;throw new Win32Exception(e);}
 }
 public System.Collections.Generic.Dictionary<string,int> Sample(){
  uint wait=WaitForSingleObject(mutex,5000);if(wait!=0&&wait!=0x80)throw new Exception("EC mutex wait="+wait);
  try{
   var result=new System.Collections.Generic.Dictionary<string,int>();
   foreach(ushort a in addresses){byte[] packet=new byte[8];Array.Copy(BitConverter.GetBytes((uint)a),packet,4);byte[] output=new byte[16];uint n;
    if(!DeviceIoControl(device,0x9c40a488,packet,8,output,16,out n,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error());
    if(n==0||n>16)throw new Exception("Invalid read length="+n);result.Add("0x"+a.ToString("X3"),output[0]);
   }
   return result;
  }finally{ReleaseMutex(mutex);}
 }
 public void Dispose(){if(device!=IntPtr.Zero&&device!=new IntPtr(-1))CloseHandle(device);if(mutex!=IntPtr.Zero)CloseHandle(mutex);device=mutex=IntPtr.Zero;}
}
'@
$reader=[WujieIdleReadOnly]::new()
$writer=[IO.StreamWriter]::new((Join-Path $outDir 'ec-idle.jsonl'),$false,[Text.UTF8Encoding]::new($false))
try {
 $sw=[Diagnostics.Stopwatch]::StartNew()
 while($sw.Elapsed.TotalSeconds -lt $DurationSeconds){
  if(Test-Path -LiteralPath (Join-Path $outDir 'stop.request')){break}
  $raw=$reader.Sample()
  $row=@{utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$sw.ElapsedMilliseconds;raw=$raw;counter=($raw['0x8A5']*256+$raw['0x8A6']);app_presence=($raw['0x741']-band 1)-ne 0;idle_blank=($raw['0x741']-band 4)-ne 0;host_power_disabled=($raw['0x78C']-band 2)-ne 0;selected_level=($raw['0x78C']-shr 5)-band 7;ec_write_calls=0}
  $row.windows_power=[WujieIdleReadOnly]::Power()
  $writer.WriteLine(($row|ConvertTo-Json -Compress -Depth 4));$writer.Flush()
  Start-Sleep -Milliseconds 500
 }
} finally {$writer.Dispose();$reader.Dispose()}
Get-Process -ErrorAction SilentlyContinue|Where-Object ProcessName -Match 'MagicControl|GCUService|GCUBridge|AiStone|ControlCenter'|Select-Object Id,ProcessName,Path|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $outDir 'processes.json') -Encoding utf8
try {Get-WinEvent -FilterHashtable @{LogName='System';ProviderName='Microsoft-Windows-Power-Troubleshooter';Id=1;StartTime=(Get-Date).AddDays(-1)} -MaxEvents 3|Select-Object TimeCreated,Id,Message|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $outDir 'wake-events.json') -Encoding utf8}catch{$_|Out-String|Set-Content -LiteralPath (Join-Path $outDir 'wake-events-error.txt') -Encoding utf8}
@{finished_utc=[DateTime]::UtcNow.ToString('o');success=$true;elevated=$isAdmin;ec_write_calls=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $outDir 'result.json') -Encoding utf8
} catch {@{finished_utc=[DateTime]::UtcNow.ToString('o');success=$false;error=$_.ToString();ec_write_calls=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $outDir 'result.json') -Encoding utf8;throw}
