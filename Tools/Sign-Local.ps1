<#
.SYNOPSIS
    Signs the MagicControl binaries with a local development certificate.

.DESCRIPTION
    Signing and trusting are two separate things, and this script keeps them
    apart on purpose:

      Signing   puts a signature on the executable. It removes the "Unknown
                publisher" line from the SmartScreen prompt and makes the binary
                tamper-evident. It needs no elevation, because the certificate
                and its private key live in the current user's store.

      Trusting  adds the certificate to a Windows trust store so the signature
                is recognised as coming from a trusted publisher. That is what
                removes the prompt itself. It changes a machine-wide store and
                therefore needs elevation, which is why it is a separate switch.

    By default this script only signs. Pass -Trust to also install the public
    certificate into LocalMachine\TrustedPeople, which is the per-machine store
    Windows consults for publisher trust.

    Neither action makes the certificate publicly trusted: other machines will
    still show the prompt. Only a certificate issued by a public authority
    removes it everywhere.

.PARAMETER Thumbprint
    Thumbprint of the certificate to use. When omitted, the script looks for a
    code-signing certificate issued to itself in the current user's store and
    prefers one whose subject mentions MagicControl or MECHREVO, so a machine
    with several development certificates picks the project one.

.PARAMETER Directory
    Directory holding the binaries. Defaults to build\windows\bin\Release.

.PARAMETER Trust
    Also install the certificate into LocalMachine\TrustedPeople. Prompts for
    elevation.

.PARAMETER Untrust
    Removes the certificate from LocalMachine\TrustedPeople again.

.EXAMPLE
    powershell -NoProfile -File Tools\Sign-Local.ps1
    Signs the Release binaries with the project development certificate.

.EXAMPLE
    powershell -NoProfile -File Tools\Sign-Local.ps1 -Trust
    Signs, then installs publisher trust so Windows stops prompting.
#>
[CmdletBinding()]
param(
    [string] $Thumbprint,
    [string] $Directory,
    [switch] $Trust,
    [switch] $Untrust
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
if (-not $Directory) { $Directory = Join-Path $root 'build\windows\bin\Release' }

$description = 'MagicControl local development'

# --- Locate SignTool --------------------------------------------------------
function Find-SignTool {
    $candidates = @(
        'C:\Program Files (x86)\Windows Kits\10\bin',
        'C:\Program Files\Windows Kits\10\bin'
    )
    foreach ($base in $candidates) {
        if (-not (Test-Path -LiteralPath $base)) { continue }
        $found = Get-ChildItem -LiteralPath $base -Filter 'signtool.exe' -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw 'SignTool was not found. Install the Windows SDK signing tools.'
}

# --- Pick a certificate -----------------------------------------------------
function Select-SigningCertificate {
    param([string] $Requested)

    if ($Requested) {
        $normalised = ($Requested -replace '\s', '').ToUpperInvariant()
        if ($normalised -notmatch '^[0-9A-F]{40}$') { throw 'Thumbprint must be 40 hexadecimal characters.' }
        $explicit = Get-Item -LiteralPath "Cert:\CurrentUser\My\$normalised" -ErrorAction SilentlyContinue
        if (-not $explicit) { throw "Certificate $normalised was not found in Cert:\CurrentUser\My." }
        return $explicit
    }

    $candidates = Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' -ErrorAction SilentlyContinue |
        Where-Object {
            $_.HasPrivateKey -and
            $_.Subject -eq $_.Issuer -and
            $_.NotAfter -gt (Get-Date) -and
            '1.3.6.1.5.5.7.3.3' -in @($_.EnhancedKeyUsageList | ForEach-Object { [string]$_.ObjectId })
        }

    if (-not $candidates) {
        throw 'No self-issued code-signing certificate with a private key was found. Create one with New-SelfSignedCertificate -Type CodeSigningCert.'
    }

    # Prefer a certificate that names this project, then the one with the longest
    # remaining validity, so a stale certificate is never chosen by accident.
    $project = $candidates | Where-Object { $_.Subject -match 'MagicControl|MECHREVO' }
    $pool = if ($project) { $project } else { $candidates }
    return $pool | Sort-Object NotAfter -Descending | Select-Object -First 1
}

# --- Trust store ------------------------------------------------------------
function Set-PublisherTrust {
    param(
        [System.Security.Cryptography.X509Certificates.X509Certificate2] $Certificate,
        [switch] $Remove
    )

    $storePath = 'Cert:\LocalMachine\TrustedPeople'
    $existing = Get-ChildItem -LiteralPath $storePath -ErrorAction SilentlyContinue |
        Where-Object { $_.Thumbprint -eq $Certificate.Thumbprint }

    if ($Remove) {
        if (-not $existing) {
            Write-Host "[INFO] Certificate is not in $storePath, nothing to remove."
            return
        }
        Write-Host '[INFO] Removing publisher trust. Windows will ask for elevation.'
        $script = "Remove-Item -LiteralPath '$storePath\$($Certificate.Thumbprint)' -Force"
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru `
            -ArgumentList '-NoProfile', '-Command', $script
        if ($process.ExitCode -ne 0) { throw "Removing trust failed with code $($process.ExitCode)." }
        Write-Host '[OK] Publisher trust removed.'
        return
    }

    if ($existing) {
        Write-Host "[INFO] Certificate is already trusted in $storePath."
        return
    }

    # LocalMachine needs elevation. The public certificate is written to a
    # temporary file first so the elevated process needs no access to our key,
    # and the private key never leaves the current user's store.
    $publicPath = Join-Path ([System.IO.Path]::GetTempPath()) "magiccontrol-trust-$($Certificate.Thumbprint).cer"
    [System.IO.File]::WriteAllBytes($publicPath,
        $Certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))

    try {
        Write-Host '[INFO] Installing publisher trust. Windows will ask for elevation.'
        $script = @"
`$certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2('$publicPath')
`$store = New-Object System.Security.Cryptography.X509Certificates.X509Store('TrustedPeople', 'LocalMachine')
`$store.Open('ReadWrite')
`$store.Add(`$certificate)
`$store.Close()
"@
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru `
            -ArgumentList '-NoProfile', '-Command', $script
        if ($process.ExitCode -ne 0) { throw "Installing trust failed with code $($process.ExitCode)." }
        Write-Host '[OK] Publisher trust installed.'
    } finally {
        Remove-Item -LiteralPath $publicPath -Force -ErrorAction SilentlyContinue
    }
}

# --- Main -------------------------------------------------------------------
$certificate = Select-SigningCertificate -Requested $Thumbprint
Write-Host "[INFO] Certificate : $($certificate.Subject)"
Write-Host "[INFO] Thumbprint  : $($certificate.Thumbprint)"
Write-Host "[INFO] Valid until : $($certificate.NotAfter.ToString('yyyy-MM-dd'))"
Write-Host "[INFO] Directory   : $Directory"
Write-Host ''

if ($Untrust) {
    Set-PublisherTrust -Certificate $certificate -Remove
    exit 0
}

if (-not (Test-Path -LiteralPath $Directory)) {
    throw "Directory not found: $Directory. Build the project first."
}

$binaries = Get-ChildItem -LiteralPath $Directory -Filter '*.exe' -File |
    Where-Object { $_.Name -in @('MagicControl.exe', 'magiccontrol_tests.exe') }
if (-not $binaries) {
    throw "No MagicControl binaries found in $Directory. Build the project first."
}

$signTool = Find-SignTool
Write-Host "[INFO] SignTool    : $signTool"
Write-Host ''

foreach ($binary in $binaries) {
    # /fd SHA256 selects the file digest, /td SHA256 and /tr the RFC 3161
    # timestamp. A timestamp keeps the signature valid after the certificate
    # expires, which matters for a build that may be kept for years.
    & $signTool sign /s My /sha1 $certificate.Thumbprint /fd SHA256 /td SHA256 `
        /tr 'http://timestamp.digicert.com' /d $description $binary.FullName | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Signing failed for $($binary.Name) with code $LASTEXITCODE." }

    $signature = Get-AuthenticodeSignature -LiteralPath $binary.FullName
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint -or
        [string]$signature.Status -in @('NotSigned', 'HashMismatch', 'NotSupportedFileFormat')) {
        throw "Signature verification failed for $($binary.Name): $($signature.Status)."
    }

    Write-Host "[OK] $($binary.Name): $($signature.Status)"
}

Write-Host ''
Write-Host 'Signing complete. A signature proves the file was not altered, but Windows'
Write-Host 'still shows a publisher prompt until the certificate is trusted.'

if ($Trust) {
    Write-Host ''
    Set-PublisherTrust -Certificate $certificate
    Write-Host ''
    Write-Host 'Run the binaries again: the publisher should now read as'
    Write-Host "  $($certificate.Subject)"
} else {
    Write-Host ''
    Write-Host 'To also install publisher trust on this machine, run:'
    Write-Host '    powershell -NoProfile -File Tools\Sign-Local.ps1 -Trust'
}
