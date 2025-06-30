# Step 1: Create a self-signed cert for localhost
$cert = New-SelfSignedCertificate `
  -DnsName "localhost" `
  -CertStoreLocation "cert:\LocalMachine\My" `
  -FriendlyName "localhost-MsQuic-Test" `
  -KeyExportPolicy Exportable `
  -KeyUsage DigitalSignature, KeyEncipherment `
  -Type SSLServerAuthentication `
  -HashAlgorithm SHA256 `
  -NotAfter (Get-Date).AddYears(5)

# Step 2: Export the cert (public portion only)
$cerPath = "$env:USERPROFILE\localhost.cer"
Export-Certificate -Cert $cert -FilePath $cerPath -Force

# Step 3: Trust the cert in the LocalMachine Root store
Import-Certificate -FilePath $cerPath -CertStoreLocation "cert:\LocalMachine\Root"
