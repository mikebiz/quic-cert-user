@echo off
setlocal

echo Removing Chrome user profile from temp...
rmdir /s /q "%TEMP%\chrome-quic-test-user"

echo Removing CN=localhost certificates from CurrentUser stores...
powershell -Command ^
  "Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=localhost' } | Remove-Item -Force;" ^
  "Get-ChildItem Cert:\CurrentUser\Root | Where-Object { $_.Subject -eq 'CN=localhost' } | Remove-Item -Force;"

echo Removing exported .cer and .pfx files...
rmdir /s /q "%TEMP%\quic-cert-user"

echo Cleanup complete.
endlocal
exit /b
