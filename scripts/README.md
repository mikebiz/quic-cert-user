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

Here's a detailed documentation update for your `README.md` to include all `.bat` and `.ps1` scripts along with their purpose and typical usage scenarios. This version reflects the full contents of your updated ZIP:

---

# QUIC Certificate Generator & Test Utility

This repository provides a set of scripts and tools to generate a `localhost` certificate, import it into the Windows certificate store, and launch Chrome in a test mode suitable for QUIC and WebTransport experiments.

---

## 🔧 Folder Structure

```
quic-cert-user-main/
├── certs/
│   └── localhost.pfx                   # Generated PFX file for localhost
├── scripts/
│   ├── cleanup-quic-test-user.bat      # Cleans user-scoped cert store and deletes test certs
│   ├── cleanup-quic-test.bat           # Cleans local machine cert store and deletes test certs
│   ├── create-cert.bat                 # Wrapper for creating localhost cert and importing it
│   ├── create-localhostcert.ps1        # PowerShell script to create a localhost cert
│   ├── generate-cert.ps1               # Main cert creation script; used by `create-cert.bat`
│   ├── launch-chrome-ignore-cert.bat   # Launch Chrome ignoring TLS cert errors
│   ├── launch-chrome-quic-test.bat     # Launch Chrome pointing to `https://localhost:4443/`
│   ├── setup-and-launch-quic-test.bat  # End-to-end: cert setup + launch Chrome test
│   └── setup-quic-test-user.bat        # Sets up certs in current user's store only
├── LICENSE
└── README.md                           # This file
```

---

## ⚙️ Script Usage Guide

| File                                     | Purpose                                                                                                             | When to Use                                                     |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------- |
| `scripts/generate-cert.ps1`              | Core logic to generate `.pfx` and `.cer` files for `CN=localhost`. Uses SHA256 and marks private key as exportable. | Called internally by other scripts; not typically run directly. |
| `scripts/create-localhostcert.ps1`       | Similar to `generate-cert.ps1`, but standalone.                                                                     | For advanced usage or customizing cert fields.                  |
| `scripts/create-cert.bat`                | Batch wrapper to run `generate-cert.ps1`, then import certs to store.                                               | Use this to generate and install certs quickly.                 |
| `scripts/setup-quic-test-user.bat`       | Installs cert to **CurrentUser** store only (no admin required).                                                    | Use for dev environments or non-admin installs.                 |
| `scripts/setup-and-launch-quic-test.bat` | Complete setup and test: creates cert, installs it, launches Chrome.                                                | Use when starting from scratch and running test immediately.    |
| `scripts/cleanup-quic-test.bat`          | Removes certs from **LocalMachine** store. Requires admin.                                                          | Run when cleaning up after machine-wide test.                   |
| `scripts/cleanup-quic-test-user.bat`     | Removes certs from **CurrentUser** store. No admin needed.                                                          | Run to clean user-level test artifacts.                         |
| `scripts/launch-chrome-ignore-cert.bat`  | Launches Chrome with `--ignore-certificate-errors`.                                                                 | Use when Chrome won't trust your local cert.                    |
| `scripts/launch-chrome-quic-test.bat`    | Launches Chrome directly to `https://localhost:4443/`.                                                              | Use when cert is already installed.                             |

---

## ✅ Typical Workflow

1. Open **PowerShell as Administrator** (if using machine-level install).
2. Run:

   ```
   scripts\create-cert.bat
   ```
3. Or run all-in-one:

   ```
   scripts\setup-and-launch-quic-test.bat
   ```
4. To clean up:

   * User certs only:

     ```
     scripts\cleanup-quic-test-user.bat
     ```
   * Machine-wide cleanup (admin):

     ```
     scripts\cleanup-quic-test.bat
     ```

---

## ⚠️ Security Warning

* These scripts disable or bypass TLS validation during Chrome launch for local testing.
* **Never use in production environments.**


