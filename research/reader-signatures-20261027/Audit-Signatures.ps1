$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$signTool='C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
$os=[Environment]::OSVersion.Version
$targetOs='2:'+($os.Major,$os.Minor,$os.Build -join '.')
$runtime=Join-Path $root 'src/platform/windows/runtime'
$records=[Collections.Generic.List[object]]::new()
$checks=[Collections.Generic.List[object]]::new()
New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot 'verification'),(Join-Path $PSScriptRoot 'driver-images') | Out-Null
function Verify([string]$id,[string[]]$arguments){
    $log=Join-Path $PSScriptRoot ('verification/'+$id+'.txt')
    & $signTool @arguments *> $log
    $result=[pscustomobject]@{id=$id;arguments=$arguments;exit_code=$LASTEXITCODE;log=$log}
    $checks.Add($result)
    return $result.exit_code
}
foreach($package in @('AmiAfu','IntelFpt','IntelFpt19','InsydeH2offt','NvFlash')){
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $runtime $package) -File | Where-Object {$_.Extension -in '.exe','.dll','.sys','.cat','.bin'}){
        $path=$file.FullName
        $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        # Inert .bin references are exact PE drivers extracted from the vendor
        # executable. A same-byte .sys copy lets the normal driver SIP inspect it.
        if($file.Extension -eq '.bin'){
            $path=Join-Path $PSScriptRoot ('driver-images/'+$package+'-'+$file.BaseName+'.sys')
            Copy-Item -LiteralPath $file.FullName -Destination $path
            if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash){throw 'Reference copy differs'}
        }
        $signature=Get-AuthenticodeSignature -LiteralPath $path
        $id=$package+'-'+$file.Name
        $pa=Verify ($id+'-pa') @('verify','/pa','/all','/v',$path)
        $kp=$null;$secondary=$null
        if([IO.Path]::GetExtension($path) -eq '.sys'){
            # This SDK rejects /all, /ds and /ms together with /kp.
            # Standalone embedded-signature verification has no catalogue /o.
            $kp=Verify ($id+'-kp') @('verify','/kp','/v',$path)
            $all=Get-Content -LiteralPath (Join-Path $PSScriptRoot ('verification/'+$id+'-pa.txt')) -Raw
            if($all -match 'Signature Index:\s*1'){$secondary=Verify ($id+'-pa-secondary') @('verify','/pa','/ds','1','/v',$path)}
        }
        $records.Add([pscustomobject]@{
            package=$package;file=$file.Name;source=$file.FullName;inspected=$path;bytes=$file.Length;sha256=$hash
            file_version=$file.VersionInfo.FileVersion;product_version=$file.VersionInfo.ProductVersion
            authenticode_status=[string]$signature.Status;status_message=$signature.StatusMessage
            signature_type=[string]$signature.SignatureType
            signer_subject=$signature.SignerCertificate.Subject;signer_issuer=$signature.SignerCertificate.Issuer
            signer_thumbprint=$signature.SignerCertificate.Thumbprint
            signer_not_before=$signature.SignerCertificate.NotBefore;signer_not_after=$signature.SignerCertificate.NotAfter
            timestamp_subject=$signature.TimeStamperCertificate.Subject
            signtool_pa_exit=$pa;signtool_pa_secondary_exit=$secondary;signtool_kp_exit=$kp
        })
        Write-Output ($package+'/'+$file.Name+' '+$signature.Status+' pa='+$pa+' kp='+$kp)
    }
}
$insyde=Join-Path $runtime 'InsydeH2offt'
foreach($name in @('H2OFFT64.sys','H2OFFT.inf')){
    $policy=if($name.EndsWith('.sys')){'/kp'}else{'/pa'}
    $null=Verify ('InsydeH2offt-'+$name+'-catalog') @('verify',$policy,'/v','/c',(Join-Path $insyde 'H2OFFT.cat'),(Join-Path $insyde $name))
}
[pscustomobject]@{captured_utc=[DateTime]::UtcNow.ToString('o');signtool=$signTool;target_os=$targetOs;readers_executed=$false;drivers_loaded=$false;files=$records;checks=$checks} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'RESULTS.json') -Encoding utf8
