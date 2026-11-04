$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=Join-Path $root 'MagicControl/windows/mctl.exe'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(!$admin){throw 'Read verification requires an elevated token'}
$result=@()
# Status only: no value argument, --yes, setter or lifecycle startup.
foreach($name in @('windows','fn','usb','ac','copilot','wifi','bluetooth','camera','touchpad','microphone')){
 & $exe keys $name --read-only > (Join-Path $PSScriptRoot ($name+'.jsonl')) 2> (Join-Path $PSScriptRoot ($name+'.stderr.txt'))
 $result+=@{key=$name;exit_code=$LASTEXITCODE}
}
@{elevated=$admin;version=(Get-Item -LiteralPath $exe).VersionInfo.FileVersion;finished_utc=[DateTime]::UtcNow.ToString('o');read_only_commands=$result}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'result.json') -Encoding utf8
