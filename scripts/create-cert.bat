@echo off
REM Batch file to generate a self-signed localhost certificate using PowerShell
REM It calls the create-cert.ps1 script

echo Creating self-signed localhost certificate...
powershell -ExecutionPolicy Bypass -File "%~dp0\create-cert.ps1"
if %ERRORLEVEL% NEQ 0 (
    echo Failed to create certificate. Make sure PowerShell is enabled and script permissions are set.
    pause
    exit /b %ERRORLEVEL%
)
echo Done.
pause

