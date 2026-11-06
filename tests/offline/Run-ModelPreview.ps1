# Hardware-free model and preview matrix on Windows and Linux.
# Separate build directories; no application startup, device driver or EC access.
[CmdletBinding()]
param([string]$LinuxDistribution='Ubuntu-26.04',[string]$PythonExecutable='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function Invoke-Checked([string]$Program,[string[]]$Arguments){
    & $Program @Arguments
    if($LASTEXITCODE -ne 0){throw "Preview check failed ($LASTEXITCODE): $Program"}
}
$cmake=(Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if(!$cmake){
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    $cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
$win=Join-Path $root 'build/model-preview/windows'
$targets=@('magiccontrol_model_preview_tests','magiccontrol_fan_preview_tests','settings_preview_offline_tests','gpu_preview_offline_tests','settings_cards_offline_tests','settings_geometry_offline_tests')
$filter='^(magiccontrol_model_preview_tests|magiccontrol_fan_preview_tests|settings_preview_offline|gpu_preview_offline|settings_cards_offline_parity|settings_geometry_offline_contract|model_preview_source_wiring|magiccontrol_lenovo_power_json_offline)$'
$configure=@('-S',$root,'-B',$win,'-A','x64','-DMAGICCONTROL_BUILD_TESTS=ON','-DMAGICCONTROL_BUILD_GUI=ON','-DMAGICCONTROL_BUILD_CLI=OFF','-DMAGICCONTROL_SIGN=OFF')
if($PythonExecutable){$configure+="-DPython3_EXECUTABLE=$PythonExecutable"}
Invoke-Checked $cmake $configure
Invoke-Checked $cmake (@('--build',$win,'--config','Release','--parallel','2','--target')+$targets+@('magiccontrol_power_log_tests','--','/v:minimal','/nologo','/nodeReuse:false'))
Invoke-Checked $ctest @('--test-dir',$win,'-C','Release','--output-on-failure','-R',$filter)
$linuxRoot=(& wsl.exe -d $LinuxDistribution --exec wslpath -u $root | Out-String).Trim()
if($LASTEXITCODE -ne 0 -or !$linuxRoot){throw 'Cannot resolve the Linux workspace'}
$linux="$linuxRoot/build/model-preview/linux"
Invoke-Checked 'wsl.exe' @('-d',$LinuxDistribution,'--exec','cmake','-S',$linuxRoot,'-B',$linux,'-DCMAKE_BUILD_TYPE=Release','-DMAGICCONTROL_BUILD_TESTS=ON','-DMAGICCONTROL_BUILD_GUI=ON','-DMAGICCONTROL_BUILD_CLI=OFF','-DMAGICCONTROL_SIGN=OFF')
Invoke-Checked 'wsl.exe' (@('-d',$LinuxDistribution,'--exec','cmake','--build',$linux,'--parallel','2','--target')+$targets)
Invoke-Checked 'wsl.exe' @('-d',$LinuxDistribution,'--exec','ctest','--test-dir',$linux,'--output-on-failure','-R',$filter)
Write-Host 'Model/preview checks completed for Windows and Linux; no application or device access.'
