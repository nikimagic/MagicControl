$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';[Console]::OutputEncoding=[Text.Encoding]::UTF8
$root='C:\Users\aniki\Documents\MagicControl-Diagnostics\deep-0.1.58-20261019'
function Save($name,$value){ConvertTo-Json -InputObject $value -Depth 8|Set-Content -LiteralPath (Join-Path $root $name) -Encoding UTF8}
$queries=@()
foreach($q in @(
 @{Name='bios';NS='root/cimv2';Class='Win32_BIOS';Fields=@('Manufacturer','SMBIOSBIOSVersion','ReleaseDate')},
 @{Name='cpu';NS='root/cimv2';Class='Win32_Processor';Fields=@('Name','NumberOfCores','NumberOfLogicalProcessors','MaxClockSpeed','CurrentClockSpeed','LoadPercentage')},
 @{Name='ram';NS='root/cimv2';Class='Win32_PhysicalMemory';Fields=@('BankLabel','DeviceLocator','Capacity','Manufacturer','PartNumber','SMBIOSMemoryType','ConfiguredClockSpeed')},
 @{Name='battery';NS='root/cimv2';Class='Win32_Battery';Fields=@('Name','BatteryStatus','EstimatedChargeRemaining')},
 @{Name='battery-status';NS='root/wmi';Class='BatteryStatus';Fields=@('InstanceName','PowerOnline','Charging','Discharging','Voltage','ChargeRate','DischargeRate','RemainingCapacity')},
 @{Name='battery-temperature';NS='root/wmi';Class='BatteryTemperature';Fields=@('InstanceName','Temperature')},
 @{Name='thermal-zones';NS='root/wmi';Class='MSAcpi_ThermalZoneTemperature';Fields=@('InstanceName','CurrentTemperature')},
 @{Name='graphics';NS='root/cimv2';Class='Win32_VideoController';Fields=@('Name','PNPDeviceID','DriverVersion','DriverDate','AdapterRAM','CurrentRefreshRate')},
 @{Name='pawnio';NS='root/cimv2';Class='Win32_SystemDriver';Fields=@('Name','State','StartMode','PathName')}
)){
 try{$items=@(Get-CimInstance -Namespace $q.NS -ClassName $q.Class -OperationTimeoutSec 8);if($q.Name -eq 'pawnio'){$items=@($items|Where-Object Name -eq 'PawnIO')};$queries +=[pscustomobject]@{Name=$q.Name;Available=$true;Values=@($items|Select-Object -Property $q.Fields)}}
 catch{$queries +=[pscustomobject]@{Name=$q.Name;Available=$false;Error=$_.Exception.Message}}
}
Save 'windows-sources.json' $queries
$counter=@();foreach($class in 'Win32_PerfFormattedData_Counters_ProcessorInformation','Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine','Win32_PerfFormattedData_GPUPerformanceCounters_GPUAdapterMemory'){
 try{$items=@(Get-CimInstance -Namespace root/cimv2 -ClassName $class -OperationTimeoutSec 10);$counter +=[pscustomobject]@{Class=$class;Count=$items.Count;Rows=@($items|Select-Object Name,PercentProcessorTime,ProcessorFrequency,UtilizationPercentage,DedicatedUsage,SharedUsage)}}catch{$counter +=[pscustomobject]@{Class=$class;Error=$_.Exception.Message}}
};Save 'windows-performance.json' $counter
try{$disks=@(Get-PhysicalDisk);$reliable=@();foreach($disk in $disks){try{$reliable +=$disk|Get-StorageReliabilityCounter -ErrorAction Stop|Select-Object DeviceId,Temperature,TemperatureMax,Wear,PowerOnHours,ReadErrorsTotal,WriteErrorsTotal}catch{$reliable +=[pscustomobject]@{DeviceId=$disk.DeviceId;Error=$_.Exception.Message}}};Save 'storage-reliability.json' $reliable}catch{Save 'storage-reliability.json' @{Error=$_.Exception.Message}}
$dll=@();foreach($name in 'atiadlxx.dll','amdadlx64.dll','ControlLib.dll','nvml.dll'){$path=Join-Path C:\Windows\System32 $name;if(Test-Path -LiteralPath $path){$f=Get-Item -LiteralPath $path;$sig=Get-AuthenticodeSignature -LiteralPath $path;$dll +=[pscustomobject]@{Name=$name;Present=$true;Version=$f.VersionInfo.FileVersion;Signature=$sig.Status.ToString();SHA256=(Get-FileHash -LiteralPath $path).Hash}}else{$dll +=[pscustomobject]@{Name=$name;Present=$false}}};Save 'gpu-runtime.json' $dll
$instance=Get-CimInstance -Namespace root/wmi -ClassName OemWMIMethod|Select-Object -First 1
$class=Get-CimClass -Namespace root/wmi -ClassName OemWMIMethod;$names=@($class.CimClassMethods['OemWMIfun'].Parameters|ForEach-Object {$_.Name});$hwmi=@()
foreach($c in @(@{Name='CPU';Code=0x0202;Index=0},@{Name='GLIV';Code=0x0602;Index=0},@{Name='Fn';Code=0x0604;Index=0},@{Name='Turbo';Code=0x0e04;Index=0},@{Name='KeyboardTimeout';Code=0x1206;Index=0},@{Name='ChargeThresholds';Code=0x1103;Index=0},@{Name='BatteryVoltage';Code=0x0902;Index=0x20},@{Name='BatteryCurrent';Code=0x0902;Index=0x30})){
 foreach($attempt in 1,2,3){$watch=[Diagnostics.Stopwatch]::StartNew();try{$bytes=[byte[]]::new(64);$bytes[0]=$c.Code-band255;$bytes[1]=($c.Code-shr8)-band255;$bytes[2]=$c.Index;if($names -contains 'u8Input'){$arg=@{u8Input=$bytes}}else{$arg=@{u64Input=[BitConverter]::ToUInt64($bytes,0)}};$r=Invoke-CimMethod -InputObject $instance -MethodName OemWMIfun -Arguments $arg;$raw=[byte[]]$r.u8Output;$hwmi +=[pscustomobject]@{Name=$c.Name;Code=('0x{0:X4}' -f $c.Code);Index=$c.Index;Attempt=$attempt;Status=$raw[0];HeadHex=[BitConverter]::ToString($raw[0..15]);DurationMs=$watch.ElapsedMilliseconds}}catch{$hwmi +=[pscustomobject]@{Name=$c.Name;Attempt=$attempt;Error=$_.Exception.Message}}}
};Save 'hwmi-getter-series.json' $hwmi
& powercfg.exe /a | Out-File -LiteralPath ($root+'\power-states.txt') -Encoding UTF8
& powercfg.exe /requests | Out-File -LiteralPath ($root+'\power-requests.txt') -Encoding UTF8
try{Save 'wsl-present.json' @{ExecutablePresent=(Test-Path C:\Windows\System32\wsl.exe);InstalledDistributions=@(Get-ChildItem HKCU:\Software\Microsoft\Windows\CurrentVersion\Lxss -ErrorAction SilentlyContinue|ForEach-Object{(Get-ItemProperty $_.PSPath).DistributionName})}}catch{Save 'wsl-present.json' @{Error=$_.Exception.Message}}
try{$events=@(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-25);Level=1,2,3} -MaxEvents 40 -ErrorAction Stop|Where-Object {$_.ProviderName -match 'WHEA|Kernel-Power|ACPI|disk|nvme|Display'}|Select-Object TimeCreated,ProviderName,Id,LevelDisplayName,Message);Save 'system-hardware-events.json' $events}catch{Save 'system-hardware-events.json' @{Result=$_.Exception.Message}}
'Read-only collection complete'
