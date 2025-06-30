param (
    [string]$Thumbprint,
    [string]$Sha256
)

$certStores = @("My", "Root")
$locations = @("LocalMachine", "CurrentUser")
$certDeleted = $false

foreach ($location in $locations) {
    foreach ($store in $certStores) {
        $storePath = "Cert:\$location\$store"
        if ($Thumbprint) {
            $cert = Get-ChildItem -Path $storePath | Where-Object { $_.Thumbprint -eq $Thumbprint }
            if ($cert) {
                Write-Host "Removing from ${storePath}: $($cert.Subject) [$($cert.Thumbprint)]"
                Remove-Item -Path $cert.PSPath -Force
                $certDeleted = $true
            }
        }
    }
}

# Clean up .cer and .pfx files by SHA256
if ($Sha256) {
    $certDir = Join-Path $PSScriptRoot "..\certs"
    $files = Get-ChildItem -Path $certDir -Include *.cer, *.pfx -Recurse
    foreach ($file in $files) {
        $fileHash = (Get-FileHash -Algorithm SHA256 -Path $file.FullName).Hash.ToUpper()
        if ($fileHash -eq $Sha256.ToUpper()) {
            Write-Host "Removing file: $($file.FullName)"
            Remove-Item -Path $file.FullName -Force
        }
    }
} elseif ($Thumbprint -and -not $certDeleted) {
    Write-Host "No matching certificate found in cert stores."
}