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
3. Return `QUIC_STATUS_SUCCESS` to accept, or error to reject

#### `QUIC_LISTENER_EVENT_STOP_COMPLETE`
**Trigger**: Listener stop operation completed
**Purpose**: Cleanup notification
**Data Available**:
```cpp
struct {
    BOOLEAN AppCloseInProgress : 1;     // Application initiated close
    BOOLEAN RESERVED : 7;
} STOP_COMPLETE;
```

### Dependencies
- **Requires**: Registration, Configuration (for accepted connections)
- **Creates**: Connection objects via NEW_CONNECTION events
- **Lifetime**: Independent of created connections

### Stopping and Cleanup
```cpp
void MsQuic->ListenerStop(HQUIC Listener);    // Async stop
void MsQuic->ListenerClose(HQUIC Listener);   // Final cleanup
```

---

## Connection

### Purpose
Represents a QUIC connection between client and server, managing streams, datagrams, and connection-level operations.

### Creation

#### Client-side
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
    QUIC_ADDRESS_FAMILY Family,
    const char* ServerName,
    uint16_t ServerPort
);
```

#### Server-side
Created automatically via `QUIC_LISTENER_EVENT_NEW_CONNECTION`

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
**Trigger**: Connection handshake completed successfully
**Purpose**: Connection is ready for streams and datagrams
**Data Available**:
```cpp
struct {
    BOOLEAN SessionResumed;             // TLS session was resumed
    uint8_t NegotiatedAlpnLength;       // Length of negotiated ALPN
    const uint8_t* NegotiatedAlpn;      // Final ALPN protocol
} CONNECTED;
```

**Typical Actions**:
- Create initial streams
- Send initial application data
- Set up application state

#### `QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT`
**Trigger**: QUIC transport layer initiated shutdown (protocol error, etc.)
**Purpose**: Notification of transport-level connection termination
**Data Available**:
```cpp
struct {
    QUIC_STATUS Status;                 // Reason for shutdown
    QUIC_UINT62 ErrorCode;              // QUIC error code (wire format)
} SHUTDOWN_INITIATED_BY_TRANSPORT;
```

#### `QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER`
**Trigger**: Remote peer initiated graceful connection shutdown
**Purpose**: Peer is closing the connection
**Data Available**:
```cpp
struct {
    QUIC_UINT62 ErrorCode;              // Application error code from peer
} SHUTDOWN_INITIATED_BY_PEER;
```

#### `QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE`
**Trigger**: Connection shutdown process completed
**Purpose**: Connection is fully closed and can be cleaned up
**Data Available**:
```cpp
struct {
    BOOLEAN HandshakeCompleted : 1;      // Handshake finished before shutdown
    BOOLEAN PeerAcknowledgedShutdown : 1; // Peer confirmed shutdown
    BOOLEAN AppCloseInProgress : 1;      // App initiated close
} SHUTDOWN_COMPLETE;
```

**Required Action**: Call `MsQuic->ConnectionClose(Connection)`

#### `QUIC_CONNECTION_EVENT_LOCAL_ADDRESS_CHANGED`
**Trigger**: Local address changed (e.g., interface change, NAT rebind)
**Data Available**:
```cpp
struct {
    const QUIC_ADDR* Address;           // New local address
} LOCAL_ADDRESS_CHANGED;
```

#### `QUIC_CONNECTION_EVENT_PEER_ADDRESS_CHANGED`
**Trigger**: Peer address changed (connection migration)
**Data Available**:
```cpp
struct {
    const QUIC_ADDR* Address;           // New peer address
} PEER_ADDRESS_CHANGED;
```

#### `QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED`
**Trigger**: Peer opened a new stream
**Purpose**: Accept or reject the new stream
**Data Available**:
```cpp
struct {
    HQUIC Stream;                       // New stream handle
    QUIC_STREAM_OPEN_FLAGS Flags;       // Stream characteristics
} PEER_STREAM_STARTED;

// QUIC_STREAM_OPEN_FLAGS values:
// QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL - Stream is unidirectional
// QUIC_STREAM_OPEN_FLAG_0_RTT - Stream opened in 0-RTT data
```

**Required Actions**:
1. Set stream callback: `MsQuic->SetCallbackHandler(Stream, StreamCallback, Context)`
2. Optionally enable receive: `MsQuic->StreamReceiveSetEnabled(Stream, TRUE)`

#### `QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE`
**Trigger**: Flow control updated, more streams can be created
**Data Available**:
```cpp
struct {
    uint16_t BidirectionalCount;        // Available bidirectional streams
    uint16_t UnidirectionalCount;       // Available unidirectional streams
} STREAMS_AVAILABLE;
```

#### `QUIC_CONNECTION_EVENT_PEER_NEEDS_STREAMS`
**Trigger**: Peer needs more stream capacity
**Data Available**:
```cpp
struct {
    BOOLEAN Bidirectional;              // Type of streams needed
} PEER_NEEDS_STREAMS;
```

#### `QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED`
**Trigger**: Optimal processor for this connection changed
**Data Available**:
```cpp
struct {
    uint16_t IdealProcessor;            // Recommended processor
    uint16_t PartitionIndex;            // Partition index
} IDEAL_PROCESSOR_CHANGED;
```

#### `QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED`
**Trigger**: Datagram send capability changed
**Data Available**:
```cpp
struct {
    BOOLEAN SendEnabled;                // Can send datagrams
    uint16_t MaxSendLength;             // Maximum datagram size
} DATAGRAM_STATE_CHANGED;
```

#### `QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED`
**Trigger**: Unreliable datagram received from peer
**Data Available**:
```cpp
struct {
    const QUIC_BUFFER* Buffer;          // Datagram data
    QUIC_RECEIVE_FLAGS Flags;           // Receive flags (0-RTT, etc.)
} DATAGRAM_RECEIVED;
```

#### `QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED`
**Trigger**: Datagram send operation completed or failed
**Data Available**:
```cpp
struct {
    void* ClientContext;                // Context from send call
    QUIC_DATAGRAM_SEND_STATE State;     // Final state
} DATAGRAM_SEND_STATE_CHANGED;

// QUIC_DATAGRAM_SEND_STATE values:
// QUIC_DATAGRAM_SEND_SENT - Sent and awaiting ACK
// QUIC_DATAGRAM_SEND_LOST_SUSPECT - Possibly lost
// QUIC_DATAGRAM_SEND_LOST_DISCARDED - Confirmed lost
// QUIC_DATAGRAM_SEND_ACKNOWLEDGED - Successfully received
// QUIC_DATAGRAM_SEND_CANCELED - Canceled before send
```

#### `QUIC_CONNECTION_EVENT_RESUMED`
**Trigger**: TLS session resumption information available (server only)
**Data Available**:
```cpp
struct {
    uint16_t ResumptionStateLength;     // State data length
    const uint8_t* ResumptionState;     // Application resumption data
} RESUMED;
```

#### `QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED`
**Trigger**: TLS resumption ticket received (client only)
**Data Available**:
```cpp
struct {
    uint32_t ResumptionTicketLength;    // Ticket length
    const uint8_t* ResumptionTicket;    // Ticket data to persist
} RESUMPTION_TICKET_RECEIVED;
```

#### `QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED`
**Trigger**: Peer certificate available for validation (if enabled)
**Data Available**:
```cpp
struct {
    QUIC_CERTIFICATE* Certificate;      // Platform-specific certificate
    uint32_t DeferredErrorFlags;        // Validation error flags
    QUIC_STATUS DeferredStatus;         // Validation status
    QUIC_CERTIFICATE_CHAIN* Chain;      // Certificate chain
} PEER_CERTIFICATE_RECEIVED;
```

### Connection Operations

#### Creating Streams
```cpp
QUIC_STATUS MsQuic->StreamOpen(
    HQUIC Connection,
    QUIC_STREAM_OPEN_FLAGS Flags,
    QUIC_STREAM_CALLBACK_HANDLER Handler,
    void* Context,
    HQUIC* Stream
);
```

#### Sending Datagrams
```cpp
QUIC_STATUS MsQuic->DatagramSend(
    HQUIC Connection,
    const QUIC_BUFFER* Buffers,
    uint32_t BufferCount,
    QUIC_SEND_FLAGS Flags,
    void* ClientContext
);
```

#### Shutdown
```cpp
void MsQuic->ConnectionShutdown(
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
QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL    // Create unidirectional stream
QUIC_STREAM_OPEN_FLAG_0_RTT             // Allow 0-RTT data
QUIC_STREAM_OPEN_FLAG_DELAY_ID_FC_UPDATES // Delay flow control updates

// Start flags  
QUIC_STREAM_START_FLAG_IMMEDIATE         // Immediately notify peer
QUIC_STREAM_START_FLAG_FAIL_BLOCKED      // Fail if flow control blocks
QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL  // Shutdown on start failure
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
**Trigger**: Peer finished sending (sent FIN)
**Purpose**: Peer has no more data to send
**No additional data**

#### `QUIC_STREAM_EVENT_PEER_SEND_ABORTED`
**Trigger**: Peer aborted sending
**Data Available**:
```cpp
struct {
    QUIC_UINT62 ErrorCode;              // Peer's abort error code
} PEER_SEND_ABORTED;
```

#### `QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED`
**Trigger**: Peer aborted receiving
**Data Available**:
```cpp
struct {
    QUIC_UINT62 ErrorCode;              // Peer's abort error code
} PEER_RECEIVE_ABORTED;
```

#### `QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE`
**Trigger**: Local send shutdown completed
**Data Available**:
```cpp
struct {
    BOOLEAN Graceful;                   // Graceful shutdown (FIN sent)
} SEND_SHUTDOWN_COMPLETE;
```

#### `QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE`
**Trigger**: Stream completely shut down
**Purpose**: Stream can be cleaned up
**Data Available**:
```cpp
struct {
    BOOLEAN ConnectionShutdown;         // Connection is shutting down
    BOOLEAN AppCloseInProgress : 1;     // App initiated close
    BOOLEAN ConnectionShutdownByApp : 1; // App shut down connection
    BOOLEAN ConnectionClosedRemotely : 1; // Remote peer closed connection
    BOOLEAN RESERVED : 5;
    QUIC_UINT62 ConnectionErrorCode;    // Connection error code
    QUIC_STATUS ConnectionCloseStatus;  // Connection close status
} SHUTDOWN_COMPLETE;
```

**Required Action**: Call `MsQuic->StreamClose(Stream)`

#### `QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE`
**Trigger**: Recommended send buffer size changed
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
1. **Registration** - Create first, process-wide context
2. **Configuration** - Configure TLS, ALPN, settings
3. **Listener** (server) OR **Connection** (client) 
4. **Connection** (server: via listener events)
5. **Stream** - Created on demand

### Destruction Order (Reverse)
1. **Stream** - Close all streams first
2. **Connection** - Close connections
3. **Listener** - Stop and close listener
4. **Configuration** - Close configuration
5. **Registration** - Close last

### Dependency Matrix

| Object | Requires | Creates | Used By |
|--------|----------|---------|---------|
| Registration | None | None | Configuration, Listener, Connection |
| Configuration | Registration | None | Listener, Connection |
| Listener | Registration | Connection | None |
| Connection | Registration, Configuration | Stream | Stream |
| Stream | Connection | None | None |

### Callback Dependency Chain
```
Listener Callback
    ├─→ Creates Connection
    │   ├─→ Connection Callback
    │   │   ├─→ CONNECTED → Can create streams
    │   │   ├─→ PEER_STREAM_STARTED → Creates stream
    │   │   │   └─→ Stream Callback
    │   │   │       ├─→ RECEIVE → Process data
    │   │   │       ├─→ SEND_COMPLETE → Send more data
    │   │   │       └─→ SHUTDOWN_COMPLETE → Close stream
    │   │   ├─→ DATAGRAM_RECEIVED → Process datagram
    │   │   └─→ SHUTDOWN_COMPLETE → Close connection
    │   └─→ Set Configuration
    └─→ Set Connection Callback
```

### Thread Safety Notes
- **Registration**: Thread-safe
- **Configuration**: Thread-safe (immutable after credential load)
- **Listener**: Thread-safe
- **Connection**: Callbacks serialized per connection
- **Stream**: Callbacks serialized per stream
- **Cross-object**: Not thread-safe (don't share handles across threads)

---

## Event Flow Diagrams

### Client Connection Flow
```
1. ConnectionOpen() → Connection Handle
2. ConnectionStart() → Connecting
3. CONNECTION_CONNECTED → Ready for streams
4. StreamOpen() → Stream Handle  
5. StreamStart() → Stream ready
6. StreamSend() → SEND_COMPLETE
7. RECEIVE → Data from peer
8. StreamShutdown() → SHUTDOWN_COMPLETE
9. StreamClose() → Stream destroyed
10. ConnectionShutdown() → SHUTDOWN_COMPLETE
11. ConnectionClose() → Connection destroyed
```

### Server Connection Flow
```
1. ListenerOpen() → Listener Handle
2. ListenerStart() → Listening
3. NEW_CONNECTION → New connection available
4. SetCallbackHandler() → Connection callback set
5. ConnectionSetConfiguration() → Connection configured
6. CONNECTION_CONNECTED → Ready for streams
7. PEER_STREAM_STARTED → Peer created stream
8. SetCallbackHandler() → Stream callback set
9. RECEIVE → Data from peer
10. StreamSend() → Send response
11. [Continue until shutdown...]
```

### WebTransport Flow (HTTP/3 over QUIC)
```
1. Connection established
2. Create control stream (unidirectional)
3. Send SETTINGS frame with ENABLE_WEBTRANSPORT
4. Create bidirectional stream for CONNECT
5. Send HEADERS frame with WebTransport CONNECT
6. Receive 200 OK response
7. WebTransport session established
8. Create additional streams/send datagrams
```

This reference should help you understand the complete MsQuic object model, when callbacks are triggered, what data is available, and how objects depend on each other throughout their lifecycle.