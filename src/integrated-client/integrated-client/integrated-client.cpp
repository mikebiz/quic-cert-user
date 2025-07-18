// integrated_client.cpp - MsQuic Client with HTTP/3 WebTransport Support
// Compatible with MsQuic NuGet package 2.4.10
#include <msquic.h>
#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <format>
#include <cstring>
#include <chrono>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "msquic.lib")
#pragma comment(lib, "Ws2_32.lib")

// QPACK Static Table (subset for WebTransport)
struct QpackStaticEntry {
    std::string_view name;
    std::string_view value;
};

constexpr std::array<QpackStaticEntry, 99> QPACK_STATIC_TABLE = { {
    {"", ""},                                    // 0 - unused
    {":authority", ""},                          // 1
    {":path", "/"},                             // 2
    {":path", "/index.html"},                   // 3
    {":path", "/index.htm"},                    // 4
    {":method", "CONNECT"},                     // 5  <- Important for WebTransport
    {":method", "DELETE"},                      // 6
    {":method", "GET"},                         // 7
    {":method", "HEAD"},                        // 8
    {":method", "OPTIONS"},                     // 9
    {":method", "POST"},                        // 10
    {":method", "PUT"},                         // 11
    {":scheme", "http"},                        // 12
    {":scheme", "https"},                       // 13
    {":status", "103"},                         // 14
    {":status", "200"},                         // 15
    {":status", "304"},                         // 16
    {":status", "404"},                         // 17
    {":status", "503"},                         // 18
    {":status", "100"},                         // 19
    {":status", "204"},                         // 20
    {":status", "206"},                         // 21
    {":status", "300"},                         // 22
    {":status", "400"},                         // 23
    {":status", "403"},                         // 24
    {":status", "421"},                         // 25
    {":status", "425"},                         // 26
    {":status", "500"},                         // 27
    {"accept-charset", ""},                     // 28
    {"accept-encoding", "gzip, deflate, br"},  // 29
    {"accept-language", ""},                    // 30
    {"accept-ranges", ""},                      // 31
    {"accept", ""},                             // 32
    {"access-control-allow-headers", ""},       // 33
    {"access-control-allow-methods", ""},       // 34
    {"access-control-allow-origin", ""},        // 35
    {"age", ""},                                // 36
    {"allow", ""},                              // 37
    {"authorization", ""},                      // 38
    {"cache-control", ""},                      // 39
    {"content-disposition", ""},                // 40
    {"content-encoding", ""},                   // 41
    {"content-language", ""},                   // 42
    {"content-length", ""},                     // 43
    {"content-location", ""},                   // 44
    {"content-range", ""},                      // 45
    {"content-type", ""},                       // 46
    {"cookie", ""},                             // 47
    {"date", ""},                               // 48
    {"etag", ""},                               // 49
    {"expect", ""},                             // 50
    {"expires", ""},                            // 51
    {"from", ""},                               // 52
    {"host", ""},                               // 53
    {"if-match", ""},                           // 54
    {"if-modified-since", ""},                  // 55
    {"if-none-match", ""},                      // 56
    {"if-range", ""},                           // 57
    {"if-unmodified-since", ""},                // 58
    {"last-modified", ""},                      // 59
    {"link", ""},                               // 60
    {"location", ""},                           // 61
    {"max-forwards", ""},                       // 62
    {"proxy-authenticate", ""},                 // 63
    {"proxy-authorization", ""},                // 64
    {"range", ""},                              // 65
    {"referer", ""},                            // 66
    {"refresh", ""},                            // 67
    {"retry-after", ""},                        // 68
    {"server", ""},                             // 69
    {"set-cookie", ""},                         // 70
    {"strict-transport-security", ""},          // 71
    {"transfer-encoding", ""},                  // 72
    {"user-agent", ""},                         // 73
    {"vary", ""},                               // 74
    {"via", ""},                                // 75
    {"www-authenticate", ""},                   // 76
    {"accept-encoding", "gzip, deflate"},      // 77
    {"accept-language", "en"},                  // 78
    {"cache-control", "max-age=0"},             // 79
    {"cache-control", "no-cache"},              // 80
    {"content-encoding", "br"},                 // 81
    {"content-encoding", "gzip"},               // 82
    {"content-type", "application/dns-message"}, // 83
    {"content-type", "application/javascript"}, // 84
    {"content-type", "application/json"},       // 85
    {"content-type", "application/octet-stream"}, // 86
    {"content-type", "text/css"},               // 87
    {"content-type", "text/html; charset=utf-8"}, // 88
    {"content-type", "text/plain"},             // 89
    {"content-type", "text/plain;charset=utf-8"}, // 90
    {"range", "bytes=0-"},                      // 91
    {"strict-transport-security", "max-age=31536000"}, // 92
    {"strict-transport-security", "max-age=31536000; includesubdomains"}, // 93
    {"strict-transport-security", "max-age=31536000; includesubdomains; preload"}, // 94
    {"vary", "accept-encoding"},                // 95
    {"vary", "origin"},                         // 96
    {"x-content-type-options", "nosniff"},      // 97
    {"x-xss-protection", "1; mode=block"},      // 98
} };

class QpackEncoder {
private:
    std::vector<uint8_t> buffer;

    void encodeInteger(uint64_t value, uint8_t prefix_bits, uint8_t prefix_pattern = 0) {
        uint64_t max_prefix = (1ULL << prefix_bits) - 1;

        if (value < max_prefix) {
            buffer.push_back(static_cast<uint8_t>(prefix_pattern | value));
        }
        else {
            buffer.push_back(static_cast<uint8_t>(prefix_pattern | max_prefix));
            value -= max_prefix;

            while (value >= 128) {
                buffer.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            buffer.push_back(static_cast<uint8_t>(value));
        }
    }

    void encodeString(std::string_view str, bool huffman = false) {
        // For simplicity, we'll skip Huffman encoding
        encodeInteger(str.length(), 7, huffman ? 0x80 : 0x00);
        buffer.insert(buffer.end(), str.begin(), str.end());
    }

    int findStaticTableIndex(std::string_view name, std::string_view value) {
        for (size_t i = 1; i < QPACK_STATIC_TABLE.size(); ++i) {
            if (QPACK_STATIC_TABLE[i].name == name && QPACK_STATIC_TABLE[i].value == value) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int findStaticTableNameIndex(std::string_view name) {
        for (size_t i = 1; i < QPACK_STATIC_TABLE.size(); ++i) {
            if (QPACK_STATIC_TABLE[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

public:
    void clear() { buffer.clear(); }

    void encodeHeader(std::string_view name, std::string_view value) {
        int exact_match = findStaticTableIndex(name, value);
        if (exact_match >= 0) {
            // Static table reference with exact match
            encodeInteger(exact_match, 6, 0x80);  // 10xxxxxx pattern
            return;
        }

        int name_match = findStaticTableNameIndex(name);
        if (name_match >= 0) {
            // Static table name reference with literal value
            encodeInteger(name_match, 6, 0x40);  // 01xxxxxx pattern
            encodeString(value);
        }
        else {
            // Literal name and value
            buffer.push_back(0x20);  // 001xxxxx pattern (literal with incremental indexing)
            encodeString(name);
            encodeString(value);
        }
    }

    std::vector<uint8_t> getEncoded() const { return buffer; }
};

class Http3FrameBuilder {
public:
    enum Type : uint8_t {
        DATA = 0x00,
        HEADERS = 0x01,
        SETTINGS = 0x04,
        MAX_PUSH_ID = 0x0D,
        WEBTRANSPORT_STREAM = 0x41
    };

    static std::vector<uint8_t> createSettingsFrame() {
        std::vector<uint8_t> frame;

        // Frame type
        frame.push_back(SETTINGS);

        // Settings payload
        std::vector<uint8_t> payload;

        // ENABLE_WEBTRANSPORT setting (0x2b603742 = 1)
        payload.push_back(0x2b);
        payload.push_back(0x60);
        payload.push_back(0x37);
        payload.push_back(0x42);
        payload.push_back(0x01);

        // MAX_FIELD_SECTION_SIZE (optional, recommended)
        payload.push_back(0x06);
        payload.push_back(0x80);
        payload.push_back(0x80);
        payload.push_back(0x01);

        // Frame length
        encodeVarint(frame, payload.size());

        // Append payload
        frame.insert(frame.end(), payload.begin(), payload.end());

        return frame;
    }

    static std::vector<uint8_t> createHeadersFrame(const std::vector<uint8_t>& qpackData) {
        std::vector<uint8_t> frame;

        // Frame type
        frame.push_back(HEADERS);

        // Frame length
        encodeVarint(frame, qpackData.size());

        // QPACK encoded headers
        frame.insert(frame.end(), qpackData.begin(), qpackData.end());

        return frame;
    }

    static std::vector<uint8_t> createMaxPushIdFrame() {
        std::vector<uint8_t> frame;

        // Frame type
        frame.push_back(MAX_PUSH_ID);

        // Frame length (1 byte for push ID = 0)
        frame.push_back(0x01);

        // Push ID = 0
        frame.push_back(0x00);

        return frame;
    }

private:
    static void encodeVarint(std::vector<uint8_t>& buffer, uint64_t value) {
        if (value < 64) {
            buffer.push_back(static_cast<uint8_t>(value));
        }
        else if (value < 16384) {
            buffer.push_back(static_cast<uint8_t>(0x40 | (value >> 8)));
            buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        }
        else {
            // For larger values, use full varint encoding
            while (value >= 128) {
                buffer.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            buffer.push_back(static_cast<uint8_t>(value));
        }
    }
};

// Global variables for MsQuic
const QUIC_API_TABLE* MsQuic = nullptr;
HQUIC Registration = nullptr;
HQUIC Configuration = nullptr;
HQUIC Connection = nullptr;
HQUIC ControlStream = nullptr;
HQUIC ConnectStream = nullptr;

bool WebTransportEstablished = false;

// Forward declarations with proper MsQuic 2.4.10 annotations
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
ClientStreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event
);

_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ClientConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event
);

static void DescribeQuicStatus(QUIC_STATUS status, const std::string& message) {
    std::cerr << message << " (QUIC_STATUS: 0x" << std::hex << status << ")\n";
}

static void SendSettingsFrame(HQUIC connection) {
    std::cout << "[Client] Sending SETTINGS frame on control stream\n";

    // Create HTTP/3 control stream data
    std::vector<uint8_t> controlStreamData;

    // FIRST: Add control stream type identifier (0x00)
    controlStreamData.push_back(0x00);

    // THEN: Add the SETTINGS frame
    auto settingsFrame = Http3FrameBuilder::createSettingsFrame();
    controlStreamData.insert(controlStreamData.end(), settingsFrame.begin(), settingsFrame.end());

    // Debug output
    std::cout << "[Client] Control stream data (" << controlStreamData.size() << " bytes): ";
    for (size_t i = 0; i < controlStreamData.size(); ++i) {
        std::cout << std::format("{:02x} ", static_cast<int>(controlStreamData[i]));
    }
    std::cout << "\n";

    // Rest of your existing stream creation code...
    QUIC_STATUS status = MsQuic->StreamOpen(
        connection,
        QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL,
        ClientStreamCallback,
        connection,
        &ControlStream
    );

    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "[Client] Failed to open control stream");
        return;
    }

    status = MsQuic->StreamStart(ControlStream, QUIC_STREAM_START_FLAG_IMMEDIATE);
    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "[Client] Failed to start control stream");
        return;
    }

    QUIC_BUFFER controlBuf = {};
    controlBuf.Buffer = controlStreamData.data();
    controlBuf.Length = static_cast<uint32_t>(controlStreamData.size());

    status = MsQuic->StreamSend(ControlStream, &controlBuf, 1, QUIC_SEND_FLAG_FIN, nullptr);
    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "[Client] Failed to send control stream data");
    }

    std::cout << "[Client] Control stream sent (" << controlStreamData.size() << " bytes)\n";
}

static void SendWebTransportConnect(HQUIC connection, const std::string& host, const std::string& path) {
    std::cout << "[Client] Sending WebTransport CONNECT request\n";

    // Build QPACK encoded headers for WebTransport CONNECT
    QpackEncoder encoder;
    encoder.encodeHeader(":method", "CONNECT");
    encoder.encodeHeader(":protocol", "webtransport");
    encoder.encodeHeader(":scheme", "https");
    encoder.encodeHeader(":authority", host);
    encoder.encodeHeader(":path", path);

    auto qpackData = encoder.getEncoded();
//    auto headersFrame = Http3FrameBuilder::createHeadersFrame(qpackData);
    // Make headersFrame static or ensure it stays in scope
    static std::vector<uint8_t> headersFrame = Http3FrameBuilder::createHeadersFrame(qpackData);

    // debug -begin
    std::cout << "[Client] HEADERS frame bytes (" << headersFrame.size() << "): ";
    size_t maxPrint = (headersFrame.size() < 32) ? headersFrame.size() : 32;
    for (size_t i = 0; i < maxPrint; ++i) {
        std::cout << std::format("{:02x} ", static_cast<int>(headersFrame[i]));
    }
    if (headersFrame.size() > 32) {
        std::cout << "... (+" << (headersFrame.size() - 32) << " more)";
    }
    std::cout << "\n";
    // debug -end

    QUIC_STATUS status = MsQuic->StreamOpen(
        connection,
        QUIC_STREAM_OPEN_FLAG_NONE,  // Bidirectional stream
        ClientStreamCallback,        // Properly annotated callback
        connection,                  // Non-null context (connection handle)
        &ConnectStream
    );

    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "[Client] Failed to open CONNECT stream");
        return;
    }

    status = MsQuic->StreamStart(ConnectStream, QUIC_STREAM_START_FLAG_IMMEDIATE);
    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "[Client] Failed to start CONNECT stream");
        return;
    }

    std::cout << "[Client] Connect stream started successfully\n";

    // Add a small delay to ensure stream is ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    QUIC_BUFFER headersBuf = {};
    headersBuf.Buffer = headersFrame.data();
    headersBuf.Length = static_cast<uint32_t>(headersFrame.size());

    std::cout << "[Client] About to send QUIC buffer (" << headersBuf.Length << " bytes): ";
    maxPrint = (headersBuf.Length < 32) ? headersBuf.Length : 32;
    for (size_t i = 0; i < maxPrint; ++i) {
        std::cout << std::format("{:02x} ", static_cast<int>(headersBuf.Buffer[i]));
    }
    if (headersBuf.Length > 32) {
        std::cout << "... (+" << (headersBuf.Length - 32) << " more)";
    }
    std::cout << "\n";

    std::cout << "[Client] About to send buffer verification:\n";
    std::cout << "[Client] Buffer pointer: " << (void*)headersBuf.Buffer << "\n";
    std::cout << "[Client] Buffer length: " << headersBuf.Length << "\n";
    std::cout << "[Client] First 16 bytes of actual buffer: ";
    for (uint32_t i = 0; i < headersBuf.Length && i < 16; ++i) {
        std::cout << std::format("{:02x} ", static_cast<int>(headersBuf.Buffer[i]));
    }
    std::cout << "\n";

    status = MsQuic->StreamSend(ConnectStream, &headersBuf, 1, QUIC_SEND_FLAG_NONE, nullptr);
    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "[Client] Failed to send CONNECT request");
    }
    else {
        std::cout << "[Client] HEADERS frame send initiated successfully\n";
    }

    std::cout << "[Client] WebTransport CONNECT request sent (" << headersFrame.size() << " bytes)\n";
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
ClientStreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event
) {
    UNREFERENCED_PARAMETER(Context);

    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE: {
        std::cout << "[ClientStreamCallback] Received " << Event->RECEIVE.Buffers->Length << " bytes\n";

        // For simplicity, just print the raw response
        std::string received(
            reinterpret_cast<char*>(Event->RECEIVE.Buffers->Buffer),
            Event->RECEIVE.Buffers->Length
        );

        std::cout << "[Client] Server response: ";
        for (uint8_t b : received) {
            std::cout << std::format("{:02x} ", static_cast<uint8_t>(b));
        }
        std::cout << "\n";

        // Check if this looks like an HTTP/3 200 OK response
        if (received.size() >= 3 && static_cast<uint8_t>(received[0]) == 0x01) {
            std::cout << "[Client] Received HTTP/3 frame, checking for 200 OK...\n";
            // Simple check for status 200 (QPACK encoded as 0x8F)
            if (received.find('\x8F') != std::string::npos) {
                std::cout << "[Client] WebTransport connection established! Got 200 OK\n";
                WebTransportEstablished = true;
            }
        }
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:{
        // Get the actual QUIC stream ID
        QUIC_UINT62 streamId = 0;
        uint32_t bufferLength = sizeof(streamId);
        QUIC_STATUS status = MsQuic->GetParam(Stream, QUIC_PARAM_STREAM_ID, &bufferLength, &streamId);

        std::cout << "[ClientStreamCallback] Send completed on QUIC stream ID " << streamId
            << " (handle " << std::hex << Stream << std::dec << ")\n";
        break;
    }
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:{
        std::cout << "[ClientStreamCallback] Stream shutdown complete\n";
        MsQuic->StreamClose(Stream);
        break;
    }

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
ClientConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event
) {
    UNREFERENCED_PARAMETER(Context);

    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        std::cout << "[ClientConnectionCallback] Connected to server!\n";

        // Step 1: Send SETTINGS frame
        //SendSettingsFrame(Connection);

        // Step 2: Wait MUCH longer for streams to be fully established
        std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // Increase to 2 seconds
        SendWebTransportConnect(Connection, "localhost:4443", "/webtransport");

        break;

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        std::cout << "[ClientConnectionCallback] Server started a stream\n";
        MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, ClientStreamCallback, nullptr);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        std::cout << "[ClientConnectionCallback] Connection shutdown\n";
        MsQuic->ConnectionClose(Connection);
        break;

    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
        std::cout << "[ClientConnectionCallback] Received QUIC datagram ("
            << Event->DATAGRAM_RECEIVED.Buffer->Length << " bytes)\n";
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

int main(int argc, char** argv) {
    std::string serverAddress = "127.0.0.1";
    uint16_t serverPort = 4443;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg.starts_with("-server:")) {
            serverAddress = arg.substr(8);
        }
        else if (arg.starts_with("-port:")) {
            serverPort = static_cast<uint16_t>(std::stoul(std::string(arg.substr(6))));
        }
    }

    std::cout << "=== MsQuic WebTransport Client ===\n";
    std::cout << "Connecting to: " << serverAddress << ":" << serverPort << "\n\n";

    // Initialize MsQuic
    if (QUIC_FAILED(MsQuicOpen2(&MsQuic))) {
        std::cerr << "MsQuicOpen2 failed\n";
        return 1;
    }

    QUIC_REGISTRATION_CONFIG regConfig = { "QuicWebTransportClient", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    if (QUIC_FAILED(MsQuic->RegistrationOpen(&regConfig, &Registration))) {
        std::cerr << "RegistrationOpen failed\n";
        return 1;
    }

    // Configure for HTTP/3
    const char* alpnStr = "h3";
    QUIC_BUFFER Alpn = {
        static_cast<uint32_t>(std::strlen(alpnStr)),
        reinterpret_cast<uint8_t*>(const_cast<char*>(alpnStr))
    };

    QUIC_SETTINGS settings = {};
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 4;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerBidiStreamCount = 4;

    if (QUIC_FAILED(MsQuic->ConfigurationOpen(
        Registration,
        &Alpn,
        1,
        &settings,
        sizeof(settings),
        nullptr,
        &Configuration))) {
        std::cerr << "ConfigurationOpen failed\n";
        return 1;
    }

    // Configure TLS for client (allow self-signed certificates for testing)
    QUIC_CREDENTIAL_CONFIG credConfig = {};
    credConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

    QUIC_STATUS status = MsQuic->ConfigurationLoadCredential(Configuration, &credConfig);
    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "ConfigurationLoadCredential failed");
        return 1;
    }

    // Create connection with proper callback
    if (QUIC_FAILED(MsQuic->ConnectionOpen(Registration, ClientConnectionCallback, nullptr, &Connection))) {
        std::cerr << "ConnectionOpen failed\n";
        return 1;
    }

    // Start connection
    QUIC_ADDR serverAddr = {};
    serverAddr.Ipv4.sin_family = AF_INET;
    serverAddr.Ipv4.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverAddress.c_str(), &serverAddr.Ipv4.sin_addr);

    status = MsQuic->ConnectionStart(Connection, Configuration, QUIC_ADDRESS_FAMILY_INET, serverAddress.c_str(), serverPort);
    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "ConnectionStart failed");
        return 1;
    }

    std::cout << "[Client] Connection started, waiting for WebTransport handshake...\n";

    // Wait for WebTransport establishment
    auto startTime = std::chrono::steady_clock::now();
    while (!WebTransportEstablished &&
        std::chrono::steady_clock::now() - startTime < std::chrono::seconds(10)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (WebTransportEstablished) {
        std::cout << "\n[SUCCESS] WebTransport connection fully established!\n";
        std::cout << "Ready to send WebTransport streams and datagrams...\n";

        // Demonstrate sending some test data
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::string testMessage = "Hello from WebTransport client!";
        QUIC_BUFFER testBuf = {};
        testBuf.Buffer = reinterpret_cast<uint8_t*>(testMessage.data());
        testBuf.Length = static_cast<uint32_t>(testMessage.size());

        if (ConnectStream) {
            std::cout << "[Client] Sending test message on WebTransport stream...\n";
            MsQuic->StreamSend(ConnectStream, &testBuf, 1, QUIC_SEND_FLAG_NONE, nullptr);
        }

        // Keep alive for a bit to test bidirectional communication
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    else {
        std::cout << "[FAILED] WebTransport connection failed to establish within timeout\n";
    }

    // Cleanup
    if (ControlStream) MsQuic->StreamClose(ControlStream);
    if (ConnectStream) MsQuic->StreamClose(ConnectStream);
    if (Connection) MsQuic->ConnectionClose(Connection);
    if (Configuration) MsQuic->ConfigurationClose(Configuration);
    if (Registration) MsQuic->RegistrationClose(Registration);
    MsQuicClose(MsQuic);

    std::cout << "\n[Client] Shutdown complete\n";
    return 0;
}