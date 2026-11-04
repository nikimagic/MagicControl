# Read-only HLYL discovery. No EC/SMU writes, driver installs, downloads or uploads.
# Firmware APIs cannot enumerate duplicate SSDT instances reliably; also export
# the binary ACPI tables cached by Windows in HKLM\HARDWARE\ACPI.
[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $PSScriptRoot ('Honor-diagnostics-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))))
$ErrorActionPreference = 'Stop'
$computer = Get-CimInstance Win32_ComputerSystem
$board = Get-CimInstance Win32_BaseBoard
if ($computer.Manufacturer -ne 'HUAWEI' -or $computer.Model -ne 'HLYL-WXX9' -or $board.Product -ne 'HLYL-WXX9-PCB') {
    throw 'This collector is only for HUAWEI HLYL-WXX9 / HLYL-WXX9-PCB.'
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists. Choose a new name.' }
$outRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outRoot | Out-Null
$errorsFound = [Collections.Generic.List[string]]::new()
@{ CollectedAt=(Get-Date).ToString('o'); Manufacturer=$computer.Manufacturer; Model=$computer.Model;
   Board=$board.Product; Bios=(Get-CimInstance Win32_BIOS | Select-Object SMBIOSBIOSVersion,ReleaseDate);
   ReadOnly=$true; FirmwareApiDuplicateSsdtLimitation=$true } | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $outRoot 'identity.json') -Encoding UTF8
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HonorFirmwareReader {
    [DllImport("kernel32.dll",SetLastError=true)]
    public static extern uint EnumSystemFirmwareTables(uint provider, byte[] buffer, uint length);
    [DllImport("kernel32.dll",SetLastError=true)]
    public static extern uint GetSystemFirmwareTable(uint provider, uint id, byte[] buffer, uint length);
}
'@
$acpiRoot = Join-Path $outRoot 'acpi'
New-Item -ItemType Directory -Path $acpiRoot | Out-Null
try {
    # Provider DWORD is 'ACPI' in documented big-endian signature notation.
    $provider = [uint32]0x41435049
    $size = [HonorFirmwareReader]::EnumSystemFirmwareTables($provider,$null,0)
    if ($size -eq 0 -or $size -gt 1048576) { throw 'ACPI table enumeration failed.' }
    $ids = New-Object byte[] $size
    $actual = [HonorFirmwareReader]::EnumSystemFirmwareTables($provider,$ids,$size)
    if ($actual -ne $size -or $actual % 4 -ne 0) { throw 'ACPI table list changed or is malformed.' }
    $seen = @{}
    for ($offset=0; $offset -lt $actual; $offset+=4) {
        $id = [BitConverter]::ToUInt32($ids,$offset)
        if ($seen.ContainsKey($id)) { continue }; $seen[$id]=$true
        # Windows OEM activation keys are unrelated to hardware control.
        if ([Text.Encoding]::ASCII.GetString([BitConverter]::GetBytes($id)) -eq 'MSDM') { continue }
        $length = [HonorFirmwareReader]::GetSystemFirmwareTable($provider,$id,$null,0)
        if ($length -lt 36 -or $length -gt 16777216) { continue }
        $bytes = New-Object byte[] $length
        $returned = [HonorFirmwareReader]::GetSystemFirmwareTable($provider,$id,$bytes,$length)
        if ($returned -ne $length) { throw 'ACPI table changed during read.' }
        $name = [Text.Encoding]::ASCII.GetString($bytes,0,4) -replace '[^A-Za-z0-9_]','_'
        [IO.File]::WriteAllBytes((Join-Path $acpiRoot ($name+'-api.aml')),$bytes)
    }
} catch { $errorsFound.Add('Firmware API: '+$_.Exception.Message) }
try {
    $keys = @(Get-Item 'Registry::HKEY_LOCAL_MACHINE\HARDWARE\ACPI') + @(Get-ChildItem 'Registry::HKEY_LOCAL_MACHINE\HARDWARE\ACPI' -Recurse)
    $index=0
    foreach ($key in $keys) {
        foreach ($valueName in $key.GetValueNames()) {
            $data = $key.GetValue($valueName)
            if ($data -isnot [byte[]] -or $data.Length -lt 36 -or $data.Length -gt 16777216) { continue }
            $signature=[Text.Encoding]::ASCII.GetString($data,0,4)
            if ($signature -eq 'MSDM') { continue }
            if ($signature -notmatch '^[A-Z0-9_]{4}$' -or [BitConverter]::ToUInt32($data,4) -ne $data.Length) { continue }
            $index++
            [IO.File]::WriteAllBytes((Join-Path $acpiRoot ('{0}-{1:D3}-registry.aml' -f $signature,$index)),$data)
        }
    }
} catch { $errorsFound.Add('ACPI registry cache: '+$_.Exception.Message) }
try {
    $class=Get-CimClass -Namespace root/wmi -ClassName OemWMIMethod
    $class | Format-List * | Out-String -Width 250 | Set-Content -LiteralPath (Join-Path $outRoot 'wmi-class.txt') -Encoding UTF8
    $instances=@(Get-CimInstance -Namespace root/wmi -ClassName OemWMIMethod | Where-Object {$_.Active -ne $false})
    $matchesHwmi=@($instances | Where-Object {$_.InstanceName -match 'HWMI'})
    if ($matchesHwmi.Count -eq 1) {$instance=$matchesHwmi[0]}
    elseif ($instances.Count -eq 1) {$instance=$instances[0]}
    else {throw 'No unique active Huawei WMI instance.'}
    $inputNames=@($class.CimClassMethods['OemWMIfun'].Parameters | ForEach-Object {$_.Name})
    $getters=[ordered]@{ ChargeThresholds=0x1103; FnLock=0x0604; FanMode=0x0C04;
        Turbo=0x0E04; LegacyKeyboardLevel=0x0602; KeyboardMode=0x1306; KeyboardTimeout=0x1206; SmartCharge=0x1603 }
    $results=@()
    foreach($name in $getters.Keys) {
        $opcode=[uint64]$getters[$name]
        try {
            if ($inputNames -contains 'u8Input') {
                $inputBytes=New-Object byte[] 64
                $inputBytes[0]=[byte]($opcode -band 255);$inputBytes[1]=[byte](($opcode -shr 8) -band 255)
                $arguments=@{u8Input=$inputBytes}
            } elseif ($inputNames -contains 'u64Input') {$arguments=@{u64Input=$opcode}}
            else {throw 'Unknown WMI input ABI.'}
            $reply=Invoke-CimMethod -InputObject $instance -MethodName OemWMIfun -Arguments $arguments
            $bytes=@($reply.u8Output)
            $results+=@{Name=$name;Opcode=('0x{0:X4}' -f $opcode);Bytes=$bytes;Status=$(if($bytes.Count){$bytes[0]}else{$null})}
        } catch {$results+=@{Name=$name;Opcode=('0x{0:X4}' -f $opcode);Error=$_.Exception.Message}}
    }
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outRoot 'wmi-getters.json') -Encoding UTF8
} catch {$errorsFound.Add('WMI getters: '+$_.Exception.Message)}
$errorsFound | Set-Content -LiteralPath (Join-Path $outRoot 'errors.txt') -Encoding UTF8
Get-ChildItem -LiteralPath $outRoot -File -Recurse | Get-FileHash -Algorithm SHA256 |
    Select-Object Hash,@{Name='File';Expression={$_.Path.Substring($outRoot.Length+1)}} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outRoot 'hashes.json') -Encoding UTF8
$zip=$outRoot+'.zip'
Compress-Archive -LiteralPath $outRoot -DestinationPath $zip
Write-Host ('Saved read-only Honor diagnostics: '+$zip)
