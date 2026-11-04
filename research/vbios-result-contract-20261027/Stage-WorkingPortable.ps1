$ErrorActionPreference='Stop'
$taskPrimary='C:\Users\admin\Documents\mechrevo_dsh\MagicControl'
$taskIso='C:\Users\admin\Documents\mechrevo_dsh\MagicControl-build-233'
$taskSession=Get-Content -LiteralPath (Join-Path $taskIso 'build/product-build.json') -Raw | ConvertFrom-Json
if($taskSession.status -ne 'complete' -or $taskSession.version -ne '0.1.234'){throw 'Expected complete common0.1.234 build'}
if($taskSession.platforms.windows.status -ne 'verified' -or $taskSession.platforms.linux.status -ne 'verified'){throw 'Both platforms must be verified'}
$taskLinuxIso='/mnt/c/Users/admin/Documents/mechrevo_dsh/MagicControl-build-233'
$taskLinuxPrimary='/mnt/c/Users/admin/Documents/mechrevo_dsh/MagicControl'
wsl.exe -d Ubuntu-26.04 --exec cmake '-DCONFIG=Release' "-DGUI_EXECUTABLE=$taskLinuxIso/build/linux/bin/MagicControl" "-DCLI_EXECUTABLE=$taskLinuxIso/build/linux/bin/mctl" "-DDESTINATION=$taskLinuxPrimary/MagicControl/linux" '-DLAYER_LIBRARY=/usr/lib/x86_64-linux-gnu/libgtk4-layer-shell.so' "-DVERSION_FILE:FILEPATH=$taskLinuxIso/build/linux/generated/MagicControl/Version.h.version" -P "$taskLinuxPrimary/cmake/StagePortableLinux.cmake"
if($LASTEXITCODE -ne 0){throw 'Linux stager failed'}
# Copy the actual verified build sidecar, never synthesize or edit a version.
Copy-Item -LiteralPath (Join-Path $taskIso 'MagicControl/linux/build-version.txt') -Destination (Join-Path $taskPrimary 'MagicControl/linux/build-version.txt')
$taskStage=Join-Path $taskPrimary 'build/verified-vbios-234/windows'
New-Item -ItemType Directory -Force -Path $taskStage | Out-Null
foreach($taskExe in @('MagicControl.exe','mctl.exe')){Copy-Item -LiteralPath (Join-Path $taskIso ('build/windows/bin/Release/'+$taskExe)) -Destination (Join-Path $taskStage $taskExe)}
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' '-DCONFIG=Release' "-DEXECUTABLE=$taskStage/MagicControl.exe" "-DCLI_EXECUTABLE=$taskStage/mctl.exe" "-DDESTINATION=$taskPrimary/MagicControl/windows" '-DSTATIC_CRT=TRUE' "-DPAWNIO_RUNTIME=$taskPrimary/src/platform/windows/runtime/PawnIO" "-DVERSION_FILE:FILEPATH=$taskIso/build/windows/generated/MagicControl/Version.h.version" -P "$taskPrimary/cmake/StagePortableWindows.cmake"
if($LASTEXITCODE -ne 0){throw 'Windows stager failed; close application normally if its EXE is occupied'}
