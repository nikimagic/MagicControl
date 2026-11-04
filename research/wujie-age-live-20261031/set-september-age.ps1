# Explicit owner-requested experiment on the exact Wujie X5SP4NAG.
# Host writes ONLY the two alternative-date bytes 7F2/7F3, using ECRW.
# The EC can subsequently persist its age/service record through its own bus.
# Backup below is an EC RAM snapshot, not an independent device-50 dump.
# No manufacture-date, cycle/stress, APP/FAN, platform/ROM, power or flash writes.
param([ValidateSet('Apply','Restore')][string]$Action='Apply',[string]$BaselinePath='')
$ErrorActionPreference='Stop'
$folder=Join-Path $PSScriptRoot ('september-age-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
[void](New-Item -ItemType Directory -Path $folder)
$out=Join-Path $folder 'result.json'
$journal=Join-Path $folder 'journal.jsonl'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$result=[ordered]@{success=$false;action=$Action;started_utc=[DateTime]::UtcNow.ToString('o');
 requested_date='2026-09-01';requested_word='0x5D21';host_writable_addresses=@('0x7F2','0x7F3');
 backup_scope='EC RAM service-record sources; not an independent SMBus device-50 dump';
 independent_device50_readback=$false;manufacture_date_written=$false;fan_or_power_write_calls=0}
$probe=$null;$attempted=$false;$baseline=$null
function Save-State {
 $result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $out -Encoding utf8
}
function Log-Event([string]$phase,$data) {
 @{utc=[DateTime]::UtcNow.ToString('o');phase=$phase;data=$data} | ConvertTo-Json -Depth 7 -Compress |
   Add-Content -LiteralPath $journal -Encoding utf8
}
try {
 $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 if(!$admin){throw 'Administrator token required'}
 $result.board=(Get-CimInstance Win32_BaseBoard).Product
 if($result.board -ne 'WUJIE Series-X5SP4NAG'){throw 'SMBIOS board mismatch'}
 $result.driver_sha256=(Get-FileHash -LiteralPath "$env:WINDIR\System32\drivers\UWACPIDriver.sys" -Algorithm SHA256).Hash
 if($result.driver_sha256 -ne '6E360EE4A0C42B2A4EAF46208C1592C87F8CD71ACA2A6C52831A2AD356E61235'){throw 'Unreviewed driver'}
 $image=Join-Path $root 'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin'
 $result.saved_firmware_sha256=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
 if($result.saved_firmware_sha256 -ne '5D4EC03A4EC9B36938D907FD6FCB51109B445B4A43C5498F018D022D4E6AC7A8'){throw 'Saved firmware mismatch'}
 $rom=[IO.File]::ReadAllBytes($image)
 foreach($entry in @(@(0xE7FA,[byte[]](0x90,3,0x6D,0xE0,0x64,0x59,0x70,2,0xFF,0x22)),
                     @(0xE804,[byte[]](0x90,7,0xF2,0xE0,0xFE,0xA3,0xE0,0xFF)),
                     @(0xE992,[byte[]](0x6F,0x60,0x0B,0x12,0xE3,0xB4)))) {
  for($i=0;$i -lt $entry[1].Length;$i++){if($rom[$entry[0]+0x8000+$i] -ne $entry[1][$i]){throw 'Age branch opcode mismatch'}}
 }
 Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public sealed class WujieSeptemberAge:IDisposable {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateMutexExW(IntPtr p,string n,uint f,uint a);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(IntPtr h,uint code,byte[] input,uint inputSize,byte[] output,uint outputSize,out uint returned,IntPtr p);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint timeout);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReleaseMutex(IntPtr h);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 IntPtr device,gate; readonly string writeJournal;
 public int HostWriteAttempts,SelectorWrites;
 public readonly List<string> Writes=new List<string>();
 static readonly ushort[] fields={0x454,0x456,0x740,0x314,0x315,0x36c,0x36d,0x39d,0x39f,
 0x3a1,0x3a3,0x3c4,0x490,0x491,0x497,0x4a6,0x4a7,0x726,0x72d,0x72e,0x741,
 0x751,0x7a6,0x7c3,0x770,0x7cc,0x7f2,0x7f3,0x9c7,0x9c8,0x9c9,0x9ca,
 0x30e,0x30f,0x522,0x523,0x402,0x403,0x404,0x405,0x432,0x433,0x434,0x435,0x438,0x439,
 0x350,0x351,0x352,0x353,0x354,0x355,0x356,0x357,0x358,0x359,0x35a,0x35b,0x35c,0x35d,0x35e,0x35f,
 0x3b5,0x342,0x54c,0x561,0x680,0xa4c,0xa4d,0xa4e,0xa4f,0xa54,0xa56,
 0x1c00,0x1c01,0x1c02,0x1c03,0x1c04,0x1c05,0x1c06,0x1c07};
 public WujieSeptemberAge(string journal){
  writeJournal=journal;
  device=CreateFileW(@"\\.\ACPIDriver",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
  if(device==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
  gate=CreateMutexExW(IntPtr.Zero,@"Global\MagicControl.EcTransactions.v2",0,0x100001);
  if(gate==IntPtr.Zero){int e=Marshal.GetLastWin32Error();CloseHandle(device);device=IntPtr.Zero;throw new Win32Exception(e);}
 }
 void Enter(){uint w=WaitForSingleObject(gate,5000);if(w!=0&&w!=0x80)throw new Exception("EC mutex unavailable");}
 byte Invoke(uint code,uint first,uint second){
  byte[] input=new byte[8],output=new byte[16];uint n;
  Array.Copy(BitConverter.GetBytes(first),0,input,0,4);Array.Copy(BitConverter.GetBytes(second),0,input,4,4);
  if(code==0x9c40a4c8)SelectorWrites++;
  if(!DeviceIoControl(device,code,input,8,output,16,out n,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error());
  if(code!=0x9c40a48c&&(n==0||n>16))throw new Exception("Invalid IOCTL reply");
  return output[0];
 }
 byte Read(ushort address){Invoke(0x9c40a4c8,0x681,(uint)(address>>8));return Invoke(0x9c40a4c8,0x682,(uint)(address&255));}
 void Identity(){
  ushort[] ids={0x454,0x456,0x740};byte[] expected={82,72,26};
  for(int i=0;i<ids.Length;i++)if(Invoke(0x9c40a488,ids[i],0)!=expected[i]||Read(ids[i])!=expected[i])throw new Exception("Fresh identity mismatch");
  if(Read(0x2014)!=6||Read(0x2015)!=0x80||(Read(0x2012)&1)!=1||(Read(0x200d)&3)!=3)throw new Exception("I2EC config mismatch");
 }
 void Record(string message){
  string text=DateTime.UtcNow.ToString("o")+" "+message;Writes.Add(text);
  byte[] bytes=System.Text.Encoding.UTF8.GetBytes(text+Environment.NewLine);
  using(var f=new FileStream(writeJournal,FileMode.Append,FileAccess.Write,FileShare.Read)){f.Write(bytes,0,bytes.Length);f.Flush(true);}
 }
 void Write(ushort address,byte value){
  if(address!=0x7f2&&address!=0x7f3)throw new Exception("Write outside date allowlist");
  Record("ATTEMPT ECRW "+address.ToString("X4")+"="+value.ToString("X2"));HostWriteAttempts++;
  Invoke(0x9c40a48c,address,value);
  byte actual=Read(address);Record("READBACK "+address.ToString("X4")+"="+actual.ToString("X2"));
  if(actual!=value)throw new Exception("Date byte readback mismatch at "+address.ToString("X4"));
 }
 public Dictionary<string,int> Snapshot(){Enter();try{
  Identity();var r=new Dictionary<string,int>();foreach(ushort address in fields)r.Add("0x"+address.ToString("X3"),Read(address));return r;
 }finally{ReleaseMutex(gate);}}
 public void Apply(){Enter();try{
  Identity();
  if(Read(0x7f2)!=0||Read(0x7f3)!=0||Read(0x36d)!=0||(Read(0x726)&7)!=0||Read(0x3a1)!=0)throw new Exception("Date/service state changed before apply");
  if(Read(0x314)!=0x5a||Read(0x315)!=0x52||Read(0x72d)!=0x26||Read(0x72e)!=0x10)throw new Exception("Battery/clock changed");
  // Low first: intermediate0021 falls back to the existing cached age.
  // High first would temporarily create a valid two-month date5D00.
  Write(0x7f3,0x21);Write(0x7f2,0x5d);
  if(Read(0x7f2)!=0x5d||Read(0x7f3)!=0x21)throw new Exception("Full date readback mismatch");
 }finally{ReleaseMutex(gate);}}
 public void RestoreZeroAlternative(){Enter();try{
  Identity();int current=(Read(0x7f2)<<8)|Read(0x7f3);
  if(current!=0&&current!=0x21&&current!=0x5d21)throw new Exception("Unexpected date; refuse to overwrite foreign value");
  if(Read(0x314)!=0x5a||Read(0x315)!=0x52)throw new Exception("Battery changed before restore");
  Write(0x7f2,0);Write(0x7f3,0);
  if(Read(0x7f2)!=0||Read(0x7f3)!=0)throw new Exception("Restore readback mismatch");
 }finally{ReleaseMutex(gate);}}
 public void Dispose(){if(device!=IntPtr.Zero&&device!=new IntPtr(-1))CloseHandle(device);if(gate!=IntPtr.Zero)CloseHandle(gate);device=gate=IntPtr.Zero;}
}
'@
 $probe=[WujieSeptemberAge]::new((Join-Path $folder 'writes.txt'))
 $first=$probe.Snapshot();Start-Sleep -Milliseconds 200;$second=$probe.Snapshot()
 $baseline=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');board=$result.board;
  driver_sha256=$result.driver_sha256;saved_firmware_sha256=$result.saved_firmware_sha256;
  scope=$result.backup_scope;first=$first;second=$second}
 $baselineFile=Join-Path $folder 'before.json'
 $baseline | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $baselineFile -Encoding utf8
 $result.baseline_file=$baselineFile;$result.baseline_sha256=(Get-FileHash -LiteralPath $baselineFile -Algorithm SHA256).Hash
 $result.before=$second;Save-State
 foreach($a in @(0x314,0x315,0x36c,0x36d,0x39f,0x3a1,0x726,0x72d,0x72e,0x7f2,0x7f3,
    0x350,0x351,0x352,0x353,0x354,0x355,0x356,0x357,0x358,0x359,0x35a,0x35b,0x35c,0x35d,0x35e,0x35f,0x3b5,0x9c9,0x9ca)){
  $k='0x{0:X3}' -f $a;if($first[$k] -ne $second[$k]){throw "Unstable baseline $k"}
 }
 if($second['0x314'] -ne 0x5a -or $second['0x315'] -ne 0x52 -or $second['0x36D'] -ne 0){throw 'Unexpected battery/service override'}
 if($Action -eq 'Apply'){
  if($second['0x7F2'] -ne 0 -or $second['0x7F3'] -ne 0 -or $second['0x3A1'] -ne 0){throw 'Apply requires original zero alternative date and idle save queue'}
  $attempted=$true;Log-Event 'apply_requested' @{date='2026-09-01';word='5D21'};$probe.Apply()
 }else{
  if([string]::IsNullOrWhiteSpace($BaselinePath)){throw 'Restore requires original before.json'}
  $original=Get-Content -LiteralPath $BaselinePath -Raw|ConvertFrom-Json
  if($original.board -ne $result.board -or $original.driver_sha256 -ne $result.driver_sha256 -or
     $original.second.'0x7F2' -ne 0 -or $original.second.'0x7F3' -ne 0 -or
     $original.second.'0x314' -ne 0x5a -or $original.second.'0x315' -ne 0x52){throw 'Original baseline mismatch'}
  $probe.RestoreZeroAlternative()
 }
 $result.pair_readback_confirmed=$true;Save-State
 $expectedAge=if($Action -eq 'Apply'){1}else{20}
 $deadline=[DateTime]::UtcNow.AddSeconds(45);$confirmed=$false;$last=$null;$settled=0
 do {
  $sample=$probe.Snapshot();$result.after=$sample
  if($sample['0x314'] -ne 0x5a -or $sample['0x315'] -ne 0x52){throw 'Manufacture date changed unexpectedly'}
  $native=($sample['0x36C'] -eq $expectedAge -and $sample['0x39F'] -eq $expectedAge)
  if($native){$confirmed=$true}
  if($native -and ($sample['0x3A1'] -band 8) -eq 0){$settled++}else{$settled=0}
  $state=($sample|ConvertTo-Json -Compress)
  if($state -ne $last){Log-Event 'observe' $sample;$last=$state;Save-State}
  if($settled -ge 3){break}
  Start-Sleep -Milliseconds 300
 }while([DateTime]::UtcNow -lt $deadline)
 $result.native_age_confirmed=$confirmed;$result.expected_age_months=$expectedAge
 $result.native_save_queue_idle=(($result.after['0x3A1'] -band 8) -eq 0)
 $result.status=if($confirmed){'firmware_age_recalculated'}else{'date_written_waiting_for_firmware_recalculation'}
 $result.success=$true
}catch {
 $result.error=$_.ToString();Log-Event 'error' $result.error
 if($attempted -and $null -ne $probe -and $probe.HostWriteAttempts -gt 0){
  try {$probe.RestoreZeroAlternative();$result.automatic_rollback='original zero alternative date read back';$result.after=$probe.Snapshot()}
  catch {$result.automatic_rollback_error=$_.ToString();Log-Event 'rollback_error' $result.automatic_rollback_error}
 }
}finally {
 if($null -ne $probe){$result.host_date_write_attempts=$probe.HostWriteAttempts;$result.host_selector_writes=$probe.SelectorWrites;$result.writes=$probe.Writes;$probe.Dispose()}
 $result.completed_utc=[DateTime]::UtcNow.ToString('o');Save-State
}
