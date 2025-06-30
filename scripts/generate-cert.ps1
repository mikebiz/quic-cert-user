
$certName = "localhost"
$certPath = "Cert:\LocalMachine\My"
$pfxPassword = ConvertTo-SecureString -String "password" -Force -AsPlainText
$pfxOutput = "output\localhost.pfx"
$cerOutput = "output\localhost.cer"

$cert = New-SelfSignedCertificate -DnsName $certName `
    -CertStoreLocation $certPath `
    -KeyExportPolicy Exportable `
    -KeySpec Signature `
    -KeyLength 2048 `
    -KeyAlgorithm RSA `
    -HashAlgorithm SHA256 `
    -NotAfter (Get-Date).AddYears(1) `
    -FriendlyName "localhost QUIC cert"

Export-Certificate -Cert $cert -FilePath $cerOutput
Export-PfxCertificate -Cert $cert -FilePath $pfxOutput -Password $pfxPassword
