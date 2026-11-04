$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=Join-Path $root 'MagicControl/windows/mctl.exe'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(!$admin){throw 'Read verification requires elevation'}
& $exe keyboard status --read-only > (Join-Path $PSScriptRoot 'keyboard-status.jsonl') 2> (Join-Path $PSScriptRoot 'keyboard-status.stderr.txt')
$keyboardExit=$LASTEXITCODE
& $exe power status --read-only > (Join-Path $PSScriptRoot 'power-status.jsonl') 2> (Join-Path $PSScriptRoot 'power-status.stderr.txt')
@{keyboard_exit=$keyboardExit;power_exit=$LASTEXITCODE;elevated=$admin;version=(Get-Item -LiteralPath $exe).VersionInfo.FileVersion;finished_utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'result.json') -Encoding utf8
