param([switch]$PowerOnly)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
# Fixed getter-only commands. No GUI startup, settings replay or tuning.
& (Join-Path $root 'MagicControl/windows/mctl.exe') power status --read-only > (Join-Path $PSScriptRoot 'power-elevated.json') 2> (Join-Path $PSScriptRoot 'power-elevated.stderr.txt')
$powerCode=$LASTEXITCODE
$probeCode=$null
if(!$PowerOnly){
    & (Join-Path $root 'build/settings-queue-review/bin/Release/magiccontrol_gpu_power_probe.exe') > (Join-Path $PSScriptRoot 'probe-elevated.jsonl') 2> (Join-Path $PSScriptRoot 'probe-elevated.stderr.txt')
    $probeCode=$LASTEXITCODE
}
@{power_exit=$powerCode;probe_exit=$probeCode;finished_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'elevated-result.json')
