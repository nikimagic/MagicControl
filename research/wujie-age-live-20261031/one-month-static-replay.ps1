# Offline branch replay against a saved Wujie RAM snapshot and firmware bytes.
# No hardware access, EC write, SMBus transaction, or firmware execution.
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$image=Join-Path $root 'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin'
$snapshot=Join-Path $PSScriptRoot 'age-shadow-backup.json'
$output=Join-Path $PSScriptRoot 'one-month-static-replay.json'
$hash='5D4EC03A4EC9B36938D907FD6FCB51109B445B4A43C5498F018D022D4E6AC7A8'
if((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ne $hash){throw 'Wrong saved firmware'}
$rom=[IO.File]::ReadAllBytes($image)
# Bank 1 logical 8000..FFFF occupies file offsets 10000..17FFF.
function ExpectBytes([int]$address,[byte[]]$wanted){
    $offset=$address+0x8000
    for($i=0;$i -lt $wanted.Length;$i++){if($rom[$offset+$i] -ne $wanted[$i]){throw ('Instruction bytes changed at {0:X4}' -f ($address+$i))}}
}
ExpectBytes 0xE7FA ([byte[]](0x90,0x03,0x6d,0xe0,0x64,0x59,0x70,0x02,0xff,0x22))
ExpectBytes 0xE855 ([byte[]](0x90,0x07,0xf2,0x12,0xec,0xcc))
ExpectBytes 0xE985 ([byte[]](0x90,0x03,0x6c,0xf0))
ExpectBytes 0xE992 ([byte[]](0x6f,0x60,0x0b,0x12,0xe3,0xb4))
$j=Get-Content -LiteralPath $snapshot -Raw|ConvertFrom-Json
if(!$j.success -or !$j.read_only -or $j.ec_write_calls -ne 0){throw 'Snapshot is not a successful read-only capture'}
$a=$j.first;$b=$j.second
foreach($key in @('0x454','0x456','0x740','0x490','0x726','0x72D','0x72E','0x7F2','0x7F3','0x314','0x315','0x36C','0x36D','0x39F','0x3A1')){
    if($null -eq $a.$key -or $a.$key -ne $b.$key){throw "Missing or unstable snapshot field $key"}
}
if($b.'0x454' -ne 82 -or $b.'0x456' -ne 72 -or $b.'0x740' -ne 26){throw 'Wrong board identity'}
if(($b.'0x726' -band 7) -ne 0 -or $b.'0x36D' -eq 0x59){throw 'Service flags require separate analysis'}
if($b.'0x7F2' -ne 0 -or $b.'0x7F3' -ne 0 -or $b.'0x3A1' -ne 0){throw 'This model requires the captured zero override and idle queue'}
function Bcd([int]$v){if(($v -band 15) -gt 9 -or ($v -shr 4) -gt 9){throw 'Invalid EC BCD clock'};return (($v -shr 4)*10+($v -band 15))}
$now=[datetime]::new((2000+(Bcd $b.'0x72D')),(Bcd $b.'0x72E'),1)
$date=$now.AddMonths(-1)
$packed=(($date.Year-1980) -shl 9) -bor ($date.Month -shl 5) -bor $date.Day
function Replay([string]$name,[int]$word){
    $altMonth=($word -shr 5) -band 15
    # The 32-bit compare at EB7D/7172 checks 0xEF9F minus the packed word.
    # E817 also checks the lower bound; E82F/E846 check month 1..12.
    $selectAlt=($word -ge 13 -and $word -le 0xEF9F -and $altMonth -ge 1 -and $altMonth -le 12)
    $used=if($selectAlt){$word}else{(([int]$b.'0x314' -shl 8) -bor [int]$b.'0x315')}
    $year=1980+($used -shr 9);$month=($used -shr 5) -band 15;$day=$used -band 31
    # E886 checks the date year; E8DC falls back to the stored 36C age.
    $valid=($month -ge 1 -and $month -le 12 -and $year -ge 2000)
    $age=if($valid){($now.Year-$year)*12+$now.Month-$month}else{[int]$b.'0x36C'}
    if($age -lt 0){$age=0}
    $branch=if(!$valid){'cached-age-fallback'}elseif($selectAlt){'alternative-date'}else{'manufacture-date'}
    @{case=$name;word=('0x{0:X4}' -f $word);selected_word=('0x{0:X4}' -f $used);decoded_year=$year;decoded_month=$month;decoded_day=$day;
      branch=$branch;predicted_months=$age;
      would_request_age_save=($valid -and $age -ne [int]$b.'0x39F')}
}
$before=(([int]$b.'0x7F2' -shl 8) -bor [int]$b.'0x7F3')
$highFirst=(($packed -band 0xFF00) -bor ($before -band 0xFF))
$lowFirst=(($before -band 0xFF00) -bor ($packed -band 0xFF))
$rows=@((Replay 'baseline' $before),(Replay 'hypothetical_high_byte_first' $highFirst),
        (Replay 'hypothetical_low_byte_first' $lowFirst),(Replay 'candidate_one_month' $packed))
if(($rows | Where-Object {$_.case -eq 'candidate_one_month'}).predicted_months -ne 1){throw 'One-month replay did not produce one month'}
@{mode='static branch replay, not firmware emulation or device observation';ec_write_calls=0;
  warning='Intermediate states are risks, not instructions to write. EC scheduling, live persistence and rollback are unverified.';
  snapshot_utc=$j.utc;firmware_sha256=$hash;ec_clock_month=$now.ToString('yyyy-MM');
  candidate_date=$date.ToString('yyyy-MM-dd');candidate_word=('0x{0:X4}' -f $packed);
  candidate_high=('0x{0:X2}' -f ($packed -shr 8));candidate_low=('0x{0:X2}' -f ($packed -band 255));cases=$rows} |
  ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $output -Encoding utf8
Get-Content -LiteralPath $output -Raw
