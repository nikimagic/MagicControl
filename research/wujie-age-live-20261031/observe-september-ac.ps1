# Read-only follow-up after the owner's AC connection. Reuses the reviewed
# snapshot implementation; does NOT execute Apply/Restore or the parent script.
$ErrorActionPreference='Stop'
$folder=Join-Path $PSScriptRoot ('september-ac-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
[void](New-Item -ItemType Directory -Path $folder)
$out=Join-Path $folder 'result.json';$rows=Join-Path $folder 'samples.jsonl'
$result=[ordered]@{success=$false;started_utc=[DateTime]::UtcNow.ToString('o');
 ec_memory_write_calls=0;smbus_command_calls=0;scope='EC I2EC observation; address selector writes only'}
$reader=$null
try {
 $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 if(!$admin){throw 'Administrator token required'}
 if((Get-CimInstance Win32_BaseBoard).Product -ne 'WUJIE Series-X5SP4NAG'){throw 'Wrong board'}
 if((Get-FileHash -LiteralPath "$env:WINDIR\System32\drivers\UWACPIDriver.sys").Hash -ne '6E360EE4A0C42B2A4EAF46208C1592C87F8CD71ACA2A6C52831A2AD356E61235'){throw 'Unreviewed driver'}
 $helper=Join-Path $PSScriptRoot 'set-september-age.ps1'
 if((Get-FileHash -LiteralPath $helper).Hash -ne '4BB1373D4DCEE138AB9A955589E6642DDE87E347754D7989840967907F67D659'){throw 'Snapshot implementation changed'}
 $text=Get-Content -LiteralPath $helper -Raw
 $cs=[regex]::Match($text,"(?s)Add-Type -TypeDefinition @'\r?\n(.*?)\r?\n'@").Groups[1].Value
 if([string]::IsNullOrWhiteSpace($cs)){throw 'Missing snapshot implementation'}
 Add-Type -TypeDefinition $cs
 $reader=[WujieSeptemberAge]::new((Join-Path $folder 'unexpected-writes.txt'))
 $frames=[Collections.Generic.List[object]]::new()
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 do {
  $s=$reader.Snapshot()
  $v=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');
   date_word=('0x{0:X2}{1:X2}' -f $s['0x7F2'],$s['0x7F3']);
   age_cached=$s['0x36C'];age_applied=$s['0x39D'];age_mirror=$s['0x39F'];
   ac_490_bit0=(($s['0x490'] -band 1) -ne 0);ac_491_bit0=(($s['0x491'] -band 1) -ne 0);
   bms_base_mv=(256*$s['0x30E']+$s['0x30F']);ec_target_mv=($s['0x522']+256*$s['0x523']);
   battery_mv=($s['0x438']+256*$s['0x439']);current_word_ma=($s['0x434']+256*$s['0x435']);
   discharging_bit=(($s['0x432'] -band 1) -ne 0);charging_bit=(($s['0x432'] -band 2) -ne 0);
   history=(256*$s['0x9C9']+$s['0x9CA']);native_save_pending=(($s['0x3A1'] -band 8) -ne 0);
   raw=$s}
  $frames.Add($v);$v|ConvertTo-Json -Depth 5 -Compress|Add-Content -LiteralPath $rows -Encoding utf8
  if($frames.Count -eq 1){$result.first=$v}
  $result.last=$v;$result.sample_count=$frames.Count
  $result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $out -Encoding utf8
  Start-Sleep -Milliseconds 1000
 }while([DateTime]::UtcNow -lt $deadline)
 $result.success=$true
}catch {$result.error=$_.ToString()}
finally {
 if($null -ne $reader){
  $result.ec_memory_write_calls=$reader.HostWriteAttempts
  $result.host_selector_writes=$reader.SelectorWrites;$reader.Dispose()
 }
 $result.completed_utc=[DateTime]::UtcNow.ToString('o')
 $result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $out -Encoding utf8
}
