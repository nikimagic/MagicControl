# Hardware-free cross-profile coverage for fan presentation, battery and
# memory telemetry. Builds isolated test targets, never launches MagicControl.
[CmdletBinding()]
param([string]$LinuxDistribution='Ubuntu-26.04',[string]$PythonExecutable='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))

& (Join-Path $PSScriptRoot 'Run-ModelPreview.ps1') -LinuxDistribution $LinuxDistribution -PythonExecutable $PythonExecutable
if(!$?){throw 'Model and fan preview coverage failed.'}

function Invoke-Checked([string]$Program,[string[]]$Arguments){
    & $Program @Arguments
    if($LASTEXITCODE -ne 0){throw "Device coverage failed ($LASTEXITCODE): $Program"}
}
$cmakeCommand=Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmake=if($cmakeCommand){$cmakeCommand.Source}else{$null}
if(!$cmake){
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    $cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
$targets=@('magiccontrol_battery_aging_tests','magiccontrol_battery_aging_ui_tests','magiccontrol_gm6_default_fan_tests','battery_offline_tests','memory_telemetry_offline_tests')
$tests=@('magiccontrol_battery_aging_tests','magiccontrol_battery_aging_ui_tests','magiccontrol_gm6_default_fan_tests','battery_offline_contract','memory_telemetry_offline_contract')
$win=Join-Path $root 'build/model-preview/windows'
Invoke-Checked $cmake (@('--build',$win,'--config','Release','--parallel','2','--target')+$targets+@('--','/v:minimal','/nologo','/nodeReuse:false'))
foreach($test in $tests){Invoke-Checked $ctest @('--test-dir',$win,'-C','Release','--output-on-failure','--no-tests=error','-R',"^$test$")}

$linuxRoot=(& wsl.exe -d $LinuxDistribution --exec wslpath -u $root | Out-String).Trim()
if($LASTEXITCODE -ne 0 -or !$linuxRoot){throw 'Cannot resolve the Linux workspace.'}
$linux="$linuxRoot/build/model-preview/linux"
Invoke-Checked 'wsl.exe' (@('-d',$LinuxDistribution,'--exec','cmake','--build',$linux,'--parallel','2','--target')+$targets)
foreach($test in $tests){Invoke-Checked 'wsl.exe' @('-d',$LinuxDistribution,'--exec','ctest','--test-dir',$linux,'--output-on-failure','--no-tests=error','-R',"^$test$")}
Write-Host 'Device coverage completed on Windows and Linux using fake or pure data; no hardware access.'
