# generate-local-cert.ps1

param (
    [string]$CertName = "localhost",
    [string]$CertFolder = "certs"
)

# Ensure output folder exists
if (-not (Test-Path -Path $CertFolder)) {
    New-Item -Path $CertFolder -ItemType Directory | Out-Null
}

# Generate self-signed certificate
$cert = New-SelfSignedCertificate `
    -DnsName $CertName `
    -FriendlyName "MsQuic-Test-$CertName" `
    -KeyUsageProperty Sign `
    -KeyUsage DigitalSignature `
    -CertStoreLocation "cert:\LocalMachine\My" `
    -HashAlgorithm SHA256 `
    -Provider "Microsoft Software Key Storage Provider" `
    -KeyExportPolicy Exportable

# Export to PFX
$pfxPath = Join-Path $CertFolder "$CertName.pfx"
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password (ConvertTo-SecureString -String "password" -Force -AsPlainText) | Out-Null

# Export to CER
$cerPath = Join-Path $CertFolder "$CertName.cer"
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

Write-Output "Generated certificate: $CertName"
Write-Output " - CER:  $cerPath"
Write-Output " - PFX:  $pfxPath"

# Calculate SHA256 and print byte array
$hash = Get-FileHash -Path $cerPath -Algorithm SHA256

# Convert hex string to byte array
$hashBytes = -split $hash.Hash -replace '..', { "0x$($_)" } | ForEach-Object { [byte]$_ }

# Print SHA256 byte array
$byteArrayString = ($hashBytes | ForEach-Object { "0x{0:X2}" -f $_ }) -join ", "
Write-Output "`n=== SHA256 Byte Array ==="
Write-Output $byteArrayString
