# MsQuic Objects and Callbacks Reference Guide

## Table of Contents
1. [Object Hierarchy](#object-hierarchy)
2. [Registration](#registration)
3. [Configuration](#configuration)
4. [Listener](#listener)
5. [Connection](#connection)
6. [Stream](#stream)
7. [Datagrams](#datagrams)
8. [TLS/Security Context](#tlssecurity-context)
9. [Dependencies and Lifecycle](#dependencies-and-lifecycle)
10. [Event Flow Diagrams](#event-flow-diagrams)

---

## Object Hierarchy

```
Registration (Process-level context)
├── Configuration (TLS/ALPN settings)
├── Listener (Server-side acceptor)
│   └── Connection[] (Accepted connections)
└── Connection (Client or accepted connection)
    ├── Stream[] (Bidirectional/Unidirectional data streams)
    ├── Datagrams (Unreliable messages)
    └── TLS Context (Security context)
```

---

## Registration

### Purpose
The Registration represents a process-level QUIC context that manages execution profiles, threading, and global settings.

### Creation
```cpp
QUIC_STATUS MsQuic->RegistrationOpen(
    const QUIC_REGISTRATION_CONFIG* Config,
    HQUIC* Registration
);
```

### Configuration Structure
```cpp
typedef struct QUIC_REGISTRATION_CONFIG {
    const char* AppName;                    // Application identifier
    QUIC_EXECUTION_PROFILE ExecutionProfile; // Performance profile
} QUIC_REGISTRATION_CONFIG;
```

### Execution Profiles
- `QUIC_EXECUTION_PROFILE_LOW_LATENCY` - Optimized for low latency (default)
- `QUIC_EXECUTION_PROFILE_TYPE_MAX_THROUGHPUT` - Optimized for throughput
- `QUIC_EXECUTION_PROFILE_TYPE_SCAVENGER` - Low priority background processing
- `QUIC_EXECUTION_PROFILE_TYPE_REAL_TIME` - Real-time applications

### Additional Functions
```cpp
// Shutdown all connections in registration
void MsQuic->RegistrationShutdown(
    HQUIC Registration,
    QUIC_CONNECTION_SHUTDOWN_FLAGS Flags,
    QUIC_UINT62 ErrorCode
);
```

### No Callbacks
Registration objects do not have callbacks.

### Dependencies
- **Required for**: All other MsQuic objects
- **Lifetime**: Must outlive all child objects
- **Thread Safety**: Thread-safe

### Cleanup
```cpp
void MsQuic->RegistrationClose(HQUIC Registration);
```

---

## Configuration

### Purpose
Configuration objects define connection parameters including TLS credentials, ALPN protocols, and QUIC settings.

### Creation
```cpp
QUIC_STATUS MsQuic->ConfigurationOpen(
    HQUIC Registration,
    const QUIC_BUFFER* AlpnBuffers,     // ALPN protocols (e.g., "h3")
    uint32_t AlpnBufferCount,
    const QUIC_SETTINGS* Settings,      // Optional QUIC settings
    uint32_t SettingsSize,
    void* Context,                      // Optional context
    HQUIC* Configuration
);
```

### Key Settings
```cpp
typedef struct QUIC_SETTINGS {
    uint64_t MaxBytesPerKey;           // Crypto key lifetime
    uint64_t HandshakeIdleTimeoutMs;   // Handshake timeout
    uint64_t IdleTimeoutMs;            // Connection idle timeout
    uint32_t StreamRecvWindowDefault;  // Default stream flow control
    uint32_t ConnFlowControlWindow;    // Connection flow control
    uint16_t PeerBidiStreamCount;      // Max peer bidirectional streams
    uint16_t PeerUnidiStreamCount;     // Max peer unidirectional streams
    uint8_t DatagramReceiveEnabled;    // Enable datagram reception
    // ... many more settings
} QUIC_SETTINGS;
```

### TLS Credential Loading
```cpp
QUIC_STATUS MsQuic->ConfigurationLoadCredential(
    HQUIC Configuration,
    const QUIC_CREDENTIAL_CONFIG* CredConfig
);
```

### Credential Types
- `QUIC_CREDENTIAL_TYPE_NONE` - No credentials (client, allows self-signed)
- `QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE` - Windows certificate store
- `QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE` - PEM/DER files
- `QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12` - PKCS#12 format

### No Callbacks
Configuration objects do not have callbacks.

### Dependencies
- **Requires**: Registration
- **Used by**: Listener, Connection
- **Lifetime**: Must outlive connections using it

### Cleanup
```cpp
void MsQuic->ConfigurationClose(HQUIC Configuration);
```

---

## Listener

### Purpose
Server-side object that accepts incoming QUIC connections.

### Creation
```cpp
QUIC_STATUS MsQuic->ListenerOpen(
    HQUIC Registration,
    QUIC_LISTENER_CALLBACK_HANDLER Handler,
    void* Context,
    HQUIC* Listener
);
```

### Starting Listener
```cpp
QUIC_STATUS MsQuic->ListenerStart(
    HQUIC Listener,
    const QUIC_BUFFER* AlpnBuffers,
    uint32_t AlpnBufferCount,
    const QUIC_ADDR* LocalAddress      // Can be NULL for any address
);
```

### Stopping Listener
```cpp
void MsQuic->ListenerStop(HQUIC Listener);
```

### Callback Function
```cpp
typedef QUIC_STATUS (QUIC_API QUIC_LISTENER_CALLBACK)(
    HQUIC Listener,
    void* Context,
    QUIC_LISTENER_EVENT* Event
);
```

### Event Types

#### `QUIC_LISTENER_EVENT_NEW_CONNECTION`
**Trigger**: Client initiates connection to listener
**Purpose**: Accept or reject incoming connections
**Data Available**:
```cpp
struct {
    const QUIC_NEW_CONNECTION_INFO* Info;  // Connection details
    HQUIC Connection;                      // New connection handle
} NEW_CONNECTION;

typedef struct QUIC_NEW_CONNECTION_INFO {
    uint32_t QuicVersion;               // QUIC version negotiated
    const QUIC_ADDR* LocalAddress;      // Server address
    const QUIC_ADDR* RemoteAddress;     // Client address
    uint32_t CryptoBufferLength;        // TLS handshake data length
    uint16_t ClientAlpnListLength;      // Client ALPN list length
    uint16_t ServerNameLength;          // SNI length
    uint8_t NegotiatedAlpnLength;       // Negotiated ALPN length
    const uint8_t* CryptoBuffer;        // TLS handshake data
    const uint8_t* ClientAlpnList;      // Client supported ALPNs
    const uint8_t* NegotiatedAlpn;      // Final negotiated ALPN
    const char* ServerName;             // SNI server name
} QUIC_NEW_CONNECTION_INFO;
```

**Required Actions**:
1. Set connection callback: `MsQuic->SetCallbackHandler(Connection, Callback, Context)`
2. Set configuration: `MsQuic->ConnectionSetConfiguration(Connection, Configuration)`
3. Return `QUIC_STATUS_SUCCESS` to accept or `QUIC_STATUS_CONNECTION_REFUSED` to reject

#### `QUIC_LISTENER_EVENT_STOP_COMPLETE`
**Trigger**: Listener stop operation completed
**Purpose**: Cleanup notification when listener fully stopped
**Data Available**: None

### Dependencies
- **Requires**: Registration
- **Creates**: Connection objects (via NEW_CONNECTION events)
- **Lifetime**: Can be stopped and restarted

### Cleanup
```cpp
void MsQuic->ListenerClose(HQUIC Listener);
```

---

## Connection

### Purpose
Represents a QUIC connection for reliable, ordered data delivery. Connections can be client-initiated or server-accepted.

### Creation

#### Client Connection
```cpp
QUIC_STATUS MsQuic->ConnectionOpen(
    HQUIC Registration,
    QUIC_CONNECTION_CALLBACK_HANDLER Handler,
    void* Context,
    HQUIC* Connection
);

QUIC_STATUS MsQuic->ConnectionStart(
    HQUIC Connection,
    HQUIC Configuration,
    QUIC_ADDRESS_FAMILY Family,         // Address family (IPv4/IPv6)
    const char* ServerName,             // Server hostname
    uint16_t ServerPort                 // Server port
);
```

#### Server Connection
Server connections are created automatically via `QUIC_LISTENER_EVENT_NEW_CONNECTION` events.

### Configuration
```cpp
QUIC_STATUS MsQuic->ConnectionSetConfiguration(
    HQUIC Connection,
    HQUIC Configuration
);
```

### Context Management
```cpp
void MsQuic->SetContext(HQUIC Handle, void* Context);
void* MsQuic->GetContext(HQUIC Handle);

void MsQuic->SetCallbackHandler(
    HQUIC Handle,
    void* Handler,
    void* Context
);
```

### Parameter Management
```cpp
QUIC_STATUS MsQuic->SetParam(
    HQUIC Handle,
    uint32_t Param,
    uint32_t BufferLength,
    const void* Buffer
);

QUIC_STATUS MsQuic->GetParam(
    HQUIC Handle,
    uint32_t Param,
    uint32_t* BufferLength,
    void* Buffer
);
```

### Callback Function
```cpp
typedef QUIC_STATUS (QUIC_API QUIC_CONNECTION_CALLBACK)(
    HQUIC Connection,
    void* Context,
    QUIC_CONNECTION_EVENT* Event
);
```

### Event Types

#### `QUIC_CONNECTION_EVENT_CONNECTED`
**Trigger**: Connection established successfully
**Purpose**: Connection ready for stream creation
**Data Available**:
```cpp
struct {
    BOOLEAN SessionResumed;             // TLS session was resumed
    uint8_t NegotiatedAlpnLength;       // Negotiated ALPN length
    const uint8_t* NegotiatedAlpn;      // Final ALPN protocol
} CONNECTED;
```

#### `QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT`
**Trigger**: Transport layer initiated shutdown
**Purpose**: Connection being closed due to protocol error
**Data Available**:
```cpp
struct {
    QUIC_STATUS Status;                 // Shutdown reason
    QUIC_UINT62 ErrorCode;              // Transport error code
} SHUTDOWN_INITIATED_BY_TRANSPORT;
```

#### `QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER`
**Trigger**: Peer initiated connection shutdown
**Purpose**: Remote peer is closing connection
**Data Available**:
```cpp
struct {
    QUIC_UINT62 ErrorCode;              // Application error code
} SHUTDOWN_INITIATED_BY_PEER;
```

#### `QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE`
**Trigger**: Connection shutdown completed
**Purpose**: Safe to clean up connection resources
**Data Available**:
```cpp
struct {
    BOOLEAN HandshakeCompleted;         // Handshake was completed
    BOOLEAN PeerAcknowledgedShutdown;   // Peer acked shutdown
    BOOLEAN AppCloseInProgress;         // App close in progress
} SHUTDOWN_COMPLETE;
```

#### `QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED`
**Trigger**: Peer created a new stream
**Purpose**: Accept or reject incoming stream
**Data Available**:
```cpp
struct {
    HQUIC Stream;                       // New stream handle
    QUIC_STREAM_OPEN_FLAGS Flags;       // Stream properties
} PEER_STREAM_STARTED;
```

**Required Actions**:
1. Set stream callback: `MsQuic->SetCallbackHandler(Stream, StreamCallback, Context)`
2. Optionally call `MsQuic->StreamReceiveSetEnabled(Stream, TRUE)` for receive events

#### `QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED`
**Trigger**: Unreliable datagram received
**Purpose**: Process datagram data
**Data Available**:
```cpp
struct {
    const QUIC_BUFFER* Buffer;          // Datagram data
    QUIC_RECEIVE_FLAGS Flags;           // Receive flags
} DATAGRAM_RECEIVED;
```

#### `QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED`
**Trigger**: Datagram send status updated
**Purpose**: Track datagram delivery
**Data Available**:
```cpp
struct {
    void* ClientContext;                // Context from send
    QUIC_DATAGRAM_SEND_STATE State;     // Send state
} DATAGRAM_SEND_STATE_CHANGED;
```

#### `QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED`
**Trigger**: TLS resumption ticket received (client)
**Purpose**: Store ticket for future session resumption
**Data Available**:
```cpp
struct {
    uint32_t ResumptionTicketLength;    // Ticket length
    const uint8_t* ResumptionTicket;    // Ticket data
} RESUMPTION_TICKET_RECEIVED;
```

#### `QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED`
**Trigger**: Peer certificate available for validation
**Purpose**: Custom certificate validation
**Data Available**:
```cpp
struct {
    QUIC_CERTIFICATE* Certificate;      // Certificate chain
    uint32_t DeferredErrorFlags;        // Deferred validation errors
    QUIC_STATUS DeferredStatus;         // Deferred validation status
} PEER_CERTIFICATE_RECEIVED;
```

**Required Actions**:
Must call `MsQuic->ConnectionCertificateValidationComplete(Connection, Result, TlsAlert)` to complete validation.

### Advanced Functions

#### Session Resumption
```cpp
QUIC_STATUS MsQuic->ConnectionSendResumptionTicket(
    HQUIC Connection,
    QUIC_SEND_RESUMPTION_FLAGS Flags,
    uint16_t DataLength,
    const uint8_t* ResumptionData
);

QUIC_STATUS MsQuic->ConnectionResumptionTicketValidationComplete(
    HQUIC Connection,
    BOOLEAN Result
);
```

#### Certificate Validation
```cpp
QUIC_STATUS MsQuic->ConnectionCertificateValidationComplete(
    HQUIC Connection,
    BOOLEAN Result,
    QUIC_TLS_ALERT_CODES TlsAlert
);
```

### Shutdown
```cpp
QUIC_STATUS MsQuic->ConnectionShutdown(
    HQUIC Connection,
    QUIC_CONNECTION_SHUTDOWN_FLAGS Flags,
    QUIC_UINT62 ErrorCode
);
```

### Dependencies
- **Requires**: Registration, Configuration (client) or Listener (server)
- **Creates**: Stream objects
- **Manages**: Datagram operations, TLS context

### Cleanup
```cpp
void MsQuic->ConnectionClose(HQUIC Connection);
```

---

## Stream

### Purpose
Represents a QUIC stream for reliable, ordered data delivery. Streams can be bidirectional or unidirectional.

### Creation
```cpp
QUIC_STATUS MsQuic->StreamOpen(
    HQUIC Connection,
    QUIC_STREAM_OPEN_FLAGS Flags,
    QUIC_STREAM_CALLBACK_HANDLER Handler,
    void* Context,
    HQUIC* Stream
);

QUIC_STATUS MsQuic->StreamStart(
    HQUIC Stream,
    QUIC_STREAM_START_FLAGS Flags
);
```

### Stream Flags
```cpp
// Open flags
QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL      // Create unidirectional stream
QUIC_STREAM_OPEN_FLAG_0_RTT               // Allow 0-RTT data
QUIC_STREAM_OPEN_FLAG_DELAY_ID_FC_UPDATES // Delay flow control updates

// Start flags
QUIC_STREAM_START_FLAG_IMMEDIATE          // Immediately notify peer
QUIC_STREAM_START_FLAG_FAIL_BLOCKED       // Fail if flow control blocks
QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL   // Shutdown on start failure
QUIC_STREAM_START_FLAG_INDICATE_PEER_ACCEPT // Indicate peer acceptance
```

### Callback Function
```cpp
typedef QUIC_STATUS (QUIC_API QUIC_STREAM_CALLBACK)(
    HQUIC Stream,
    void* Context,
    QUIC_STREAM_EVENT* Event
);
```

### Event Types

#### `QUIC_STREAM_EVENT_START_COMPLETE`
**Trigger**: Stream start operation completed
**Data Available**:
```cpp
struct {
    QUIC_STATUS Status;                 // Start result
    QUIC_UINT62 ID;                     // Stream ID assigned
    BOOLEAN PeerAccepted : 1;           // Peer accepted stream
    BOOLEAN RESERVED : 7;
} START_COMPLETE;
```

#### `QUIC_STREAM_EVENT_RECEIVE`
**Trigger**: Data received from peer
**Purpose**: Process incoming stream data
**Data Available**:
```cpp
struct {
    uint64_t AbsoluteOffset;            // Offset in stream
    uint64_t TotalBufferLength;         // Total data available
    const QUIC_BUFFER* Buffers;         // Data buffers
    uint32_t BufferCount;               // Number of buffers
    QUIC_RECEIVE_FLAGS Flags;           // Receive flags
} RECEIVE;

// QUIC_RECEIVE_FLAGS values:
// QUIC_RECEIVE_FLAG_0_RTT - Data encrypted with 0-RTT
// QUIC_RECEIVE_FLAG_FIN - FIN included with data
```

**Required Action**: Call `MsQuic->StreamReceiveComplete(Stream, BytesConsumed)`

#### `QUIC_STREAM_EVENT_SEND_COMPLETE`
**Trigger**: Send operation completed
**Data Available**:
```cpp
struct {
    BOOLEAN Canceled;                   // Send was canceled
    void* ClientContext;                // Context from send call
} SEND_COMPLETE;
```

#### `QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN`
**Trigger**: Peer closed send direction (sent FIN)
**Purpose**: No more data will be received
**Data Available**: None

#### `QUIC_STREAM_EVENT_PEER_SEND_ABORTED`
**Trigger**: Peer aborted send direction
**Purpose**: Stream reset by peer
**Data Available**:
```cpp
struct {
    QUIC_UINT62 ErrorCode;              // Peer's error code
} PEER_SEND_ABORTED;
```

#### `QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED`
**Trigger**: Peer aborted receive direction
**Purpose**: Peer will not accept more data
**Data Available**:
```cpp
struct {
    QUIC_UINT62 ErrorCode;              // Peer's error code
} PEER_RECEIVE_ABORTED;
```

#### `QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE`
**Trigger**: Send shutdown completed
**Data Available**:
```cpp
struct {
    BOOLEAN Graceful;                   // Was shutdown graceful
} SEND_SHUTDOWN_COMPLETE;
```

#### `QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE`
**Trigger**: Stream completely shutdown
**Purpose**: Safe to clean up stream resources
**Data Available**:
```cpp
struct {
    BOOLEAN ConnectionShutdown;         // Connection is shutting down
    BOOLEAN AppCloseInProgress : 1;     // App close in progress
    BOOLEAN ConnectionShutdownByApp : 1; // Shutdown by app
    BOOLEAN ConnectionClosedRemotely : 1; // Closed by peer
    BOOLEAN RESERVED : 5;
    QUIC_UINT62 ConnectionErrorCode;    // Connection error
    QUIC_STATUS ConnectionCloseStatus;  // Connection status
} SHUTDOWN_COMPLETE;
```

#### `QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE`
**Trigger**: Recommended send buffer size changed
**Purpose**: Optimize send performance
**Data Available**:
```cpp
struct {
    uint64_t ByteCount;                 // Recommended buffer size
} IDEAL_SEND_BUFFER_SIZE;
```

#### `QUIC_STREAM_EVENT_PEER_ACCEPTED`
**Trigger**: Peer accepted the stream
**Purpose**: Stream is now fully established

#### `QUIC_STREAM_EVENT_CANCEL_ON_LOSS`
**Trigger**: Stream data was lost and needs cancel code
**Data Available**:
```cpp
struct {
    QUIC_UINT62 ErrorCode;              // Error code to use for cancel
} CANCEL_ON_LOSS;
```

### Stream Operations

#### Sending Data
```cpp
QUIC_STATUS MsQuic->StreamSend(
    HQUIC Stream,
    const QUIC_BUFFER* Buffers,
    uint32_t BufferCount,
    QUIC_SEND_FLAGS Flags,
    void* ClientContext
);

// QUIC_SEND_FLAGS values:
// QUIC_SEND_FLAG_ALLOW_0_RTT - Allow 0-RTT encryption
// QUIC_SEND_FLAG_START - Start stream with this send
// QUIC_SEND_FLAG_FIN - Mark end of stream
// QUIC_SEND_FLAG_DGRAM_PRIORITY - High priority
// QUIC_SEND_FLAG_DELAY_SEND - Delay for batching
```

#### Flow Control
```cpp
void MsQuic->StreamReceiveComplete(
    HQUIC Stream,
    uint64_t BufferLength
);

QUIC_STATUS MsQuic->StreamReceiveSetEnabled(
    HQUIC Stream,
    BOOLEAN IsEnabled
);
```

#### Shutdown
```cpp
QUIC_STATUS MsQuic->StreamShutdown(
    HQUIC Stream,
    QUIC_STREAM_SHUTDOWN_FLAGS Flags,
    QUIC_UINT62 ErrorCode
);

// QUIC_STREAM_SHUTDOWN_FLAGS values:
// QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL - Graceful send shutdown (FIN)
// QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND - Abort send direction
// QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE - Abort receive direction
// QUIC_STREAM_SHUTDOWN_FLAG_ABORT - Abort both directions
// QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE - Immediate completion
```

### Dependencies
- **Requires**: Connection
- **Lifetime**: Must be closed before connection

### Cleanup
```cpp
void MsQuic->StreamClose(HQUIC Stream);
```

---

## Datagrams

### Purpose
Unreliable, unordered message delivery over QUIC connection. No callbacks - datagrams are handled via connection events.

### Configuration
Enable in QUIC_SETTINGS:
```cpp
QUIC_SETTINGS settings = {};
settings.IsSet.DatagramReceiveEnabled = TRUE;
settings.DatagramReceiveEnabled = TRUE;
```

### Sending
```cpp
QUIC_STATUS MsQuic->DatagramSend(
    HQUIC Connection,
    const QUIC_BUFFER* Buffers,
    uint32_t BufferCount,
    QUIC_SEND_FLAGS Flags,
    void* ClientContext
);
```

### Receiving
Handled via `QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED` connection event.

### Send Status Tracking
Tracked via `QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED` connection event.

### Dependencies
- **Requires**: Connection with datagram support enabled
- **No separate handle**: Uses connection handle

---

## TLS/Security Context

### Purpose
Manages TLS handshake, certificate validation, and encryption. No direct handle - managed internally.

### Configuration
Set via QUIC_CREDENTIAL_CONFIG in configuration:

```cpp
// Client (no certificate validation)
QUIC_CREDENTIAL_CONFIG credConfig = {};
credConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
credConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | 
                   QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

// Server (certificate from Windows store)
QUIC_CREDENTIAL_CONFIG credConfig = {};
credConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE;
credConfig.CertificateHashStore = &certHashStore;
```

### Certificate Validation
If `QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED` is set:
- Triggers `QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED`
- Allows custom certificate validation
- Use `MsQuic->ConnectionCertificateValidationComplete()` to complete

### Session Resumption
- Server: Triggered by `QUIC_CONNECTION_EVENT_RESUMED`
- Client: Triggered by `QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED`
- Use `MsQuic->ConnectionSendResumptionTicket()` (server) to send tickets

### TLS Secrets (Debugging)
Available via `QUIC_PARAM_CONN_TLS_SECRETS` parameter for Wireshark decryption.

### Dependencies
- **Managed by**: Configuration and Connection
- **No direct handle**: Internal to connection

---

## Dependencies and Lifecycle

### Creation Order
1. **MsQuicOpen2()** - Initialize MsQuic library and get API table
2. **Registration** - Create first, process-wide context
3. **Configuration** - Configure TLS, ALPN, settings
4. **Listener** (server) OR **Connection** (client) 
5. **Connection** (server: via listener events)
6. **Stream** - Created on demand

### Destruction Order (Reverse)
1. **Stream** - Close all streams first
2. **Connection** - Close connections
3. **Listener** - Stop and close listener
4. **Configuration** - Close configuration
5. **Registration** - Close registration
6. **MsQuicClose()** - Close library

### Key API Table Functions
```cpp
typedef struct QUIC_API_TABLE {
    QUIC_SET_CONTEXT_FN                 SetContext;
    QUIC_GET_CONTEXT_FN                 GetContext;
    QUIC_SET_CALLBACK_HANDLER_FN        SetCallbackHandler;

    QUIC_SET_PARAM_FN                   SetParam;
    QUIC_GET_PARAM_FN                   GetParam;

    QUIC_REGISTRATION_OPEN_FN           RegistrationOpen;
    QUIC_REGISTRATION_CLOSE_FN          RegistrationClose;
    QUIC_REGISTRATION_SHUTDOWN_FN       RegistrationShutdown;

    QUIC_CONFIGURATION_OPEN_FN          ConfigurationOpen;
    QUIC_CONFIGURATION_CLOSE_FN         ConfigurationClose;
    QUIC_CONFIGURATION_LOAD_CREDENTIAL_FN ConfigurationLoadCredential;

    QUIC_LISTENER_OPEN_FN               ListenerOpen;
    QUIC_LISTENER_CLOSE_FN              ListenerClose;
    QUIC_LISTENER_START_FN              ListenerStart;
    QUIC_LISTENER_STOP_FN               ListenerStop;

    QUIC_CONNECTION_OPEN_FN             ConnectionOpen;
    QUIC_CONNECTION_CLOSE_FN            ConnectionClose;
    QUIC_CONNECTION_SHUTDOWN_FN         ConnectionShutdown;
    QUIC_CONNECTION_START_FN            ConnectionStart;
    QUIC_CONNECTION_SET_CONFIGURATION_FN ConnectionSetConfiguration;
    QUIC_CONNECTION_SEND_RESUMPTION_FN  ConnectionSendResumptionTicket;

    QUIC_STREAM_OPEN_FN                 StreamOpen;
    QUIC_STREAM_CLOSE_FN                StreamClose;
    QUIC_STREAM_START_FN                StreamStart;
    QUIC_STREAM_SHUTDOWN_FN             StreamShutdown;
    QUIC_STREAM_SEND_FN                 StreamSend;
    QUIC_STREAM_RECEIVE_COMPLETE_FN     StreamReceiveComplete;
    QUIC_STREAM_RECEIVE_SET_ENABLED_FN  StreamReceiveSetEnabled;

    QUIC_DATAGRAM_SEND_FN               DatagramSend;

    QUIC_CONNECTION_COMP_RESUMPTION_FN  ConnectionResumptionTicketValidationComplete;
    QUIC_CONNECTION_COMP_CERT_FN        ConnectionCertificateValidationComplete;
} QUIC_API_TABLE;
```

---

## Event Flow Diagrams

### Client Connection Flow
```
1. MsQuicOpen2() → API Table
2. RegistrationOpen() → Registration Handle
3. ConfigurationOpen() → Configuration Handle
4. ConfigurationLoadCredential() → TLS Setup
5. ConnectionOpen() → Connection Handle
6. SetCallbackHandler() → Connection callback set
7. ConnectionStart() → Connecting
8. CONNECTION_CONNECTED → Ready for streams
9. StreamOpen() → Stream Handle
10. SetCallbackHandler() → Stream callback set
11. StreamStart() → Stream ready
12. StreamSend() → SEND_COMPLETE
13. RECEIVE → Data from peer
14. StreamShutdown() → SHUTDOWN_COMPLETE
15. StreamClose() → Stream destroyed
16. ConnectionShutdown() → SHUTDOWN_COMPLETE
17. ConnectionClose() → Connection destroyed
```

### Server Connection Flow
```
1. MsQuicOpen2() → API Table
2. RegistrationOpen() → Registration Handle
3. ConfigurationOpen() → Configuration Handle
4. ConfigurationLoadCredential() → TLS Setup
5. ListenerOpen() → Listener Handle
6. SetCallbackHandler() → Listener callback set
7. ListenerStart() → Listening
8. NEW_CONNECTION → New connection available
9. SetCallbackHandler() → Connection callback set
10. ConnectionSetConfiguration() → Connection configured
11. CONNECTION_CONNECTED → Ready for streams
12. PEER_STREAM_STARTED → Peer created stream
13. SetCallbackHandler() → Stream callback set
14. StreamReceiveSetEnabled() → Enable receive events
15. RECEIVE → Data from peer
16. StreamSend() → Send response
17. [Continue until shutdown...]
```

### WebTransport Flow (HTTP/3 over QUIC)
```
1. Connection established (QUIC handshake complete)
2. Create control stream (unidirectional, ID 2)
3. Send SETTINGS frame with ENABLE_WEBTRANSPORT=1
4. Create bidirectional stream for CONNECT (ID 0)
5. Send HEADERS frame with WebTransport CONNECT request
6. Receive 200 OK response from server
7. WebTransport session established
8. Create additional streams/send datagrams for application data
```

### Core Event Processing Flow
```
1. Network packet received → MsQuic Core
2. Packet parsing and frame processing
3. State updates and validation
4. Event generation based on frame type
5. Callback invocation with event data
6. Application processing in callback
7. Return status to MsQuic Core
8. Continue operation or handle errors
```

### Listener Callback Processing
```
1. Incoming connection attempt
2. Extract connection information (ALPN, SNI, addresses)
3. Validate connection request
4. Accept/reject decision logic
5. If accepted: SetCallbackHandler() + ConnectionSetConfiguration()
6. Return status (SUCCESS = accept, CONNECTION_REFUSED = reject)
7. Connection proceeds to CONNECTED state
```

### Connection Callback Processing
```
1. Receive event from MsQuic Core
2. Determine event type (CONNECTED, PEER_STREAM_STARTED, etc.)
3. Route to appropriate processor
4. Extract relevant data from event structure
5. Perform application-specific logic
6. For streams: SetCallbackHandler() if new stream
7. Return status to continue operation
```

### Stream Callback Processing
```
1. Receive stream event from MsQuic Core
2. Process based on event type:
   - RECEIVE: Process data + StreamReceiveComplete()
   - SEND_COMPLETE: Handle completion status
   - SHUTDOWN events: Cleanup resources
3. Manage flow control and state
4. Return status to MsQuic
```

### Error Handling Flow
```
1. Error detected (network, protocol, or application)
2. Generate appropriate error event
3. Propagate to application via callbacks
4. Application decides recovery strategy:
   - Continue operation (ignore/retry)
   - Graceful shutdown
   - Immediate abort
5. Cleanup resources as needed
6. Update connection/stream state
```

This comprehensive reference covers all the MsQuic API functions and events referenced in the PlantUML diagrams while maintaining the general structure and readability of the original objects guide. The document now includes all the core functions like MsQuicOpen2, SetCallbackHandler, SetContext, GetContext, SetParam, GetParam, and the advanced functions like ConnectionResumptionTicketValidationComplete and ConnectionCertificateValidationComplete that are used in real WebTransport and HTTP/3 implementations.