param (
    [string[]]$DnsNames = @("localhost"),
    [switch]$InstallToRoot = $false
)

$certName = $DnsNames[0]
$certPath = Join-Path $PSScriptRoot "..\certs"
$pfxPath = Join-Path $certPath "$certName.pfx"
$cerPath = Join-Path $certPath "$certName.cer"

if (-not (Test-Path $certPath)) {
    New-Item -Path $certPath -ItemType Directory | Out-Null
}

$cert = New-SelfSignedCertificate -DnsName $DnsNames -CertStoreLocation "cert:\LocalMachine\My" -NotAfter (Get-Date).AddYears(10)
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password (ConvertTo-SecureString -String "password" -Force -AsPlainText) | Out-Null

if ($InstallToRoot) {
    $rootStore = "cert:\LocalMachine\Root"
    Import-Certificate -FilePath $cerPath -CertStoreLocation $rootStore | Out-Null
    Write-Host "Installing certificate to Root..."
}

Write-Host "Generated certificate: $certName"
Write-Host " - CER:  $cerPath"
Write-Host " - PFX:  $pfxPath"

$hash = Get-FileHash -Path $cerPath -Algorithm SHA256
$sha256Hex = $hash.Hash.ToUpper()
Write-Host "`n=== SHA256 ==="
Write-Host $sha256Hex

$sha256Bytes = ($sha256Hex -split '(.{2})' | Where-Object { $_ }) -join ', 0x'
Write-Host "`n=== SHA256 Byte Array (PowerShell format) ==="
Write-Host "0x$sha256Bytes"

$thumbprint = $cert.Thumbprint
Write-Host "`nYou can use this hash in your Visual Studio project configuration or QUIC server setup."
Write-Host "`n=== Certificate Thumbprint (for QUIC/VS usage) ==="
Write-Host $thumbprint
