@echo off
setlocal

:: Configurable variables
set CERT_NAME=localhost
set CERT_PASSWORD=quictest123
set CERT_FOLDER=%TEMP%\quic-cert-user
set CER_FILE=%CERT_FOLDER%\%CERT_NAME%.cer
set PFX_FILE=%CERT_FOLDER%\%CERT_NAME%.pfx
set CHROME_PATH="C:\Program Files\Google\Chrome\Application\chrome.exe"
set USER_DATA_DIR=%TEMP%\chrome-quic-test-user

:: Create working folder
if exist "%CERT_FOLDER%" rmdir /s /q "%CERT_FOLDER%"
mkdir "%CERT_FOLDER%"

echo Generating self-signed certificate for CN=%CERT_NAME% in CurrentUser scope...
powershell -Command ^
    "$cert = New-SelfSignedCertificate -DnsName '%CERT_NAME%' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy Exportable -KeySpec Signature -HashAlgorithm SHA256 -FriendlyName 'MsQuic-Dev-Cert';" ^
    "Export-Certificate -Cert $cert -FilePath '%CER_FILE%' | Out-Null;" ^
    "Export-PfxCertificate -Cert $cert -FilePath '%PFX_FILE%' -Password (ConvertTo-SecureString -String '%CERT_PASSWORD%' -Force -AsPlainText) | Out-Null;" ^
    "Import-Certificate -FilePath '%CER_FILE%' -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null;"

echo Certificate created and installed:
echo   CER:  %CER_FILE%
echo   PFX:  %PFX_FILE%

echo Launching Chrome with certificate checks disabled and QUIC enabled...
start "" %CHROME_PATH% ^
  --ignore-certificate-errors ^
  --origin-to-force-quic-on=localhost:4443 ^
  --user-data-dir=%USER_DATA_DIR%

endlocal
exit /b
