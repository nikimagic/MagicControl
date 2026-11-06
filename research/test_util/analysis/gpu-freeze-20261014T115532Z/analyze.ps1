param(
    [string]$CaptureDirectory = (Join-Path $PSScriptRoot '../../../../out/test_util/analysis/gpu-freeze-20261014T115532Z'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../../../../out/test_util/generated/gpu-freeze-20261014T115532Z')
)
$ErrorActionPreference = 'Stop'
$lineNumber = 0
$records = @(Get-Content -LiteralPath (Join-Path $CaptureDirectory 'MagicControl-1.jsonl') | ForEach-Object {
    $lineNumber++
    $entry = $_ | ConvertFrom-Json -DateKind String
    $entry | Add-Member -NotePropertyName log_line -NotePropertyValue $lineNumber
    $entry
})
function LocalTime([string]$Utc) {
    ([DateTimeOffset]::Parse($Utc)).ToOffset([TimeSpan]::FromHours(3)).ToString('yyyy-MM-dd HH:mm:ss.fff')
}
function ValueRange($Values) {
    $valid = @($Values | Where-Object { $null -ne $_ })
    if (!$valid.Count) { return $null }
    $range = $valid | Measure-Object -Minimum -Maximum
    [ordered]@{ minimum = $range.Minimum; maximum = $range.Maximum }
}
$samples = @($records | Where-Object event -eq 'sample')
$loaded = @($samples | Where-Object { $null -ne $_.gpu.usage_percent -and $_.gpu.usage_percent -ge 80 })
$readings = @($samples | ForEach-Object {
    [ordered]@{
        log_line = $_.log_line; local_time = LocalTime $_.utc; utc = $_.utc
        cpu_celsius = $_.cpu.celsius; gpu_celsius = $_.gpu.celsius; soc_celsius = $_.soc_celsius
        cpu_core_watts = $_.cpu.watts; gpu_gfx_watts = $_.gpu.watts; apu_package_watts = $_.apu_package_power_watts
        gpu_usage_percent = $_.gpu.usage_percent; gpu_clock_mhz = $_.gpu.clock_mhz
        cpu_rpm = $_.cpu.fan_rpm; gpu_rpm = $_.gpu.fan_rpm
        cpu_pwm = $_.cpu.fan_pwm; gpu_pwm = $_.gpu.fan_pwm
        ac_line_status = $_.windows_power.ac_line_status; battery_watts = $_.battery.watts; battery_volts = $_.battery.volts
        ec_write_count = $_.application_ec_write_count; monitoring_status = $_.monitoring_status
    }
})
$system = @(Get-Content -LiteralPath (Join-Path $CaptureDirectory 'system-events.json') -Raw | ConvertFrom-Json -DateKind String)
$application = @(Get-Content -LiteralPath (Join-Path $CaptureDirectory 'application-events.json') -Raw | ConvertFrom-Json -DateKind String)
$kernel41 = @($system | Where-Object { $_.provider -eq 'Microsoft-Windows-Kernel-Power' -and $_.id -eq 41 } | ForEach-Object {
    $data = [ordered]@{}
    ([xml]$_.xml).SelectNodes('//*[local-name()="EventData"]/*[local-name()="Data"]') | ForEach-Object {
        $data[$_.GetAttribute('Name')] = $_.InnerText
    }
    [ordered]@{ local_time = $_.local; data = $data }
})
$summary = [ordered]@{
    source_log = 'MagicControl-1.jsonl'; records = $records.Count
    start_msk = LocalTime $records[0].utc; end_msk = LocalTime $records[-1].utc
    full_samples = $samples.Count
    last_full_sample_msk = LocalTime $samples[-1].utc
    full_sampling_gap_to_log_end_seconds = [math]::Round(([DateTimeOffset]::Parse($records[-1].utc) - [DateTimeOffset]::Parse($samples[-1].utc)).TotalSeconds, 3)
    high_gpu_load_samples = $loaded.Count
    high_gpu_load_first_msk = LocalTime $loaded[0].utc; high_gpu_load_last_msk = LocalTime $loaded[-1].utc
    high_gpu_load_ranges = [ordered]@{
        cpu_celsius = ValueRange ($loaded | ForEach-Object { $_.cpu.celsius })
        gpu_celsius = ValueRange ($loaded | ForEach-Object { $_.gpu.celsius })
        soc_celsius = ValueRange ($loaded | ForEach-Object { $_.soc_celsius })
        cpu_core_watts = ValueRange ($loaded | ForEach-Object { $_.cpu.watts })
        gpu_gfx_watts = ValueRange ($loaded | ForEach-Object { $_.gpu.watts })
        apu_package_watts = ValueRange ($loaded | ForEach-Object { $_.apu_package_power_watts })
        gpu_usage_percent = ValueRange ($loaded | ForEach-Object { $_.gpu.usage_percent })
        gpu_clock_mhz = ValueRange ($loaded | ForEach-Object { $_.gpu.clock_mhz })
        battery_watts = ValueRange ($loaded | ForEach-Object { $_.battery.watts })
        battery_volts = ValueRange ($loaded | ForEach-Object { $_.battery.volts })
    }
    kernel_power_41 = $kernel41
    system_display_4101_count = @($system | Where-Object { $_.provider -eq 'Display' -and $_.id -eq 4101 }).Count
    system_whea_logger_count = @($system | Where-Object provider -eq 'Microsoft-Windows-WHEA-Logger').Count
    application_hangs = @($application | Where-Object provider -eq 'Application Hang' | Select-Object local,provider,id,message)
    observations = @(
        'User reports a moving mouse cursor while all windows stopped responding.'
        'User reports a built-in GPU stress test in HWiNFO; executable/version not independently identified.'
        'User confirms lid and power-button actions were attempts to recover after the hang.'
        'Power samples are recorded only in finite bursts, not continuously.'
        'Cached RPM and APU power do not have freshness/age fields in this log.'
        'A FANs telemetry message still labels CPU core power APU; package analysis uses apu_package_power_watts only.'
        'No provider call-entry/call-exit markers exist, so a particular AMD/PawnIO call cannot be identified as the blocker.'
    )
}
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$readings | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'readings.json') -Encoding utf8
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'analysis.json') -Encoding utf8
$summary | ConvertTo-Json -Depth 10

