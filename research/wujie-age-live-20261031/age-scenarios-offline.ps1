# Offline calculation from a saved Wujie EC snapshot and the saved EC image.
# No device opens, IOCTL, EC/BMS writes, or calls to the application.
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fw=Join-Path $root 'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin'
$snapshot=Join-Path $PSScriptRoot 'age-shadow-backup.json'
$result=Join-Path $PSScriptRoot 'age-scenarios-offline.json'
$expected='5D4EC03A4EC9B36938D907FD6FCB51109B445B4A43C5498F018D022D4E6AC7A8'
if((Get-FileHash -LiteralPath $fw -Algorithm SHA256).Hash -ne $expected){throw 'EC image hash mismatch'}
$j=Get-Content -LiteralPath $snapshot -Raw | ConvertFrom-Json
if(!$j.success -or !$j.read_only -or $j.ec_write_calls -ne 0){throw 'Snapshot is not a successful read-only capture'}
$a=$j.first;$b=$j.second
foreach($key in @('0x454','0x456','0x740','0x490','0x491','0x741','0x7A6','0x72D','0x72E','0x7F2','0x7F3','0x36C','0x36D','0x3A1','0x9C9','0x9CA','0x30E','0x30F','0x4A6','0x4A7')){
    if($null -eq $a.$key -or $a.$key -ne $b.$key){throw "Missing or unstable snapshot value $key"}
}
if($b.'0x454' -ne 82 -or $b.'0x456' -ne 72 -or $b.'0x740' -ne 26){throw 'Wrong EC identity'}
if(($b.'0x490' -band 7) -ne 7){throw 'AC or ready battery is not confirmed in saved snapshot'}
if($b.'0x3A1' -ne 0){throw 'Firmware queue not idle in saved snapshot'}
if($b.'0x36D' -eq 0x59){throw 'The special age-zero override is already active'}
$binary=[IO.File]::ReadAllBytes($fw);$fields=@()
for($i=0;$i -lt 23;$i++){
    $pos=0x593F+3*$i;$address=([int]$binary[$pos]*256)+$binary[$pos+1];$command=$binary[$pos+2]
    $key='0x{0:X3}' -f $address
    if($null -eq $a.$key -or $a.$key -ne $b.$key){throw "RAM field missing or unstable: $key"}
    $fields+=@{ec_address=$key;device_command=('0x{0:X2}' -f $command);ram_value=[int]$b.$key}
}
$bcd={param($v) if(($v -band 15) -gt 9 -or ($v -shr 4) -gt 9){throw 'Invalid EC BCD clock'};return (($v -shr 4)*10+($v -band 15))}
$year=2000+(& $bcd ([int]$b.'0x72D'));$month=& $bcd ([int]$b.'0x72E')
$clock=[datetime]::new($year,$month,1)
$history=([int]$b.'0x9C9'*256)+[int]$b.'0x9CA'
$cycles=([int]$b.'0x4A7'*256)+[int]$b.'0x4A6'
$base=([int]$b.'0x30E'*256)+[int]$b.'0x30F'
$cellMode=([int]$b.'0x491' -band 0xC0)
$cells=if($cellMode -eq 0xC0){4}elseif($cellMode -eq 0x80){3}else{2}
$policy=[int]$b.'0x7A6' -band 0x30
$scenarios=foreach($age in 1,2,3,6,12,24){
    $date=$clock.AddMonths(-$age)
    $packed=(($date.Year-1980) -shl 9) -bor ($date.Month -shl 5) -bor $date.Day
    $drop=if($history -gt 15840 -or $cycles -ge 550 -or $age -ge 25){250}
      elseif($history -gt 11520 -or $cycles -ge 450 -or $age -ge 19 -or $policy -eq 0x20){200}
      elseif($history -gt 8640 -or $cycles -ge 350 -or $age -ge 13){150}
      elseif($history -gt 6480 -or $cycles -ge 250 -or $age -ge 10 -or $policy -eq 0x10){100}
      elseif($history -gt 4320 -or $cycles -ge 150 -or $age -ge 7){50}else{0}
    @{months=$age;candidate_date=$date.ToString('yyyy-MM-dd');hypothetical_7f2_7f3=('0x{0:X4}' -f $packed);
      conditional_drop_mV_per_cell=$drop;conditional_ec_target_mV=($base-$cells*$drop)}
}
@{mode='offline algebraic prediction, not firmware emulation or hardware test';ec_write_calls=0;
  source_snapshot_sha256=(Get-FileHash -LiteralPath $snapshot -Algorithm SHA256).Hash;firmware_sha256=$expected;
  limitation='23 bytes are EC RAM, not an independent backup of physical device 0x50; no restoration or writes authorized by this file';
  snapshot_utc=$j.utc;clock_year=$year;clock_month=$month;history_raw=$history;cycles=$cycles;cells=$cells;policy_mask=$policy;
  base_mV=$base;observed_target_mV=([int]$b.'0x522'+256*[int]$b.'0x523');ram_shadow=$fields;scenarios=@($scenarios)} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $result -Encoding utf8
Get-Content -LiteralPath $result -Raw
