# Owner-authorized exact-board experiment, NOT an application feature.
# Data writers are restricted to 9C9/9CA (history), 7A6.mask30 (Full policy),
# and 726.bit0 (native service-record save request). Every other bit is kept.
# RIOP(683,value) writes the known I2EC data port and reads684 (discarded).
# No WIOP, PawnIO port enumeration, SMBus-controller, date/cycle/FAN/PL writes.
param([ValidateSet('Reset','Restore')][string]$Action='Reset',[string]$BaselinePath='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runName='history-'+$Action.ToLower()+'-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$folder=Join-Path $PSScriptRoot $runName
$portable=Join-Path $root ('MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/battery-age-tests/'+$runName)
[void](New-Item -ItemType Directory -Path $folder)
[void](New-Item -ItemType Directory -Path $portable)
$out=Join-Path $folder 'result.json';$events=Join-Path $folder 'samples.jsonl'
$result=[ordered]@{success=$false;action=$Action;started_utc=[DateTime]::UtcNow.ToString('o');
 writable_history_addresses=@('0x9C9','0x9CA');writable_other_masks=@('0x7A6.mask30','0x726.bit0');
 direct_smbus_controller_write_calls=0;independent_device50_readback=$false;
 evidence_folder=$folder;portable_evidence_folder=$portable}
$reader=$null;$writer=$null;$baseline=$null;$armed=$false
function Save-State {$result|ConvertTo-Json -Depth 9|Set-Content -LiteralPath $out -Encoding utf8}
function Get-ExposureCount($s){return (256*[int]$s['0x9C9']+[int]$s['0x9CA'])}
function Write-ExposureSample([string]$phase,$s){
 @{utc=[DateTime]::UtcNow.ToString('o');phase=$phase;history=(Get-ExposureCount $s);
   target_mv=([int]$s['0x522']+256*[int]$s['0x523']);policy=([int]$s['0x7A6'] -band 0x30);
   raw=$s}|ConvertTo-Json -Compress -Depth 5|Add-Content -LiteralPath $events -Encoding utf8
}
try {
 $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 if(!$admin){throw 'Administrator token required'}
 $result.board=(Get-CimInstance Win32_BaseBoard).Product
 if($result.board -ne 'WUJIE Series-X5SP4NAG'){throw 'SMBIOS mismatch'}
 $result.driver_sha256=(Get-FileHash -LiteralPath "$env:WINDIR\System32\drivers\UWACPIDriver.sys").Hash
 if($result.driver_sha256 -ne '6E360EE4A0C42B2A4EAF46208C1592C87F8CD71ACA2A6C52831A2AD356E61235'){throw 'Unreviewed driver'}
 $image=Join-Path $root 'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin'
 $result.saved_firmware_sha256=(Get-FileHash -LiteralPath $image).Hash
 if($result.saved_firmware_sha256 -ne '5D4EC03A4EC9B36938D907FD6FCB51109B445B4A43C5498F018D022D4E6AC7A8'){throw 'Unreviewed saved firmware'}
 $rom=[IO.File]::ReadAllBytes($image)
 foreach($e in @(@(0xE7C6,[byte[]](0x90,7,0x26,0xE0,0x54,1,0x60,5,0xE0,0x54,0xFE,0x80,0x23)),
                 @(0xE7F6,[byte[]](0xF0,0x12,0xE3,0xB4)))){
  for($i=0;$i -lt $e[1].Length;$i++){if($rom[$e[0]+0x8000+$i] -ne $e[1][$i]){throw 'Native save opcode mismatch'}}
 }
 foreach($e in @(@(16,0x09CA,0x64),@(17,0x09C9,0x65))){
  $at=0x593f+3*$e[0]
  if((256*$rom[$at]+$rom[$at+1]) -ne $e[1] -or $rom[$at+2] -ne $e[2]){throw 'History save-table mismatch'}
 }
 # Reuse only the reviewed Snapshot class, never execute the date script.
 $helper=Join-Path $PSScriptRoot 'set-september-age.ps1'
 if((Get-FileHash -LiteralPath $helper).Hash -ne '4BB1373D4DCEE138AB9A955589E6642DDE87E347754D7989840967907F67D659'){throw 'Snapshot implementation changed'}
 $s=Get-Content -LiteralPath $helper -Raw
 $cs=[regex]::Match($s,"(?s)Add-Type -TypeDefinition @'\r?\n(.*?)\r?\n'@").Groups[1].Value
 if([string]::IsNullOrWhiteSpace($cs)){throw 'Missing snapshot class'}
 Add-Type -TypeDefinition $cs
 Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public sealed class WujieHistoryWriter:IDisposable {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateMutexExW(IntPtr p,string n,uint f,uint a);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(IntPtr h,uint code,byte[] input,uint ins,byte[] output,uint outs,out uint n,IntPtr p);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint t);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReleaseMutex(IntPtr h);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 IntPtr device,gate;readonly string journal;readonly ushort original;readonly byte policy;
 public int HistoryWriteAttempts,PolicyWriteAttempts,SaveRequestAttempts;
 public readonly List<string> Writes=new List<string>();
 public WujieHistoryWriter(string log,ushort history,byte savedPolicy){
  if(history==0||savedPolicy>2)throw new Exception("Invalid rollback values");
  journal=log;original=history;policy=(byte)(savedPolicy<<4);
  device=CreateFileW(@"\\.\ACPIDriver",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
  if(device==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
  gate=CreateMutexExW(IntPtr.Zero,@"Global\MagicControl.EcTransactions.v2",0,0x100001);
  if(gate==IntPtr.Zero){int e=Marshal.GetLastWin32Error();CloseHandle(device);device=IntPtr.Zero;throw new Win32Exception(e);}
 }
 void Enter(){uint w=WaitForSingleObject(gate,5000);if(w!=0&&w!=0x80)throw new Exception("EC mutex unavailable");}
 byte Invoke(uint code,uint first,uint second){
  var input=new byte[8];var output=new byte[16];uint n;
  Array.Copy(BitConverter.GetBytes(first),0,input,0,4);Array.Copy(BitConverter.GetBytes(second),0,input,4,4);
  if(!DeviceIoControl(device,code,input,8,output,16,out n,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error());
  if(code!=0x9c40a48c&&(n==0||n>16))throw new Exception("Invalid IOCTL reply");return output[0];
 }
 byte Read(ushort a){Invoke(0x9c40a4c8,0x681,(uint)(a>>8));return Invoke(0x9c40a4c8,0x682,(uint)(a&255));}
 ushort Word(){return (ushort)((Read(0x9c9)<<8)|Read(0x9ca));}
 void Identity(){
  ushort[] a={0x454,0x456,0x740};byte[] v={82,72,26};
  for(int i=0;i<a.Length;i++)if(Invoke(0x9c40a488,a[i],0)!=v[i]||Read(a[i])!=v[i])throw new Exception("EC identity mismatch");
  if(Read(0x2014)!=6||Read(0x2015)!=0x80||(Read(0x2012)&1)!=1||(Read(0x200d)&3)!=3)throw new Exception("I2EC mapping/permission mismatch");
  if(Read(0x314)!=0x5a||Read(0x315)!=0x52)throw new Exception("Battery identity changed");
 }
 void Log(string m){
  string text=DateTime.UtcNow.ToString("o")+" "+m;Writes.Add(text);
  byte[] b=System.Text.Encoding.UTF8.GetBytes(text+Environment.NewLine);
  using(var f=new FileStream(journal,FileMode.Append,FileAccess.Write,FileShare.Read)){f.Write(b,0,b.Length);f.Flush(true);}
 }
 void WriteHistoryByte(ushort address,byte value){
  if(address!=0x9c9&&address!=0x9ca)throw new Exception("History address outside allowlist");
  Log("ATTEMPT I2EC "+address.ToString("X4")+"="+value.ToString("X2"));HistoryWriteAttempts++;
  Invoke(0x9c40a4c8,0x681,(uint)(address>>8));Invoke(0x9c40a4c8,0x682,(uint)(address&255));
  // DSDT RIOP writes Arg1 to Arg0, then reads Arg0+1. No extra port write.
  // The return from port684 is logged and discarded; data is read back via683.
  byte unused=Invoke(0x9c40a4c8,0x683,value);
  byte actual=Read(address);Log("READBACK "+address.ToString("X4")+"="+actual.ToString("X2")+"; ignored_port684="+unused.ToString("X2"));
  if(actual!=value)throw new Exception("I2EC write readback mismatch at "+address.ToString("X4"));
 }
 void SetPolicy(byte maskValue){
  byte before=Read(0x7a6),after=(byte)((before&0xcf)|maskValue);
  if(before==after)return;
  Log("ATTEMPT ECRW 07A6.mask30="+maskValue.ToString("X2")+"; previous="+before.ToString("X2"));PolicyWriteAttempts++;
  Invoke(0x9c40a48c,0x7a6,after);
  if(Read(0x7a6)!=after)throw new Exception("Policy readback mismatch");
 }
 void SaveRequest(){
  byte before=Read(0x726);if((before&7)!=0)throw new Exception("Foreign native service request active");
  byte after=(byte)(before|1);Log("ATTEMPT ECRW 0726.bit0=1; previous="+before.ToString("X2"));SaveRequestAttempts++;
  Invoke(0x9c40a48c,0x726,after);
  byte actual=Read(0x726);Log("READBACK 0726="+actual.ToString("X2"));
  if((actual&0xfe)!=(before&0xfe))throw new Exception("Unrelated726 bits changed");
 }
 public void Reset(){Enter();try{
  Identity();
  if(Word()!=original||Read(0x3a1)!=0||(Read(0x726)&7)!=0)throw new Exception("History/save state changed before reset");
  if(Read(0x7f2)!=0x5d||Read(0x7f3)!=0x21||Read(0x36c)!=1||Read(0x39d)!=1||Read(0x36d)!=0)throw new Exception("One-month age context changed");
  if((Read(0x490)&1)==0||(Read(0x491)&1)==0)throw new Exception("AC not confirmed");
  SetPolicy(0);
  WriteHistoryByte(0x9c9,0);WriteHistoryByte(0x9ca,0);
  if(Word()!=0)throw new Exception("Zero history not confirmed");
  SaveRequest();
 }finally{ReleaseMutex(gate);}}
 public void Restore(){Enter();try{
  Identity();
  // High first restores the original protection tier before the exact low byte.
  WriteHistoryByte(0x9c9,(byte)(original>>8));WriteHistoryByte(0x9ca,(byte)original);
  if(Word()!=original)throw new Exception("Original history not restored");
  SetPolicy(policy);
  // A preceding reset may already have a native sweep in flight. Restore RAM
  // first, then wait for it to finish before requesting a complete fresh sweep.
  var deadline=DateTime.UtcNow.AddSeconds(5);
  while((Read(0x726)&7)!=0||(Read(0x3a1)&8)!=0){
   if(DateTime.UtcNow>=deadline)throw new Exception("RAM restored; native save remains busy");
   System.Threading.Thread.Sleep(20);
  }
  SaveRequest();
 }finally{ReleaseMutex(gate);}}
 public void Dispose(){if(device!=IntPtr.Zero&&device!=new IntPtr(-1))CloseHandle(device);if(gate!=IntPtr.Zero)CloseHandle(gate);device=gate=IntPtr.Zero;}
}
'@
 $reader=[WujieSeptemberAge]::new((Join-Path $folder 'unexpected-snapshot-writes.txt'))
 $first=$reader.Snapshot();Start-Sleep -Milliseconds 200;$second=$reader.Snapshot()
 foreach($k in @('0x9C9','0x9CA','0x314','0x315','0x36C','0x36D','0x39D','0x39F','0x3A1','0x726','0x7A6','0x7F2','0x7F3','0x4A6','0x4A7')){
  if($first[$k] -ne $second[$k]){throw "Unstable baseline $k"}
 }
 $baseline=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');board=$result.board;driver_sha256=$result.driver_sha256;
  scope='EC RAM sources including all23 native save entries; independent device50 contents not read';first=$first;second=$second;
  original_history=(Get-ExposureCount $second);original_policy=(([int]$second['0x7A6'] -band 0x30)-shr 4)}
 $beforeFile=Join-Path $folder 'before.json'
 $baseline|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $beforeFile -Encoding utf8
 Copy-Item -LiteralPath $beforeFile -Destination (Join-Path $portable 'before.json')
 $result.before=$second;$result.baseline_file=$beforeFile;$result.baseline_sha256=(Get-FileHash -LiteralPath $beforeFile).Hash
 if((Get-FileHash -LiteralPath (Join-Path $portable 'before.json')).Hash -ne $result.baseline_sha256){throw 'Portable rollback copy mismatch'}
 $result.history_before=(Get-ExposureCount $second);Save-State
 $original=$baseline
 if($Action -eq 'Restore'){
  if([string]::IsNullOrWhiteSpace($BaselinePath)){throw 'Original before.json required'}
  $original=Get-Content -LiteralPath $BaselinePath -Raw|ConvertFrom-Json
  if($original.board -ne $result.board -or $original.driver_sha256 -ne $result.driver_sha256 -or
    $original.second.'0x314' -ne 0x5a -or $original.second.'0x315' -ne 0x52){throw 'Restore baseline mismatch'}
 }
 $oldHistory=[int]$original.original_history;$oldPolicy=[int]$original.original_policy
 if($oldHistory -le 0 -or $oldHistory -gt 65535 -or $oldPolicy -lt 0 -or $oldPolicy -gt 2){throw 'Invalid original values'}
 $writer=[WujieHistoryWriter]::new((Join-Path $folder 'writes.txt'),[uint16]$oldHistory,[byte]$oldPolicy)
 $armed=$true
 if($Action -eq 'Reset'){$writer.Reset()}else{$writer.Restore()}
 $deadline=[DateTime]::UtcNow.AddSeconds(40);$settled=0;$observedZero=$false;$i=0
 do {
  $sample=$reader.Snapshot();$result.after=$sample;$h=Get-ExposureCount $sample
  if($h -eq 0){$observedZero=$true}
  $policy=[int]$sample['0x7A6'] -band 0x30;$target=[int]$sample['0x522']+256*[int]$sample['0x523']
  $ready=if($Action -eq 'Reset'){
   $observedZero -and $h -le 7 -and $policy -eq 0 -and $target -eq 17800 -and $sample['0x39D'] -eq 1
  }else{$h -eq $oldHistory -and $policy -eq ($oldPolicy-shl 4)}
  if(($sample['0x726']-band 1) -ne 0 -or ($sample['0x3A1']-band 8) -ne 0){$ready=$false}
  if(($sample['0x726']-band 0xF8) -ne ($second['0x726']-band 0xF8) -or
    $sample['0x741'] -ne $second['0x741'] -or $sample['0x751'] -ne $second['0x751'] -or
    $sample['0x7F2'] -ne $second['0x7F2'] -or $sample['0x7F3'] -ne $second['0x7F3'] -or
    $sample['0x4A6'] -ne $second['0x4A6'] -or $sample['0x4A7'] -ne $second['0x4A7']){throw 'Unrelated selected context changed'}
  Write-ExposureSample 'observe' $sample;Save-State
  if($ready){$settled++}else{$settled=0}
  if($settled -ge 5){break}
  Start-Sleep -Milliseconds $(if($i -lt 60){50}else{300});$i++
 }while([DateTime]::UtcNow -lt $deadline)
 if($settled -lt 5){throw 'Native target/save confirmation timed out'}
 $result.history_after=(Get-ExposureCount $result.after);$result.zero_observed=$observedZero
 $result.ec_target_mv=([int]$result.after['0x522']+256*[int]$result.after['0x523'])
 $result.native_save_request_consumed=$true;$result.native_save_queue_idle=$true
 $result.success=$true
}catch {
 $result.error=$_.ToString()
 if($armed -and $null -ne $writer -and ($writer.HistoryWriteAttempts+$writer.PolicyWriteAttempts) -gt 0){
  try {$writer.Restore();$result.rollback='Original history/policy read back; native save requested';$result.after_rollback=$reader.Snapshot()}
  catch {$result.rollback_error=$_.ToString()}
 }
}finally {
 if($null -ne $writer){$result.history_write_attempts=$writer.HistoryWriteAttempts;$result.policy_write_attempts=$writer.PolicyWriteAttempts;$result.save_request_attempts=$writer.SaveRequestAttempts;$result.writes=$writer.Writes;$writer.Dispose()}
 if($null -ne $reader){$result.snapshot_data_write_calls=$reader.HostWriteAttempts;$reader.Dispose()}
 $result.completed_utc=[DateTime]::UtcNow.ToString('o');Save-State
 foreach($name in @('result.json','samples.jsonl','writes.txt')){
  $source=Join-Path $folder $name;if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $portable $name)}
 }
}
