[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$exe=Join-Path $projectRoot 'build/firmware-discovery/Release/FirmwareDiscovery.exe'
$iasl=Join-Path $projectRoot 'build/acpica-gm6-healthy/package/usr/bin/iasl'
if(!(Test-Path -LiteralPath $exe)){throw 'Build FirmwareDiscovery first.'}
if(!$OutputDirectory){
 $runRoot=Join-Path $projectRoot 'out/test_util/discovery/runs'
 [IO.Directory]::CreateDirectory($runRoot) | Out-Null
 $OutputDirectory=Join-Path $runRoot ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory; old runs are retained.'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
function Save-Json($Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 45)+"`n",$utf8)}
function Linux-Path([string]$p){if(!$p.StartsWith('C:\',[StringComparison]::OrdinalIgnoreCase)){throw 'This local experiment uses C: only.'};return '/mnt/c/'+$p.Substring(3).Replace('\','/')}
try{
 & $exe --inventory --out $OutputDirectory *> (Join-Path $OutputDirectory 'inventory-process.log')
 if($LASTEXITCODE -ne 0){throw 'Inventory helper failed; raw process output retained.'}
 $inv=Get-Content -LiteralPath (Join-Path $OutputDirectory 'inventory.json') -Raw | ConvertFrom-Json
 $declarations=@();$inou=@();$disassembly=@()
 if(Test-Path -LiteralPath $iasl){
  $linuxIasl=Linux-Path $iasl
  foreach($table in $inv.acpi_tables){
   if($table.signature_requested -notin @('DSDT','SSDT') -or !$table.file){continue}
   $aml=Join-Path $OutputDirectory ('acpi/'+$table.file)
   $prefix=Join-Path ([IO.Path]::GetDirectoryName($aml)) ([IO.Path]::GetFileNameWithoutExtension($aml))
   & wsl.exe -d Ubuntu-26.04 --exec $linuxIasl -p (Linux-Path $prefix) -d (Linux-Path $aml) *> ($prefix+'.iasl.log')
   $code=$LASTEXITCODE;$dsl=[IO.Path]::ChangeExtension($aml,'.dsl')
   $disassembly+=@{aml=$aml;exit_code=$code;dsl_saved=(Test-Path -LiteralPath $dsl)}
   if(!(Test-Path -LiteralPath $dsl)){continue}
   $source=[IO.File]::ReadAllText($dsl)
   foreach($m in [regex]::Matches($source,'(?m)^\s*Method\s*\(\s*([\\\^A-Z0-9_.]+)\s*,\s*([0-7])')){
    $declarations+=@{name=$m.Groups[1].Value;arguments=[int]$m.Groups[2].Value;source=$dsl;confidence='IASL declaration; not executed'}
   }
   # Ignore strings/comments before balancing the Device body. Only a unique
   # INOU device declaration may authorize the fixed read-only SMRW request.
   $clean=[regex]::Replace($source,'(?s)/\*.*?\*/|//[^\r\n]*|"(?:\\.|[^"\\])*"',' ')
   foreach($m in [regex]::Matches($clean,'\bDevice\s*\(\s*INOU\s*\)')){
    $start=$clean.IndexOf('{',$m.Index+$m.Length);if($start -lt 0){continue}
    $level=1;$end=$start+1
    while($end -lt $clean.Length -and $level -gt 0){if($clean[$end] -eq '{'){$level++}elseif($clean[$end] -eq '}'){$level--};$end++}
    if($level -ne 0){continue}
    $body=$clean.Substring($start,$end-$start);$methods=@()
    foreach($mm in [regex]::Matches($body,'\bMethod\s*\(\s*([\\\^A-Z0-9_.]+)\s*,\s*([0-7])')){$methods+=@{name=$mm.Groups[1].Value;arguments=[int]$mm.Groups[2].Value}}
    $inou+=@{source=$dsl;methods=$methods}
   }
  }
 }
 $smrw=($inou.Count -eq 1 -and @($inou[0].methods|Where-Object {$_.name -eq 'SMRW' -and $_.arguments -eq 1}).Count -eq 1)
 Save-Json (Join-Path $OutputDirectory 'acpi-methods.json') @{source='Live OS ACPI tables disassembled offline; AML not executed by IASL';iasl_path=$iasl;iasl_sha256=if(Test-Path $iasl){(Get-FileHash -LiteralPath $iasl).Hash}else{$null};disassembly=$disassembly;all_method_declarations=$declarations;inou_devices=$inou;smrw_declared=$smrw;api_duplicate_instances_complete=$false}
 $wmi=@()
 foreach($name in @('AcpiTest_EventULong','AcpiTest_EventString','AcpiTest_EventPackage','AcpiTest_MULong','HID_EVENT20')){
  try{$c=Get-CimClass -Namespace root/wmi -ClassName $name -ErrorAction Stop;$wmi+=@{class=$name;present=$true;methods=@($c.CimClassMethods|ForEach-Object {$_.Name});invoked=$false}}
  catch{$wmi+=@{class=$name;present=$false;error=$_.Exception.Message;invoked=$false}}
 }
 Save-Json (Join-Path $OutputDirectory 'wmi-metadata.json') $wmi
 $argsRead=@('--read','--out',$OutputDirectory);if($smrw){$argsRead+='--smrw-advertised'}
 & $exe @argsRead *> (Join-Path $OutputDirectory 'read-process.log')
 if($LASTEXITCODE -ne 0){throw 'Read helper failed; partial evidence retained.'}
 $read=Get-Content -LiteralPath (Join-Path $OutputDirectory 'read-results.json') -Raw | ConvertFrom-Json
 $readError=$read.ec_error;if(!$readError){$readError=$read.EC.error}
 $values=@{};foreach($f in $read.EC.candidate_fields){if($f.stable){$values[$f.address]=[int]$f.read1}}
 $flags=@()
 foreach($spec in @(@('0x78E',64,'RAM fan table capability'),@('0x766',4,'EC RGB keyboard capability'),@('0x766',2,'USB PowerShare capability'),@('0x7A4',128,'Liquid cooler capability'),@('0x7C8',32,'Liquid cooler target-table capability'))){
  $a=[string]$spec[0];$mask=[int]$spec[1];$flags+=@{name=$spec[2];address=$a;mask=$mask;available=$values.ContainsKey($a);set=if($values.ContainsKey($a)){($values[$a] -band $mask) -ne 0}else{$null};interpretation='Known Uniwill candidate map; not a self-describing firmware interface or write authorization'}
 }
 $summary=@{utc=[DateTime]::UtcNow.ToString('o');status=if($readError -or !$values.Count){'partial'}else{'completed'};simulation='Model catalogue deliberately not consulted';SMBIOS=$inv.SMBIOS;driver_abi_reviewed=$read.EC.driver_ABI_reviewed;ec_error=$readError;candidate_project_id=$values['0x740'];candidate_platform=$values['0x7C3'];candidate_rom_id=$values['0x770'];method_declarations=$declarations.Count;inou_declarations=$inou.Count;smrw_declared=$smrw;smrw_queried=$read.EC.smrw.queried;capability_candidates=$flags;configuration_writes=0;factory_fan_tables='Not requested: mailbox requires state-changing requests; current RAM captured separately';ranges='Candidate default blocks/SMRW raw replies only. No universal adjustable min/max descriptor inferred';unknown_device_control_authorized=$false}
 Save-Json (Join-Path $OutputDirectory 'summary.json') $summary
 $lines=@('Firmware discovery: simulated unknown laptop','SMBIOS: '+$inv.SMBIOS.SystemManufacturer+' / '+$inv.SMBIOS.SystemProductName,'Board: '+$inv.SMBIOS.BaseBoardProduct,'BIOS: '+$inv.SMBIOS.BIOSVersion,'Model catalogue consulted: false','Installed driver ABI reviewed: '+$summary.driver_abi_reviewed,'ACPI INOU declarations: '+$inou.Count,'SMRW declared/queried: '+$smrw+' / '+$summary.smrw_queried,'Project candidate740: '+$summary.candidate_project_id,'Platform candidate7C3: '+$summary.candidate_platform,'ROM-ID candidate770: '+$summary.candidate_rom_id,'','Capability bits are protocol interpretations, not firmware-described feature names.','No EC configuration writes, fan/profiles, lighting, PWUP, mailbox, UEFI setter or driver installation.','Factory RAM bytes do not prove factory ROM tables. Adjustable ranges remain unknown unless described by the firmware.','The test does not grant write permission to an unknown laptop.','See summary.json, inventory.json, read-results.json, acpi-methods.json, wmi-metadata.json and raw ACPI.')
 [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'REPORT.txt'),$lines,$utf8)
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit-code.txt'),'0',$utf8)
 Write-Output $OutputDirectory
}catch{
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'failure.txt'),$_.Exception.ToString(),$utf8)
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'exit-code.txt'),'1',$utf8)
 throw
}
