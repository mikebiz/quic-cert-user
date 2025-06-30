# QUIC Certificate Generator

This project provides scripts to generate and install self-signed certificates for local QUIC/WebTransport development.
It includes PowerShell and batch scripts, a `.pfx` generator, and trusted root installation steps.

## Files

- `scripts\generate-cert.ps1`: PowerShell script to generate self-signed certs (.cer and .pfx).
- `scripts\launch-chrome-ignore-cert.bat`: Starts Chrome with certificate validation disabled.
- `output\`: Contains generated certificates.

## Usage

1. Run the PowerShell script: `.\scripts\generate-cert.ps1`
2. Double-click the `.bat` file to start Chrome ignoring cert errors (testing only).
3. Add the `.cer` file to trusted roots if needed.

## Security Warning

**Do not use this configuration in production.** This is for local testing with QUIC/WebTransport.
