@echo off
setlocal

:: Cleanup Chrome test profile
echo Removing Chrome test user data...
rmdir /s /q "%TEMP%\chrome-quic-test"

:: Remove localhost certs from LocalMachine stores
echo Removing CN=localhost certificates from LocalMachine stores...
powershell -Command ^
  "Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq 'CN=localhost' } | Remove-Item -Force;" ^
  "Get-ChildItem Cert:\LocalMachine\Root | Where-Object { $_.Subject -eq 'CN=localhost' } | Remove-Item -Force;"

:: Delete temporary cert files
echo Deleting temp cert files...
rmdir /s /q "%TEMP%\quic-cert"

echo Cleanup complete.
endlocal
exit /b
