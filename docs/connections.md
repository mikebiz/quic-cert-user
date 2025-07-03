# Connections

## 1. MsQuicOpen2

**Purpose:** Initialize the library and retrieve the function table.
**API:**

```cpp
QUIC_STATUS
MsQuicOpen2(
    _In_ uint32_t ClientVersion,
    _Out_ const QUIC_TABLE** QuicLib
    );
```

* **ClientVersion**: Pass `MSQUIC_API_VERSION_2` (or higher).
* **QuicLib**: On success, points to a table of function pointers (`MsQuic`, `RegistrationOpen`, etc.).
* **Error paths**: Returns `QUIC_STATUS_NOT_SUPPORTED` if version mismatch, or `QUIC_STATUS_VER_NEG_ERROR` if negotiation fails.

> Once you have `QuicLib`, all subsequent calls go through that table.

---

## 2. RegistrationOpen

**Purpose:** Create a “registration” object grouping your connections under an app identity.
**API:**

```cpp
QUIC_STATUS
RegistrationOpen(
    _In_ const QUIC_REGISTRATION_CONFIG* RegConfig,
    _In_ void* Context,
    _Out_ HQUIC* RegistrationHandle
    );
```

* **RegConfig**:

  * `AppName`: A null-terminated string identifying your application.
  * `ExecutionProfile`: e.g. `QUIC_EXECUTION_PROFILE_LOW_LATENCY`.
* **Context**: User pointer passed to all callbacks.
* **RegistrationHandle**: Returned handle for future calls.
* **Error paths**: Fails if you call before `MsQuicOpen2` or pass invalid config.

---

## 3. ConfigurationOpen

**Purpose:** Bind TLS, ALPN, congestion, and transport settings to your registration.
**API:**

```cpp
QUIC_STATUS
ConfigurationOpen(
    _In_ HQUIC Registration,
    _In_ const QUIC_BUFFER* AlpnBuffers,
    _In_ uint32_t AlpnBufferCount,
    _In_ const QUIC_SETTINGS* Settings,
    _In_ uint32_t SettingsBufferLength,
    _In_opt_ void* Context,
    _Out_ HQUIC* ConfigurationHandle
    );
```

* **AlpnBuffers**: Array of ALPN strings (e.g. `"h3"` or custom).
* **Settings**:

  * `IdleTimeoutMs`, `DesiredMaxBytesPerKey`, etc.
* **Context**: Optional pointer for config-level callbacks.
* **ConfigurationHandle**: Use this when starting connections or listeners.
* **Error paths**: Fails if TLS credential isn’t loaded, or ALPN is unsupported.

> You must call `SetParam` (e.g. for `QUIC_PARAM_GLOBAL_TLS_CREDENTIAL_CONFIG`) *before* `ConfigurationOpen`.

---

## 4. ConnectionOpen

**Purpose:** Allocate a new connection and register your event callback.
**API:**

```cpp
QUIC_STATUS
ConnectionOpen(
    _In_ HQUIC Registration,
    _In_ QUIC_CONNECTION_CALLBACK_HANDLER Handler,
    _In_opt_ void* Context,
    _Out_ HQUIC* ConnectionHandle
    );
```

* **Registration**: The handle from step 2.
* **Handler**: Your function receiving `QUIC_CONNECTION_EVENT` callbacks (e.g. `CONNECTED`, `SHUTDOWN`).
* **Context**: User pointer for per-connection state.
* **ConnectionHandle**: Returned handle, opaque until closed.
* **Error paths**: Fails if registration is invalid or out of memory.

---

## 5. ConnectionStart

**Purpose:** Kick off the handshake (Client) or await an incoming packet (Server).
**API:**

```cpp
QUIC_STATUS
ConnectionStart(
    _In_ HQUIC Connection,
    _In_ QUIC_ADDRESS_FAMILY Family,
    _In_ const char* ServerName,
    _In_ uint16_t ServerPort
    );
```

* **Connection**: From step 4.
* **Family**: `QUIC_ADDRESS_FAMILY_UNSPEC` or specific (IPv4/IPv6).
* **ServerName/Port**: DNS name and port of the peer.
* **Behavior**:

  * **Client**: Sends Initial + Handshake packets.
  * **Server**: Passive—first incoming packet triggers `HANDSHAKE_RECEIVED`.
* **Error paths**: Immediate failure if DNS lookup fails or parameters invalid.

---

## 6. Connected Callback

**Purpose:** Signal that the handshake has completed successfully (or failed).
**Callback Event:**

```cpp
case QUIC_CONNECTION_EVENT_CONNECTED:
    // Connection->Context now fully established
    break;
```

* **When fired**: After 1-RTT handshake success.
* **Data**: Contains negotiated ALPN, peer transport parameters.
* **Error paths**: If handshake fails, you’ll see `QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED` with a non-zero error code instead.

---

## 7. Shutdown & Error Handling

**Purpose:** Close the QUIC connection gracefully or in response to errors.

### a) Graceful Shutdown

```cpp
MsQuicConnectionShutdown(
    Connection,
    QUIC_STATUS_SUCCESS,
    0 // Application close code
    );
```

* Fires `QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED` → send CloseNotify.
* Then `QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE` when peer ack’s closure.

### b) Abortive / Error Shutdown

```cpp
MsQuicConnectionShutdown(
    Connection,
    QUIC_STATUS_NETWORK_INTERRUPT,
    0
    );
```

* Use non-zero status to indicate errors (e.g. `QUIC_STATUS_INTERNAL_ERROR`).
* Same sequence of events, but peers see an error code.

---

## 8. ConnectionClose

**Purpose:** Tear down the handle and release all resources.
**API:**

```cpp
MsQuicConnectionClose(
    _In_ HQUIC Connection
    );
```

* **Precondition:** Should only be called *after* `QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE`.
* **No callbacks** fire afterward.
* Frees all memory and stops any pending retries.

---

![Diagram](images/connections-sequence.png)

[🔍 View SVG](svg/connections-sequence.svg)  
[🧾 View Source (.puml)](diagrams/connections-sequence.puml)


