# QUIC Certificate Utility & Chrome Launcher

This repository provides scripts for generating self-signed certificates for `localhost`, configuring your local certificate store, and launching Chrome with QUIC/WebTransport support while bypassing certificate verification for local testing purposes.

---

## 📁 Folder Structure

```
.
├── certs/
│   └── localhost.pfx
├── scripts/
│   ├── create-cert.bat
│   ├── generate-cert.ps1
│   ├── create-localhostcert.ps1
│   ├── cleanup-quic-test.bat
│   ├── cleanup-quic-test-user.bat
│   ├── setup-quic-test-user.bat
│   ├── setup-and-launch-quic-test.bat
│   ├── launch-chrome-quic-test.bat
│   └── launch-chrome-ignore-cert.bat
├── .gitignore
├── LICENSE
└── README.md
```

---

## 🛠 Setup Instructions

### 1. Generate Certificate for `localhost`

You can run either a `.ps1` or `.bat` script depending on your preference:

```powershell
# Using PowerShell (recommended)
powershell -ExecutionPolicy Bypass -File .\scripts\generate-cert.ps1

# Or, using the BAT wrapper
scripts\create-cert.bat
```

This will:
- Generate a SHA256-signed self-signed certificate with CN=`localhost`
- Export both `.pfx` and `.cer` files
- Import the certificate to both `CurrentUser\My` and `LocalMachine\Root` stores

---

### 2. Configure Local Certificate Store for QUIC Testing

If you're testing as a non-admin user, run:

```bat
scripts\setup-quic-test-user.bat
```

If you're testing as admin or in a system context:

```bat
scripts\setup-and-launch-quic-test.bat
```

---

### 3. Launch Chrome with Certificate Checks Disabled (for testing only)

```bat
scripts\launch-chrome-ignore-cert.bat
```

Or, to use a specific QUIC test setup:

```bat
scripts\launch-chrome-quic-test.bat
```

> ⚠️ **Security Warning:** The `--ignore-certificate-errors` flag disables certificate validation in Chrome. Only use this in controlled development environments.

---

### 4. Clean Up Certificates

To remove the certificates from your local store after testing:

```bat
scripts\cleanup-quic-test.bat
```

If you only want to clean the current user’s certs:

```bat
scripts\cleanup-quic-test-user.bat
```

---

## 📄 Notes

- The `localhost.pfx` file is placed in the `certs/` folder for easy reuse.
- These scripts support iterative development using MsQuic, WebTransport, and self-signed TLS certs.

---

## 🔐 Security Notice

Never use these certificates in production. They are self-signed and intended only for development and testing on `localhost`.

---
