@echo off
set CHROME_PATH="C:\Program Files\Google\Chrome\Application\chrome.exe"
set USER_DATA_DIR=%TEMP%\chrome-quic-test

REM Launch Chrome with flags:
REM --ignore-certificate-errors: skips invalid certs (e.g., self-signed)
REM --origin-to-force-quic-on=localhost:4443: forces QUIC for localhost
REM --user-data-dir: isolates test session

%CHROME_PATH% ^
  --ignore-certificate-errors ^
  --origin-to-force-quic-on=localhost:4443 ^
  --user-data-dir=%USER_DATA_DIR%

exit /b
