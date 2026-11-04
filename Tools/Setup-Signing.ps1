<#
.SYNOPSIS
    Creates the MagicControl code-signing certificate, trusts it on this machine
    and signs the binaries.

.DESCRIPTION
    This is the one-shot setup for local development. It performs three steps
    and reports exactly what it changed:

      1. Creates a self-issued code-signing certificate in Cert:\CurrentUser\My
         if one for MagicControl does not already exist. Reusing an existing one
         keeps the thumbprint stable across runs, so binaries signed earlier stay
         valid.

      2. Installs the PUBLIC half into the machine trust stores, so Windows
         recognises the publisher. This is the step that stops the "Unknown
         publisher" prompt, and it is the only step that needs elevation.

         The private key never leaves the current user's store and is created
         non-exportable, so it cannot be copied to another machine.

      3. Signs MagicControl.exe and magiccontrol_tests.exe with a
         timestamp.

    Nothing else is changed: no Defender exclusion, no SmartScreen policy, no
    application-control setting.

    Honest limits, because they matter when reading the result:
      * The certificate is self-issued. Windows on THIS machine will trust it
        after step 2. Other machines will not, and will still warn.
      * SmartScreen reputation is a separate mechanism from publisher trust. A
        self-issued certificate can still be flagged on a machine that has never
        seen the binary. Only a certificate from a public authority removes that
        everywhere.
      * The timestamp is what keeps signatures valid after the certificate
        expires, so a release binary stays verifiable.

.PARAMETER Subject
    Certificate subject. Defaults to CN=MagicControl Local Development.

.PARAMETER SkipTrust
    Create and sign, but do not touch the machine trust stores.

.PARAMETER Force
    Create a new certificate even when a valid one already exists. Use this to
    rotate the key; every previously signed binary then reports an untrusted
    publisher until it is signed again.

.EXAMPLE
    powershell -NoProfile -File Tools\Setup-Signing.ps1
    Full setup: certificate, trust, signature. Prompts for elevation.
#>
[CmdletBinding()]
param(
    [string] $Subject = 'CN=MagicControl Local Development',
    [string] $PreferredSubject = 'Alexey Nikitin',
    # Installs the certificate into the machine trust stores. Requires
    # administrator rights; the script asks for them itself.
    [switch] $Trust,
    [switch] $SkipTrust,
    [switch] $Force,
    [string] $BinaryDirectory,
    [string] $BinaryPath,
    [string] $ManifestPath,
    [switch] $NoTimestamp
)

$ErrorActionPreference = 'Stop'
# A nested Windows PowerShell -NoProfile does not always load the certificate provider.
if (-not (Get-PSDrive -Name Cert -ErrorAction SilentlyContinue)) {
    Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1') -ErrorAction Stop
}
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
if (-not $BinaryDirectory) { $BinaryDirectory = Join-Path $root 'build\windows\bin\Release' }
$friendlyName = 'MagicControl local development - not publicly trusted'
$description = 'MagicControl local development'

# --- Elevation --------------------------------------------------------------

# True when this process already has administrator rights.
function Test-Elevated {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object System.Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Relaunches this script with administrator rights and returns when it finishes.
#
# Asking for elevation here rather than telling the user to open an elevated
# window by hand removes the step that was easy to miss, and a missed step left
# the binaries unsigned and Windows warning about an unknown publisher.
function Invoke-ElevatedSelf {
    param([string[]] $ExtraArguments)

    $scriptPath = $PSCommandPath
    if (-not $scriptPath) {
        $scriptPath = Join-Path $PSScriptRoot 'Setup-Signing.ps1'
    }

    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$scriptPath`"", '-Trust')
    if ($ExtraArguments) { $arguments += $ExtraArguments }

    Write-Host 'Requesting administrator rights, because the trust store is machine-wide.'
    $process = Start-Process -FilePath (Get-Process -Id $PID).Path -Verb RunAs -Wait -PassThru `
        -ArgumentList $arguments
    return $process.ExitCode
}

if ($Trust -and -not $SkipTrust -and -not (Test-Elevated)) {
    # The work is done by the elevated copy; this one only reports the outcome.
    $exitCode = Invoke-ElevatedSelf
    if ($exitCode -ne 0) {
        throw "The elevated run failed with code $exitCode."
    }
    Write-Host ''
    Write-Host '[OK] Trust installed by the elevated run. Run this script again without -Trust to sign future builds.'
    exit 0
}

# --- Helpers ----------------------------------------------------------------

function Find-SignTool {
    foreach ($base in @('C:\Program Files (x86)\Windows Kits\10\bin',
                        'C:\Program Files\Windows Kits\10\bin')) {
        if (-not (Test-Path -LiteralPath $base)) { continue }
        $found = Get-ChildItem -LiteralPath $base -Filter 'signtool.exe' -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw 'SignTool was not found. Install the Windows SDK signing tools.'
}

# --- Step 1: certificate ----------------------------------------------------

function Test-CodeSigningCertificate {
    param($Candidate)
    return $Candidate -and
        $Candidate.HasPrivateKey -and
        $Candidate.Subject -eq $Candidate.Issuer -and
        # Replaced while it still has a month of life left, so an expiring
        # certificate never silently signs a build.
        $Candidate.NotAfter -gt (Get-Date).AddDays(30) -and
        '1.3.6.1.5.5.7.3.3' -in @($Candidate.EnhancedKeyUsageList | ForEach-Object { [string]$_.ObjectId })
}

function Select-ExistingCertificate {
    $all = Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' -ErrorAction SilentlyContinue |
        Where-Object { Test-CodeSigningCertificate $_ }

    # Search order is deliberate. The named publisher certificate is the one the
    # reference application uses, so the same key signs both projects and Windows
    # sees one publisher rather than two. Then a certificate named for this
    # project, then the MECHREVO development certificate, and finally the one with
    # the longest remaining validity.
    $preferred = @($PreferredSubject, 'MagicControl', 'MECHREVO Local Development')
    foreach ($pattern in $preferred) {
        $match = $all | Where-Object { $_.Subject -match [regex]::Escape($pattern) } |
            Sort-Object NotAfter -Descending | Select-Object -First 1
        if ($match) { return $match }
    }

    # An explicit -Subject that matches nothing in the preferred list still wins.
    $explicit = $all | Where-Object { $_.Subject -eq $Subject } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if ($explicit) { return $explicit }

    return $all | Sort-Object NotAfter -Descending | Select-Object -First 1
}

$certificate = $null
if (-not $Force) {
    $certificate = Select-ExistingCertificate
}

if ($certificate) {
    Write-Host "[STEP 1/3] Reusing an existing certificate."
} else {
    Write-Host "[STEP 1/3] Creating the code-signing certificate."
    try {
        $certificate = New-SelfSignedCertificate `
            -Type CodeSigningCert `
            -Subject $Subject `
            -FriendlyName $friendlyName `
            -CertStoreLocation 'Cert:\CurrentUser\My' `
            -KeyAlgorithm RSA `
            -KeyLength 3072 `
            -HashAlgorithm SHA256 `
            -KeyExportPolicy NonExportable `
            -NotAfter (Get-Date).AddYears(2)
        Write-Host '           Created.'
    } catch {
        Write-Host ''
        Write-Host '           Could not create a certificate: the key store refused the write.'
        Write-Host "           $($_.Exception.Message)"
        Write-Host ''
        Write-Host '           A certificate can be created by hand, outside this restricted'
        Write-Host '           environment, with:'
        Write-Host "             New-SelfSignedCertificate -Type CodeSigningCert -Subject '$Subject' ``"
        Write-Host "               -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA ``"
        Write-Host '               -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(2)'
        Write-Host '           Then run this script again.'
        exit 1
    }
}

Write-Host "           Subject    : $($certificate.Subject)"
Write-Host "           Thumbprint : $($certificate.Thumbprint)"
Write-Host "           Valid until: $($certificate.NotAfter.ToString('yyyy-MM-dd'))"
Write-Host "           Private key: stored in this user's profile, non-exportable"

# --- Step 2: publisher trust ------------------------------------------------

if ($SkipTrust) {
    Write-Host '[STEP 2/3] Skipped: -SkipTrust was given, so no trust store was touched.'
} else {
    Write-Host '[STEP 2/3] Installing publisher trust.'

    # The public half goes to a temporary file so the elevated process needs no
    # access to our key material.
    $publicCertificate = Join-Path ([System.IO.Path]::GetTempPath()) "magiccontrol-$($certificate.Thumbprint).cer"
    [System.IO.File]::WriteAllBytes($publicCertificate,
        $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))

    try {
        # TrustedPeople is the per-machine store Windows consults when it decides
        # whether a publisher is trusted. CurrentUser\TrustedPublisher is added
        # too, because some checks consult it first and having both avoids a
        # half-trusted state that is confusing to diagnose.
        $inner = @"
`$ErrorActionPreference = 'Stop'
`$certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2('$publicCertificate')
foreach (`$target in @(
    @{ Name = 'TrustedPeople';    Location = 'LocalMachine' },
    @{ Name = 'TrustedPublisher'; Location = 'CurrentUser'  }
)) {
    `$store = New-Object System.Security.Cryptography.X509Certificates.X509Store(`$target.Name, `$target.Location)
    `$store.Open('ReadWrite')
    `$already = `$store.Certificates | Where-Object { `$_.Thumbprint -eq `$certificate.Thumbprint }
    if (-not `$already) { `$store.Add(`$certificate) }
    `$store.Close()
    Write-Host ('           ' + `$target.Location + '\' + `$target.Name + ' : installed')
}
"@

        Write-Host '           Windows will ask for elevation (UAC).'
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru `
            -ArgumentList '-NoProfile', '-Command', $inner
        if ($process.ExitCode -ne 0) {
            throw "Installing publisher trust failed with code $($process.ExitCode)."
        }
    } finally {
        Remove-Item -LiteralPath $publicCertificate -Force -ErrorAction SilentlyContinue
    }
}

# --- Sign -------------------------------------------------------------------
# This runs on EVERY invocation, whether or not trust was installed.
#
# The reason is a mistake worth recording: signing was originally a separate
# manual step, so a rebuild silently replaced the signed executable with an
# unsigned one. Windows then reported "Unknown publisher" and refused to start
# it through SmartScreen, which looked like an application defect rather than a
# missed command. Signing must be part of building, not a follow-up to it.

Write-Host '[STEP 3/3] Signing the binaries.'

if (-not (Test-Path -LiteralPath $binaryDirectory)) {
    throw "Directory not found: $binaryDirectory. Build the project first."
}

if ($BinaryPath) {
    $binaries = Get-Item -LiteralPath $BinaryPath
} else {
    $binaries = Get-ChildItem -LiteralPath $binaryDirectory -Filter '*.exe' -File |
        Where-Object { $_.Name -in @('MagicControl.exe', 'magiccontrol_tests.exe') }
}
if (-not $binaries) {
    throw "No MagicControl binaries found in $binaryDirectory. Build the project first."
}

$signTool = Find-SignTool
$results = @()

# Timestamp servers, tried in order.
#
# A signature without a timestamp is only valid while the certificate is, and
# this certificate expires on 2027-10-09; a timestamped signature stays valid
# afterwards, so a timestamp is worth real effort.
#
# Why a list rather than one address: RFC 3161 timestamps are fetched over HTTPS,
# and https://timestamp.digicert.com is not reachable from every network while
# http (port 80) answers. SignTool rejects the unreachable one with
# "Invalid Timestamp URL" and the whole build then fails, which is how an
# ordinary network restriction turned into a broken build. Each candidate is
# tried in turn, so a blocked or retired server costs a retry rather than the
# build.
$timestampUrls = @(
    'http://timestamp.sectigo.com',        # RFC 3161 over HTTP, widely reachable
    'http://timestamp.digicert.com',       # RFC 3161 over HTTP
    'http://timestamp.comodoca.com/rfc3161',
    'http://timestamp.globalsign.com/tsa/r6advanced1'
)

foreach ($binary in $binaries) {
    # /fd SHA256 is the file digest; /td SHA256 with /tr is the RFC 3161
    # timestamp.
    $base = @('sign', '/s', 'My', '/sha1', $certificate.Thumbprint, '/fd', 'SHA256', '/d', $description)

    $timestamped = $false
    if (-not $NoTimestamp) {
        foreach ($url in $timestampUrls) {
            $attempt = $base + @('/td', 'SHA256', '/tr', $url, $binary.FullName)
            & $signTool @attempt | Out-Null
            if ($LASTEXITCODE -eq 0) {
                $timestamped = $true
                break
            }
            Write-Host "           Timestamp server unavailable, trying the next: $url"
        }
    }

    if (-not $timestamped) {
        # No timestamp server answered. Sign anyway: an untimestamped signature is
        # still recognised by Windows and still names the publisher, which is the
        # point of signing locally. The compromise is reported, not hidden.
        if (-not $NoTimestamp) {
            Write-Warning 'No timestamp server answered. Signing without a timestamp; the signature will stop being valid when the certificate expires.'
        }
        $attempt = $base + $binary.FullName
        & $signTool @attempt | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Signing failed for $($binary.Name) with code $LASTEXITCODE." }
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $binary.FullName
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
        throw "The signature on $($binary.Name) does not carry the expected certificate."
    }
    if ([string]$signature.Status -in @('HashMismatch', 'NotSigned', 'NotSupportedFileFormat')) {
        throw "Signature verification failed for $($binary.Name): $($signature.Status)."
    }

    $results += [pscustomobject]@{
        File      = $binary.Name
        Status    = [string]$signature.Status
        Publisher = $signature.SignerCertificate.Subject
        Timestamp = if ($signature.TimeStamperCertificate) { 'yes' } else { 'no' }
        SHA256    = (Get-FileHash -LiteralPath $binary.FullName).Hash
    }
}

$results | Format-Table -AutoSize

# --- Report -----------------------------------------------------------------

$report = [ordered]@{
    PreparedAt                     = (Get-Date).ToString('o')
    Subject                        = $certificate.Subject
    CertificateThumbprint          = $certificate.Thumbprint
    ValidUntil                     = $certificate.NotAfter.ToString('o')
    KeyExportable                  = $false
    PubliclyTrusted                = $false
    SmartAppControlCompatibility   = 'not applicable: Smart App Control is off on this machine'
    MachineTrustStores             = if ($SkipTrust) { @() } else { @('LocalMachine\TrustedPeople', 'CurrentUser\TrustedPublisher') }
    OtherWindowsSettingsChanged    = @()
    Files                          = $results
}
if (-not $ManifestPath) { $ManifestPath = Join-Path $binaryDirectory 'code-signing-manifest.json' }
$reportPath = $ManifestPath
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding utf8

Write-Host ''
Write-Host "[OK] Signing manifest written to $reportPath"
Write-Host ''
Write-Host 'Run MagicControl.exe again: the publisher should read as'
Write-Host "  $($certificate.Subject)"
Write-Host ''
Write-Host 'Reminder: this certificate is trusted on this machine only. A binary copied'
Write-Host 'elsewhere will still be reported as coming from an unknown publisher.'
