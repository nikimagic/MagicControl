$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=Join-Path $root 'MagicControl/windows/mctl.exe'
$admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(!$admin){throw 'Elevated read verification required'}
& $exe keys microphone --read-only > (Join-Path $PSScriptRoot 'status.jsonl') 2> (Join-Path $PSScriptRoot 'status.stderr.txt')
$nativeExit=$LASTEXITCODE
& $exe keys microphone --no-ec --read-only > (Join-Path $PSScriptRoot 'status-no-ec.jsonl') 2> (Join-Path $PSScriptRoot 'status-no-ec.stderr.txt')
@{native_exit=$nativeExit;no_ec_exit=$LASTEXITCODE;elevated=$admin;version=(Get-Item -LiteralPath $exe).VersionInfo.FileVersion;finished_utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'result.json') -Encoding utf8
