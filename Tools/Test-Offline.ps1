# Explicit hardware-free test run. Does not build/run the application or install drivers.
[CmdletBinding()]
param([string]$LinuxDistribution='Ubuntu-26.04',[string]$PythonExecutable='')
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Invoke-Offline([string]$Program,[string[]]$Arguments){
    & $Program @Arguments
    if($LASTEXITCODE -ne 0){throw "Offline command failed ($LASTEXITCODE): $Program"}
}
$taskCmake=(Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if(!$taskCmake){
    $taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $taskVsPath=(& $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    $taskCmake=Join-Path $taskVsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
$taskCtest=Join-Path (Split-Path $taskCmake) 'ctest.exe'
$taskWindows=Join-Path $taskRoot 'build/offline/windows'
$taskConfigure=@('-S',(Join-Path $taskRoot 'tests/offline'),'-B',$taskWindows,'-A','x64')
if($PythonExecutable){$taskConfigure+="-DPython3_EXECUTABLE=$PythonExecutable"}
Invoke-Offline $taskCmake $taskConfigure
Invoke-Offline $taskCmake @('--build',$taskWindows,'--config','Release','--parallel','2','--','/v:minimal','/nologo','/nodeReuse:false')
Invoke-Offline $taskCtest @('--test-dir',$taskWindows,'-C','Release','--output-on-failure','--output-junit',(Join-Path $taskWindows 'results.xml'))
$taskLinuxRoot=(& wsl.exe -d $LinuxDistribution --exec wslpath -u $taskRoot | Out-String).Trim()
if($LASTEXITCODE -ne 0){throw 'Cannot resolve WSL workspace'}
Invoke-Offline 'wsl.exe' @('-d',$LinuxDistribution,'--exec','cmake','-S',"$taskLinuxRoot/tests/offline",'-B',"$taskLinuxRoot/build/offline/linux",'-DCMAKE_BUILD_TYPE=Release')
Invoke-Offline 'wsl.exe' @('-d',$LinuxDistribution,'--exec','cmake','--build',"$taskLinuxRoot/build/offline/linux",'--parallel','2')
Invoke-Offline 'wsl.exe' @('-d',$LinuxDistribution,'--exec','ctest','--test-dir',"$taskLinuxRoot/build/offline/linux",'--output-on-failure','--output-junit',"$taskLinuxRoot/build/offline/linux/results.xml")
Write-Host 'Offline suites passed on Windows and Linux. No MagicControl application or hardware backend run; Windows also tested isolated offscreen native controls.'
