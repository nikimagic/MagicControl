# One product version, GUI+CLI on both OS, two working portable copies.
# No tests, hardware commands or GitHub packaging/publication are run here.
[CmdletBinding()]
param(
    [switch]$Resume,
    [switch]$NewVersion,
    [string]$LinuxDistribution='Ubuntu-26.04'
)
$ErrorActionPreference='Stop'
if($Resume -and $NewVersion){throw 'Choose either -Resume or -NewVersion.'}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot=Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
function Write-Atomic([string]$Path,[string]$Value){
    $temporary=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllText($temporary,$Value,$utf8)
    if([IO.File]::Exists($Path)){[IO.File]::Replace($temporary,$Path,[NullString]::Value)}
    else{[IO.File]::Move($temporary,$Path)}
}
function Invoke-Native([string]$Program,[string[]]$Arguments){
    & $Program @Arguments | Out-Host
    if($LASTEXITCODE -ne 0){throw ('Command failed ({0}): {1} {2}' -f $LASTEXITCODE,$Program,($Arguments -join ' '))}
}
function Get-SourceFingerprint {
    # Conservative input identity, including new/untracked source files and
    # embedded resources. Generated build/portable/log files are excluded.
    $paths=@(
        (Join-Path $projectRoot 'CMakeLists.txt')
        (Join-Path $projectRoot 'packaging/LICENSE.txt')
    )
    foreach($directory in @('src','cmake','Tools')){
        $files=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -File -Recurse)
        # Python can refresh bytecode when a release helper is imported; it is
        # generated cache, not a change to build inputs or packaging sources.
        if($directory -eq 'Tools'){$files=@($files | Where-Object {$_.FullName -notmatch '(?i)[\\/]__pycache__[\\/]' -and $_.Extension -notin @('.pyc','.pyo')})}
        $paths+=@($files | ForEach-Object {$_.FullName})
    }
    $rows=@($paths | Sort-Object -Unique | ForEach-Object {
        $relative=$_.Substring($projectRoot.Length+1).Replace('\','/')
        $relative+' '+(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
    })
    $algorithm=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($algorithm.ComputeHash($utf8.GetBytes(($rows -join "`n"))))).Replace('-','').ToLowerInvariant()}
    finally{$algorithm.Dispose()}
}
function Get-ApplicationInputs {
    $spec=Get-Content -LiteralPath (Join-Path $projectRoot 'cmake/ApplicationInputs.json') -Raw | ConvertFrom-Json
    $paths=@($spec.files | ForEach-Object {Join-Path $projectRoot $_})
    foreach($tree in $spec.trees){$paths+=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot $tree) -File -Recurse | ForEach-Object {$_.FullName})}
    $result=[ordered]@{}
    foreach($path in ($paths | Sort-Object -Unique)){
        $relative=$path.Substring($projectRoot.Length+1).Replace('\','/')
        $result[$relative]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $result
}
function Read-Version([string]$Path){
    $value=[IO.File]::ReadAllText($Path).Trim()
    if($value -notmatch '^0\.1\.(\d+)$'){throw ('Invalid version in '+$Path)}
    return $value
}
function Save-Session {
    $script:session.updated_utc=[DateTime]::UtcNow.ToString('o')
    Write-Atomic $sessionPath (($script:session | ConvertTo-Json -Depth 8)+"`n")
}
function Assert-Portable([string]$Platform,[string]$Built){
    $portable=Join-Path $projectRoot ('MagicControl/'+$Platform)
    $binary=if($Platform -eq 'windows'){'MagicControl.exe'}else{'MagicControl'}
    $generated=Join-Path $buildRoot ($Platform+'/generated/MagicControl/Version.h.version')
    if((Read-Version $generated) -ne $script:session.version -or
       (Read-Version (Join-Path $portable 'build-version.txt')) -ne $script:session.version){throw ('Version mismatch: '+$Platform)}
    $hash=(Get-FileHash -LiteralPath $Built -Algorithm SHA256).Hash
    if((Get-FileHash -LiteralPath (Join-Path $portable $binary) -Algorithm SHA256).Hash -ne $hash){throw ('Portable binary mismatch: '+$Platform)}
    if($Platform -eq 'windows'){
        $actual=(Get-Item -LiteralPath $Built).VersionInfo.ProductVersion
        if($actual -ne $script:session.version){throw ('Embedded Windows version mismatch: '+$actual)}
    }
    $cliName=if($Platform -eq 'windows'){'mctl.exe'}else{'mctl'}
    $builtCli=Join-Path (Split-Path -Parent $Built) $cliName
    $cliHash=(Get-FileHash -LiteralPath $builtCli -Algorithm SHA256).Hash
    if((Get-FileHash -LiteralPath (Join-Path $portable $cliName) -Algorithm SHA256).Hash -ne $cliHash){throw ('Portable CLI mismatch: '+$Platform)}
    if($Platform -eq 'windows' -and (Get-Item -LiteralPath $builtCli).VersionInfo.ProductVersion -ne $script:session.version){throw 'Embedded CLI version mismatch'}
    return [pscustomobject]@{gui=$hash;cli=$cliHash}
}
function Can-Reuse([string]$Platform,[string]$Built){
    $item=$script:session.platforms.$Platform
    if($item.status -ne 'verified' -or !$item.sha256 -or !$item.cli_sha256){return $false}
    try{$verified=Assert-Portable $Platform $Built;return $verified.gui -eq $item.sha256 -and $verified.cli -eq $item.cli_sha256}catch{return $false}
}
$lock=$null
$sessionPath=Join-Path $buildRoot 'product-build.json'
$versionPath=Join-Path $buildRoot 'product-version.txt'
$counterPath=Join-Path $buildRoot 'version-counter.txt'
$script:session=$null
try{
    # Held across allocation, both builds and staging; a second launcher fails
    # before selecting a different version. Closing this process releases it.
    try{$lock=[IO.File]::Open((Join-Path $buildRoot 'product-build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
    catch{throw 'Another common product build is running. Wait for it to finish.'}
    $cmakeCommand=Get-Command cmake.exe -ErrorAction SilentlyContinue
    $cmake=if($cmakeCommand){$cmakeCommand.Source}else{$null}
    if(!$cmake){
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if(!(Test-Path -LiteralPath $vswhere)){throw 'CMake/Visual Studio not found.'}
        $vsPath=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
        $cmake=Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        if(!(Test-Path -LiteralPath $cmake)){throw 'CMake not found in Visual Studio.'}
    }
    # Verify WSL before reserving a number or changing either portable copy.
    $linuxRoot=(& wsl.exe -d $LinuxDistribution --exec wslpath -u $projectRoot | Out-String).Trim()
    if($LASTEXITCODE -ne 0 -or !$linuxRoot){throw 'WSL project path is unavailable.'}
    Invoke-Native 'wsl.exe' @('-d',$LinuxDistribution,'--exec','cmake','--version')
    $fingerprint=Get-SourceFingerprint
    $previous=if(Test-Path -LiteralPath $sessionPath){Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json}else{$null}
    $sameInputs=$previous -and $previous.source_fingerprint -eq $fingerprint
    $unfinished=$previous -and $previous.status -ne 'complete'
    if($Resume -and !$unfinished){throw 'No unfinished common build. Start a new common build instead.'}
    if(!$NewVersion -and $unfinished){
        $script:session=$previous
        if(!(Test-Path -LiteralPath $versionPath) -or (Read-Version $versionPath) -ne $session.version){throw 'Selected product version differs from the unfinished session.'}
        if(!$sameInputs){
            # A compile fix is still part of the unfinished product build. Keep
            # its number but rebuild BOTH platforms from the new source tree.
            $session.source_fingerprint=$fingerprint
            foreach($platform in @('windows','linux')){$session.platforms.$platform.status='pending';$session.platforms.$platform.sha256='';$session.platforms.$platform.error='';$session.platforms.$platform | Add-Member -Force -NotePropertyName cli_sha256 -NotePropertyValue ''}
            Write-Host 'Inputs changed: both platforms will be rebuilt for this unfinished version.'
        }
        Write-Host ('Resuming common product build '+$session.version)
    }else{
        # Preserve numbering after source transfers or older split OS builds.
        $counter=0L
        if(Test-Path -LiteralPath $counterPath){$value=[IO.File]::ReadAllText($counterPath).Trim();if($value -notmatch '^\d+$'){throw 'Invalid build counter.'};$counter=[long]$value}
        foreach($path in @($versionPath,(Join-Path $projectRoot 'MagicControl/windows/build-version.txt'),(Join-Path $projectRoot 'MagicControl/linux/build-version.txt'))){
            if(Test-Path -LiteralPath $path){$value=Read-Version $path;$number=[long]($value -replace '^0\.1\.','');$counter=[Math]::Max($counter,$number)}
        }
        $counter++
        if($counter -gt 65535){throw 'Build number exceeds the Windows version field; choose a new minor version.'}
        $selected='0.1.'+$counter
        Write-Atomic $counterPath ($counter.ToString()+"`n")
        Write-Atomic $versionPath ($selected+"`n")
        $script:session=[pscustomobject]@{version=$selected;source_fingerprint=$fingerprint;status='building';started_utc=[DateTime]::UtcNow.ToString('o');updated_utc='';
            platforms=[pscustomobject]@{windows=[pscustomobject]@{status='pending';sha256='';cli_sha256='';error=''};linux=[pscustomobject]@{status='pending';sha256='';cli_sha256='';error=''}}}
        Save-Session
        Write-Host ('New common product build '+$selected)
    }
    $session.status='building';Save-Session
    $windowsBuild=Join-Path $buildRoot 'windows'
    $windowsBinary=Join-Path $windowsBuild 'bin/Release/MagicControl.exe'
    $linuxBinary=Join-Path $buildRoot 'linux/bin/MagicControl'
    $applicationInputs=Get-ApplicationInputs
    $session | Add-Member -Force -NotePropertyName application_inputs -NotePropertyValue $applicationInputs
    $session | Add-Member -Force -NotePropertyName source_revision -NotePropertyValue ((& git -C $projectRoot rev-parse HEAD | Out-String).Trim())
    $dirty=(& git -C $projectRoot status --porcelain --untracked-files=all -- src cmake CMakeLists.txt Tools/Build-All.ps1 | Out-String).Trim()
    # Ignored local binary dependencies are not represented by HEAD.
    $tracked=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($path in (& git -c core.quotepath=false -C $projectRoot ls-files -- src cmake CMakeLists.txt Tools/Build-All.ps1 packaging/fan-curves.csv packaging/app-settings.ini packaging/LICENSE.txt)){[void]$tracked.Add($path)}
    $untrackedInputs=@($applicationInputs.Keys | Where-Object { !$tracked.Contains($_) })
    $session | Add-Member -Force -NotePropertyName source_dirty -NotePropertyValue ([bool]($dirty -or $untrackedInputs.Count))
    Save-Session
    foreach($platform in @('windows','linux')){
        $built=if($platform -eq 'windows'){$windowsBinary}else{$linuxBinary}
        if(Can-Reuse $platform $built){Write-Host ($platform+': already verified '+$session.version);continue}
        $item=$session.platforms.$platform;$item.status='building';$item.error='';Save-Session
        try{
            if($platform -eq 'windows'){
                Invoke-Native $cmake @('-S',$projectRoot,'-B',$windowsBuild,'-A','x64','-DMAGICCONTROL_BUILD_GUI=ON','-DMAGICCONTROL_BUILD_CLI=ON','-DMAGICCONTROL_BUILD_TESTS=OFF')
                Invoke-Native $cmake @('--build',$windowsBuild,'--config','Release','--target','magiccontrol-gui','magiccontrol','--parallel','1','--','/v:minimal','/nologo','/nodeReuse:false')
            }else{
                Invoke-Native 'wsl.exe' @('-d',$LinuxDistribution,'--exec','cmake','-S',$linuxRoot,'-B',($linuxRoot+'/build/linux'),'-DCMAKE_BUILD_TYPE=Release','-DMAGICCONTROL_BUILD_GUI=ON','-DMAGICCONTROL_BUILD_CLI=ON','-DMAGICCONTROL_BUILD_TESTS=OFF')
                Invoke-Native 'wsl.exe' @('-d',$LinuxDistribution,'--exec','cmake','--build',($linuxRoot+'/build/linux'),'--target','magiccontrol-gui','magiccontrol','--parallel','2')
            }
            $item.status='staging';Save-Session
            if($platform -eq 'windows'){
                Invoke-Native $cmake @('-DCONFIG=Release',('-DEXECUTABLE='+$windowsBinary),('-DDESTINATION='+(Join-Path $projectRoot 'MagicControl/windows')),'-DSTATIC_CRT=TRUE',
                    ('-DCLI_EXECUTABLE='+(Join-Path $windowsBuild 'bin/Release/mctl.exe')),
                    ('-DPAWNIO_RUNTIME='+(Join-Path $projectRoot 'src/platform/windows/runtime/PawnIO')),('-DVERSION_FILE='+(Join-Path $windowsBuild 'generated/MagicControl/Version.h.version')),
                    '-P',(Join-Path $projectRoot 'cmake/StagePortableWindows.cmake'))
            }else{
                Invoke-Native 'wsl.exe' @('-d',$LinuxDistribution,'--exec','cmake','-DCONFIG=Release',('-DGUI_EXECUTABLE='+$linuxRoot+'/build/linux/bin/MagicControl'),
                    ('-DCLI_EXECUTABLE='+$linuxRoot+'/build/linux/bin/mctl'),
                    ('-DDESTINATION='+$linuxRoot+'/MagicControl/linux'),'-DLAYER_LIBRARY=/usr/lib/x86_64-linux-gnu/libgtk4-layer-shell.so',
                    ('-DVERSION_FILE='+$linuxRoot+'/build/linux/generated/MagicControl/Version.h.version'),'-P',($linuxRoot+'/cmake/StagePortableLinux.cmake'))
            }
            $verified=Assert-Portable $platform $built;$item.sha256=$verified.gui;$item | Add-Member -Force -NotePropertyName cli_sha256 -NotePropertyValue $verified.cli;$item.status='verified';Save-Session
            Write-Host ($platform+': portable verified '+$session.version)
        }catch{
            $item.error=$_.Exception.Message;$item.status='failed';$session.status='incomplete';Save-Session
            throw
        }
    }
    if((Get-SourceFingerprint) -ne $fingerprint){$session.status='source_changed';Save-Session;throw 'Source inputs changed during the build. Start a new common build.'}
    $session.status='complete';Save-Session
    Write-Host ('SUCCESS: Windows and Linux portable both '+$session.version+'. GitHub archives were not changed.')
}finally{if($lock){$lock.Dispose()}}
