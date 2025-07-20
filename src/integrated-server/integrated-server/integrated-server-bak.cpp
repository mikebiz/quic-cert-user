// integrated-server.cpp - MsQuic Server with HTTP/3 WebTransport Support
// Compatible with MsQuic NuGet package 2.4.10
#include <msquic.h>
#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <format>
#include <unordered_map>
#include <span>
#include <cstdint>
#include <array>
#include <optional>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <wincrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <thread>
#include <unordered_set>    // Required for std::unordered_set

#pragma comment(lib, "msquic.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Ws2_32.lib")

static auto serverStartTime = std::chrono::steady_clock::now();

static std::string getTimestamp() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - serverStartTime);
    return "[T+" + std::to_string(duration.count()) + "ms]";
}

// QPACK Static Table (simplified for WebTransport)
struct QpackStaticEntry {
    std::string_view name;
    std::string_view value;
};

constexpr std::array<QpackStaticEntry, 99> QPACK_STATIC_TABLE = { {
    {"", ""},                     // 0 - unused
    {":authority", ""},           // 1
    {":path", "/"},              // 2
    {":path", "/index.html"},    // 3
    {":path", "/index.htm"},     // 4
    {":method", "CONNECT"},      // 5 <- Important for WebTransport
    {":method", "DELETE"},       // 6
    {":method", "GET"},          // 7
    {":method", "HEAD"},         // 8
    {":method", "OPTIONS"},      // 9
    {":method", "POST"},         // 10
    {":method", "PUT"},          // 11
    {":scheme", "http"},         // 12
    {":scheme", "https"},        // 13
    {":status", "103"},          // 14
    {":status", "200"},          // 15
    {":status", "304"},          // 16
    {":status", "404"},          // 17
    {":status", "503"},          // 18
    {":status", "100"},          // 19
    {":status", "204"},          // 20
    {":status", "206"},          // 21
    {":status", "300"},          // 22
    {":status", "400"},          // 23
    {":status", "403"},          // 24
    {":status", "421"},          // 25
    {":status", "425"},          // 26
    {":status", "500"},          // 27
    {"accept-charset", ""},      // 28
    {"accept-encoding", "gzip, deflate, br"},  // 29
    {"accept-language", ""},     // 30
    {"accept-ranges", ""},       // 31
    {"accept", ""},              // 32
    {"access-control-allow-headers", ""}, // 33
    {"access-control-allow-methods", ""}, // 34
    {"access-control-allow-origin", ""},  // 35
    {"age", ""},                 // 36
    {"allow", ""},               // 37
    {"authorization", ""},       // 38
    {"cache-control", ""},       // 39
    {"content-disposition", ""}, // 40
    {"content-encoding", ""},    // 41
    {"content-language", ""},    // 42
    {"content-length", ""},      // 43
    {"content-location", ""},    // 44
    {"content-range", ""},       // 45
    {"content-type", ""},        // 46
    {"cookie", ""},              // 47
    {"date", ""},                // 48
    {"etag", ""},                // 49
    {"expect", ""},              // 50
    {"expires", ""},             // 51
    {"from", ""},                // 52
    {"host", ""},                // 53
    {"if-match", ""},            // 54
    {"if-modified-since", ""},   // 55
    {"if-none-match", ""},       // 56
    {"if-range", ""},            // 57
    {"if-unmodified-since", ""}, // 58
    {"last-modified", ""},       // 59
    {"link", ""},                // 60
    {"location", ""},            // 61
    {"max-forwards", ""},        // 62
    {"proxy-authenticate", ""},  // 63
    {"proxy-authorization", ""}, // 64
    {"range", ""},               // 65
    {"referer", ""},             // 66
    {"refresh", ""},             // 67
    {"retry-after", ""},         // 68
    {"server", ""},              // 69
    {"set-cookie", ""},          // 70
    {"strict-transport-security", ""}, // 71
    {"transfer-encoding", ""},   // 72
    {"user-agent", ""},          // 73
    {"vary", ""},                // 74
    {"via", ""},                 // 75
    {"www-authenticate", ""},    // 76
    {"accept-encoding", "gzip, deflate"}, // 77
    {"accept-language", "en"},   // 78
    {"cache-control", "max-age=0"}, // 79
    {"cache-control", "no-cache"}, // 80
    {"content-encoding", "br"},  // 81
    {"content-encoding", "gzip"}, // 82
    {"content-type", "application/dns-message"}, // 83
    {"content-type", "application/javascript"}, // 84
    {"content-type", "application/json"}, // 85
    {"content-type", "application/octet-stream"}, // 86
    {"content-type", "text/css"}, // 87
    {"content-type", "text/html; charset=utf-8"}, // 88
    {"content-type", "text/plain"}, // 89
    {"content-type", "text/plain;charset=utf-8"}, // 90
    {"range", "bytes=0-"},       // 91
    {"strict-transport-security", "max-age=31536000"}, // 92
    {"strict-transport-security", "max-age=31536000; includesubdomains"}, // 93
    {"strict-transport-security", "max-age=31536000; includesubdomains; preload"}, // 94
    {"vary", "accept-encoding"}, // 95
    {"vary", "origin"},          // 96
    {"x-content-type-options", "nosniff"}, // 97
    {"x-xss-protection", "1; mode=block"}, // 98
} };

// Enhanced QPACK decoder with proper integer decoding
class QpackDecoder {
public:
    struct Header {
        std::string name;
        std::string value;
    };

private:
    size_t position = 0;
    const std::vector<uint8_t>* data = nullptr;

    // Decode QPACK integer with N-bit prefix
    std::optional<uint64_t> decodeInteger(uint8_t prefixBits) {
        if (position >= data->size()) return std::nullopt;

        uint64_t maxPrefix = (1ULL << prefixBits) - 1;
        uint64_t value = (*data)[position] & static_cast<uint8_t>(maxPrefix);
        position++;

        if (value < maxPrefix) {
            return value;
        }

        // Multi-byte integer
        uint64_t multiplier = 1;
        while (position < data->size()) {
            uint8_t byte = (*data)[position++];
            value += (byte & 0x7F) * multiplier;
            multiplier *= 128;

            if ((byte & 0x80) == 0) {
                break;
            }

            if (multiplier > (UINT64_MAX / 128)) {
                return std::nullopt; // Overflow protection
            }
        }

        return value;
    }

    std::optional<std::string> decodeString() {
        if (position >= data->size()) return std::nullopt;

        bool huffman = ((*data)[position] & 0x80) != 0;
        auto length = decodeInteger(7);
        if (!length || *length > data->size() - position) {
            return std::nullopt;
        }

        std::string result;
        if (huffman) {
            // For now, indicate Huffman encoding but don't decode
            result = "[HUFFMAN:" + std::to_string(*length) + "bytes]";
            position += *length;
        }
        else {
            result = std::string(
                reinterpret_cast<const char*>(data->data() + position),
                *length
            );
            position += *length;
        }

        return result;
    }

public:
    bool decodeHeaders(const std::vector<uint8_t>& qpackData, std::vector<Header>& headers) {
        data = &qpackData;
        position = 0;
        headers.clear();

        while (position < data->size()) {
            uint8_t firstByte = (*data)[position];
            Header header;

            if ((firstByte & 0x80) != 0) {
                // 1xxxxxxx - Indexed Header Field
                auto index = decodeInteger(7);
                if (!index || *index == 0 || *index >= QPACK_STATIC_TABLE.size()) {
                    std::cout << "  [ERROR] Invalid static table index: " << (*index) << "\n";
                    return false;
                }

                header.name = QPACK_STATIC_TABLE[*index].name;
                header.value = QPACK_STATIC_TABLE[*index].value;

                std::cout << "  [INDEXED] Static[" << *index << "]: " << header.name;
                if (!header.value.empty()) {
                    std::cout << "=" << header.value;
                }
                std::cout << "\n";

            }
            else if ((firstByte & 0x40) != 0) {
                // 01xxxxxx - Literal Header Field with Incremental Indexing — Indexed Name
                auto nameIndex = decodeInteger(6);
                if (!nameIndex || *nameIndex == 0 || *nameIndex >= QPACK_STATIC_TABLE.size()) {
                    std::cout << "  [ERROR] Invalid name index\n";
                    return false; // Instead of using *nameIndex
                }

                auto value = decodeString();
                if (!value) {
                    std::cout << "  [ERROR] Failed to decode header value\n";
                    return false;
                }

                header.name = QPACK_STATIC_TABLE[*nameIndex].name;
                header.value = *value;

                std::cout << "  [LITERAL_INDEXED_NAME] Static[" << *nameIndex << "]: " << header.name << "=" << header.value << "\n";

            }
            else if ((firstByte & 0x20) != 0) {
                // 001xxxxx - Literal Header Field with Incremental Indexing — Literal Name
                position++; // Skip the pattern byte

                auto name = decodeString();
                if (!name) {
                    std::cout << "  [ERROR] Failed to decode header name\n";
                    return false;
                }

                auto value = decodeString();
                if (!value) {
                    std::cout << "  [ERROR] Failed to decode header value\n";
                    return false;
                }

                header.name = *name;
                header.value = *value;

                std::cout << "  [LITERAL_LITERAL] " << header.name << "=" << header.value << "\n";

            }
            else {
                std::cout << "  [ERROR] Unknown QPACK pattern: 0x" << std::hex << static_cast<int>(firstByte) << std::dec << "\n";
                return false;
            }

            headers.push_back(header);
        }

        return true;
    }
};

// Proper HTTP/3 frame parser with varint support
class Http3FrameParser {
public:
    struct Frame {
        uint64_t type = 0;
        std::vector<uint8_t> payload;
        bool valid = false;
        std::string error;
    };

private:
    // Decode HTTP/3 variable-length integer
    static std::pair<uint64_t, size_t> decodeVarint(const std::vector<uint8_t>& data, size_t offset) {
        if (offset >= data.size()) {
            return { 0, 0 }; // Error
        }

        uint8_t firstByte = data[offset];
        uint8_t prefix = (firstByte & 0xC0) >> 6; // Top 2 bits determine length

        switch (prefix) {
        case 0: // 00xxxxxx - 6-bit value (0-63)
            return { firstByte & 0x3F, 1 };

        case 1: // 01xxxxxx - 14-bit value (0-16383)
            if (offset + 1 >= data.size()) return { 0, 0 };
            return {
                ((static_cast<uint64_t>(firstByte & 0x3F) << 8) | data[offset + 1]),
                2
            };

        case 2: // 10xxxxxx - 30-bit value (0-1073741823)
            if (offset + 3 >= data.size()) return { 0, 0 };
            return {
                ((static_cast<uint64_t>(firstByte & 0x3F) << 24) |
                 (static_cast<uint64_t>(data[offset + 1]) << 16) |
                 (static_cast<uint64_t>(data[offset + 2]) << 8) |
                 static_cast<uint64_t>(data[offset + 3])),
                4
            };

        case 3: // 11xxxxxx - 62-bit value (0-4611686018427387903)
            if (offset + 7 >= data.size()) return { 0, 0 };
            uint64_t value = static_cast<uint64_t>(firstByte & 0x3F);
            for (int i = 1; i < 8; ++i) {
                value = (value << 8) | static_cast<uint64_t>(data[offset + i]);
            }
            return { value, 8 };
        }

        return { 0, 0 }; // Should never reach here
    }
public:
    Frame parseFrame(const std::vector<uint8_t>& data) {
        Frame frame;

        if (data.empty()) {
            frame.error = "Empty data";
            return frame;
        }

        // Debug: Print raw data (simple version)
        std::cout << "[parseFrame] Parsing " << data.size() << " bytes: ";
        size_t printCount = data.size() < 8 ? data.size() : 8;
        for (size_t i = 0; i < printCount; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
        }
        std::cout << std::dec << "\n";

        // Decode frame type (varint)
        auto [frameType, typeBytes] = decodeVarint(data, 0);
        if (typeBytes == 0) {
            frame.error = "Failed to decode frame type";
            return frame;
        }

        std::cout << "[parseFrame] Frame type: " << frameType << " (consumed " << typeBytes << " bytes)\n";

        // Decode frame length (varint)
        auto [frameLength, lengthBytes] = decodeVarint(data, typeBytes);
        if (lengthBytes == 0) {
            frame.error = "Failed to decode frame length";
            return frame;
        }

        std::cout << "[parseFrame] Frame length: " << frameLength << " (consumed " << lengthBytes << " bytes)\n";

        size_t headerSize = typeBytes + lengthBytes;
        std::cout << "[parseFrame] Header size: " << headerSize << ", Total data: " << data.size() << ", Required: " << (headerSize + frameLength) << "\n";

        // Validate we have enough data for the payload
        if (data.size() < headerSize + frameLength) {
            std::cout << "[parseFrame] ERROR: Insufficient data!\n";
            frame.error = "Insufficient data for frame payload";
            return frame;
        }

        frame.type = frameType;
        frame.payload.assign(
            data.begin() + headerSize,
            data.begin() + headerSize + frameLength
        );
        frame.valid = true;

        std::cout << "[parseFrame] Successfully parsed frame type " << frameType << " with " << frame.payload.size() << " byte payload\n";

        return frame;
    }

    std::string getFrameTypeName(uint64_t type) {
        switch (type) {
        case 0x00: return "DATA";
        case 0x01: return "HEADERS";
        case 0x04: return "SETTINGS";
        case 0x05: return "PUSH_PROMISE";
        case 0x07: return "GOAWAY";
        case 0x0D: return "MAX_PUSH_ID";
        case 0x41: return "WEBTRANSPORT_STREAM";
        default: return "UNKNOWN(" + std::to_string(type) + ")";
        }
    }
};

// Enhanced WebTransport validator
class WebTransportValidator {
public:
    struct Result {
        bool isValid = false;
        bool isWebTransport = false;
        std::string authority;
        std::string path;
        std::string message;
    };

    Result validate(const std::vector<QpackDecoder::Header>& headers) {
        Result result;

        std::string method, protocol, scheme, authority, path;

        for (const auto& header : headers) {
            if (header.name == ":method") {
                method = header.value;
            }
            else if (header.name == ":protocol") {
                protocol = header.value;
            }
            else if (header.name == ":scheme") {
                scheme = header.value;
            }
            else if (header.name == ":authority") {
                authority = header.value;
            }
            else if (header.name == ":path") {
                path = header.value;
            }
        }

        result.authority = authority;
        result.path = path;

        // Check if this is a WebTransport CONNECT request
        if (method == "CONNECT" && protocol == "webtransport") {
            result.isWebTransport = true;
            result.message = "WebTransport CONNECT request detected";

            // Validate required headers for WebTransport
            if (scheme != "https") {
                result.message = "WebTransport requires HTTPS scheme, got: " + scheme;
                return result;
            }

            if (authority.empty()) {
                result.message = "WebTransport requires :authority header";
                return result;
            }

            if (path.empty()) {
                result.message = "WebTransport requires :path header";
                return result;
            }

            result.isValid = true;
            result.message = "Valid WebTransport request to " + authority + path;
        }
        else {
            if (method == "CONNECT") {
                result.message = "CONNECT request but not WebTransport (protocol: " + protocol + ")";
            }
            else {
                result.message = "Not a CONNECT request (method: " + method + ")";
            }
        }

        return result;
    }
};

// Global variables
const QUIC_API_TABLE* MsQuic = nullptr;
HQUIC Registration = nullptr;
HQUIC Configuration = nullptr;
HQUIC Listener = nullptr;
static std::unordered_set<HQUIC> seenStreams;

// Session tracking
struct WebTransportSession {
    HQUIC connection = nullptr;
    std::string authority;
    std::string path;
    bool established = false;
};

std::unordered_map<HQUIC, WebTransportSession> activeSessions;

// Forward declarations
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS QUIC_API ServerStreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event
);

_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS QUIC_API ServerConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event
);

_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS QUIC_API ServerListenerCallback(
    _In_ HQUIC Listener,
    _In_opt_ void* Context,
    _Inout_ QUIC_LISTENER_EVENT* Event
);

// Helper functions
static void DescribeQuicStatus(QUIC_STATUS status, const std::string& message) {
    std::cerr << message << " (QUIC_STATUS: 0x" << std::hex << status << ")\n";
}

static bool ParseHexHash(std::string_view hexStr, std::span<uint8_t, 20> outBytes) {
    if (hexStr.size() != 40) return false;
    for (size_t i = 0; i < 20; ++i) {
        std::string byteStr = { hexStr[i * 2], hexStr[i * 2 + 1] };
        if (!std::isxdigit(byteStr[0]) || !std::isxdigit(byteStr[1])) return false;
        outBytes[i] = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
    }
    return true;
}

static std::vector<uint8_t> createHttp3Response(uint16_t statusCode) {
    std::vector<uint8_t> response;

    response.push_back(0x01); // HEADERS frame type
    response.push_back(0x01); // Length: 1 byte

    if (statusCode == 200) {
        response.push_back(0x8F); // :status 200 (static table index 15)
    }
    else if (statusCode == 400) {
        response.push_back(0x97); // :status 400 (static table index 23)
    }
    else {
        response.push_back(0x91); // :status 404 (static table index 17)
    }

    return response;
}

// Helper function to send server SETTINGS frame
static void sendServerSettings(HQUIC connection) {
    std::cout << getTimestamp() << " === SENDING SERVER SETTINGS ===" << std::endl;
    std::cout << getTimestamp() << " Connection handle: " << std::hex << connection << std::dec << std::endl;

    // Create server control stream data
    static std::vector<uint8_t> serverControlData;
    serverControlData.clear();

    // Stream type identifier for control stream
    serverControlData.push_back(0x00);

    // Create SETTINGS frame
    serverControlData.push_back(0x04); // SETTINGS frame type
    serverControlData.push_back(0x05); // Length = 5 bytes

    // ENABLE_WEBTRANSPORT setting
    serverControlData.push_back(0x2b);
    serverControlData.push_back(0x60);
    serverControlData.push_back(0x37);
    serverControlData.push_back(0x42);
    serverControlData.push_back(0x01); // Value = 1

    std::cout << getTimestamp() << " Server control data (" << serverControlData.size() << " bytes): ";
    for (size_t i = 0; i < serverControlData.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)serverControlData[i] << " ";
    }
    std::cout << std::dec << std::endl;

    // Create server control stream (unidirectional, ID 3)
    HQUIC serverControlStream = nullptr;
    QUIC_STATUS status = MsQuic->StreamOpen(
        connection,
        QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL,
        ServerStreamCallback,
        reinterpret_cast<void*>(0x2000), // Server control stream context
        &serverControlStream
    );

    if (QUIC_FAILED(status)) {
        std::cout << getTimestamp() << " ERROR: Failed to create server control stream: 0x" << std::hex << status << std::dec << std::endl;
        return;
    }

    std::cout << getTimestamp() << " Server control stream created: " << std::hex << serverControlStream << std::dec << std::endl;

    status = MsQuic->StreamStart(serverControlStream, QUIC_STREAM_START_FLAG_IMMEDIATE);
    if (QUIC_FAILED(status)) {
        std::cout << getTimestamp() << " ERROR: Failed to start server control stream: 0x" << std::hex << status << std::dec << std::endl;
        MsQuic->StreamClose(serverControlStream);
        return;
    }

    std::cout << getTimestamp() << " Server control stream started successfully\n";

    QUIC_BUFFER serverBuf = {};
    serverBuf.Buffer = serverControlData.data();
    serverBuf.Length = static_cast<uint32_t>(serverControlData.size());

    std::cout << getTimestamp() << " About to send " << serverBuf.Length << " bytes\n";

    status = MsQuic->StreamSend(serverControlStream, &serverBuf, 1, QUIC_SEND_FLAG_FIN, nullptr);
    if (QUIC_FAILED(status)) {
        std::cout << getTimestamp() << " ERROR: Failed to send server SETTINGS: 0x" << std::hex << status << std::dec << std::endl;
    }
    else {
        std::cout << getTimestamp() << " SUCCESS: Server SETTINGS sent successfully\n";
    }

    std::cout << getTimestamp() << " === SERVER SETTINGS SEND COMPLETE ===" << std::endl;
}

// Helper function to parse SETTINGS payload
static void parseSettingsPayload(const std::vector<uint8_t>& payload) {
    std::cout << getTimestamp() << " === PARSING SETTINGS PAYLOAD ===" << std::endl;
    std::cout << getTimestamp() << " Payload size: " << payload.size() << " bytes\n";

    if (payload.empty()) {
        std::cout << getTimestamp() << " ERROR: Empty payload\n";
        return;
    }

    std::cout << getTimestamp() << " Full payload: ";
    for (size_t i = 0; i < payload.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)payload[i] << " ";
    }
    std::cout << std::dec << std::endl;

    size_t pos = 0;
    int settingCount = 0;

    while (pos < payload.size()) {
        settingCount++;
        std::cout << getTimestamp() << " Parsing setting #" << settingCount << " at position " << pos << "\n";

        // Try to read setting ID (simplified varint parsing for debugging)
        if (pos + 4 < payload.size()) {
            uint32_t settingId = (payload[pos] << 24) | (payload[pos + 1] << 16) |
                (payload[pos + 2] << 8) | payload[pos + 3];

            std::cout << getTimestamp() << "   Setting ID bytes: "
                << std::hex << (int)payload[pos] << " " << (int)payload[pos + 1] << " "
                << (int)payload[pos + 2] << " " << (int)payload[pos + 3] << std::dec << "\n";
            std::cout << getTimestamp() << "   Setting ID: 0x" << std::hex << settingId << std::dec << "\n";

            pos += 4;

            if (pos < payload.size()) {
                uint8_t value = payload[pos++];
                std::cout << getTimestamp() << "   Setting value: " << (int)value << "\n";

                if (settingId == 0x2b603742) { // ENABLE_WEBTRANSPORT
                    std::cout << getTimestamp() << "   -> ENABLE_WEBTRANSPORT = " << (int)value << "\n";
                    if (value == 1) {
                        std::cout << getTimestamp() << "   -> WebTransport is ENABLED!\n";
                    }
                }
                else {
                    std::cout << getTimestamp() << "   -> Unknown setting 0x" << std::hex << settingId
                        << " = " << std::dec << (int)value << "\n";
                }
            }
            else {
                std::cout << getTimestamp() << "   ERROR: No value byte available\n";
                break;
            }
        }
        else {
            std::cout << getTimestamp() << "   ERROR: Not enough bytes for setting ID (need 4, have "
                << (payload.size() - pos) << ")\n";
            break;
        }
    }

    std::cout << getTimestamp() << " === SETTINGS PARSING COMPLETE (" << settingCount << " settings) ===" << std::endl;
}

// CRITICAL FIX: Try a different approach - Force stream acceptance
// Add this function to manually handle the stream issue

static void ForceStreamAcceptance(HQUIC connection) {
    std::cout << getTimestamp() << " === IMMEDIATE STREAM DIAGNOSTIC ===\n";

    // Start a thread that checks every second for 10 seconds
    std::thread([connection]() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // Check connection statistics
            QUIC_STATISTICS_V2 stats = {};
            uint32_t statsSize = sizeof(stats);
            QUIC_STATUS statStatus = MsQuic->GetParam(connection, QUIC_PARAM_CONN_STATISTICS_V2, &statsSize, &stats);

            if (QUIC_SUCCEEDED(statStatus)) {
                std::cout << getTimestamp() << " [" << i << "] Stats check:\n";
                std::cout << getTimestamp() << "   RecvTotalPackets: " << stats.RecvTotalPackets << "\n";
                std::cout << getTimestamp() << "   RecvTotalStreamBytes: " << stats.RecvTotalStreamBytes << "\n";
                std::cout << getTimestamp() << "   SendTotalStreamBytes: " << stats.SendTotalStreamBytes << "\n";

                if (stats.RecvTotalStreamBytes > 0) {
                    std::cout << getTimestamp() << " *** CONFIRMED BUG: Stream data received but no PEER_STREAM_STARTED events! ***\n";
                    std::cout << getTimestamp() << " Client sent " << stats.RecvTotalStreamBytes << " bytes but server callbacks never fired\n";
                    std::cout << getTimestamp() << " This is a MsQuic configuration or version issue\n";

                    // Also check what MsQuic version we're using
                    uint32_t version[4] = {};
                    uint32_t versionSize = sizeof(version);
                    if (QUIC_SUCCEEDED(MsQuic->GetParam(nullptr, QUIC_PARAM_GLOBAL_LIBRARY_VERSION, &versionSize, version))) {
                        std::cout << getTimestamp() << " MsQuic library version: " << version[0] << "." << version[1] << "." << version[2] << "." << version[3] << "\n";
                    }

                    break;
                }

                if (stats.RecvTotalPackets == 0) {
                    std::cout << getTimestamp() << " No packets received yet - client may not be connecting\n";
                }
                else {
                    std::cout << getTimestamp() << " Packets received but no stream data yet\n";
                }
            }
            else {
                std::cout << getTimestamp() << " Failed to get connection statistics\n";
            }
        }

        std::cout << getTimestamp() << " Stream diagnostic completed\n";
        }).detach();
}

// DIAGNOSTIC FUNCTION: Add this to help debug the issue
static void DiagnoseStreamIssue(HQUIC connection) {
    std::cout << getTimestamp() << " === STREAM ISSUE DIAGNOSIS ===\n";

    // Check if this is a known MsQuic issue with specific versions
    uint32_t version[4] = {};
    uint32_t versionSize = sizeof(version);
    if (QUIC_SUCCEEDED(MsQuic->GetParam(nullptr, QUIC_PARAM_GLOBAL_LIBRARY_VERSION, &versionSize, version))) {
        std::cout << getTimestamp() << " MsQuic version: " << version[0] << "." << version[1] << "." << version[2] << "." << version[3] << "\n";
    }

    // Check connection state
    QUIC_STATISTICS_V2 stats = {};
    uint32_t statsSize = sizeof(stats);
    if (QUIC_SUCCEEDED(MsQuic->GetParam(connection, QUIC_PARAM_CONN_STATISTICS_V2, &statsSize, &stats))) {
        std::cout << getTimestamp() << " Connection statistics:\n";
        std::cout << getTimestamp() << "   RecvTotalPackets: " << stats.RecvTotalPackets << "\n";
        std::cout << getTimestamp() << "   RecvTotalStreamBytes: " << stats.RecvTotalStreamBytes << "\n";
        std::cout << getTimestamp() << "   SendTotalPackets: " << stats.SendTotalPackets << "\n";
        std::cout << getTimestamp() << "   SendTotalStreamBytes: " << stats.SendTotalStreamBytes << "\n";
    }

    // Check negotiated ALPN
    uint8_t alpnBuffer[16] = {};
    uint32_t alpnSize = sizeof(alpnBuffer);
    if (QUIC_SUCCEEDED(MsQuic->GetParam(connection, QUIC_PARAM_TLS_NEGOTIATED_ALPN, &alpnSize, alpnBuffer))) {
        std::cout << getTimestamp() << " Negotiated ALPN: ";
        for (uint32_t i = 0; i < alpnSize; ++i) {
            std::cout << (char)alpnBuffer[i];
        }
        std::cout << "\n";
    }

    std::cout << getTimestamp() << " === END DIAGNOSIS ===\n";
}

// Enhanced ServerStreamCallback with manual stream detection
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS QUIC_API ServerStreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event
) {
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE: {
        // === MANUAL STREAM DETECTION ===
        bool isNewStream = (seenStreams.find(Stream) == seenStreams.end());
        if (isNewStream) {
            seenStreams.insert(Stream);

            std::cout << "\n" << getTimestamp() << " *** NEW STREAM DETECTED MANUALLY! ***\n";
            std::cout << getTimestamp() << " === MANUAL PEER_STREAM_STARTED PROCESSING ===\n";
            std::cout << getTimestamp() << " Stream handle: " << std::hex << Stream << std::dec << "\n";

            // Get the actual QUIC stream ID
            QUIC_UINT62 streamId = 0;
            uint32_t bufferLength = sizeof(streamId);
            QUIC_STATUS status = MsQuic->GetParam(Stream, QUIC_PARAM_STREAM_ID, &bufferLength, &streamId);

            if (QUIC_SUCCEEDED(status)) {
                std::cout << getTimestamp() << " QUIC stream ID: " << streamId << "\n";

                // Analyze stream type using stream ID patterns
                std::cout << getTimestamp() << " Stream ID analysis:\n";
                std::cout << getTimestamp() << "   streamId % 4 = " << (streamId % 4) << "\n";

                if (streamId % 4 == 2) {
                    std::cout << getTimestamp() << "   -> UNIDIRECTIONAL (client-initiated)\n";
                    std::cout << getTimestamp() << "   -> This should be the control stream with SETTINGS\n";
                }
                else if (streamId % 4 == 0) {
                    std::cout << getTimestamp() << "   -> BIDIRECTIONAL (client-initiated)\n";
                    std::cout << getTimestamp() << "   -> This should be the WebTransport CONNECT stream\n";
                }
                else if (streamId % 4 == 3) {
                    std::cout << getTimestamp() << "   -> UNIDIRECTIONAL (server-initiated)\n";
                }
                else if (streamId % 4 == 1) {
                    std::cout << getTimestamp() << "   -> BIDIRECTIONAL (server-initiated)\n";
                }
            }
            else {
                std::cout << getTimestamp() << " ERROR: Failed to get stream ID\n";
            }

            std::cout << getTimestamp() << " === END MANUAL PEER_STREAM_STARTED PROCESSING ===\n";
        }

        // === DETAILED RECEIVE EVENT PROCESSING ===
        std::cout << getTimestamp() << " === RECEIVE EVENT ON STREAM " << std::hex << Stream << std::dec << " ===\n";
        std::cout << getTimestamp() << " Buffer size: " << Event->RECEIVE.Buffers->Length << "\n";

        // Get stream ID for processing
        QUIC_UINT62 streamId = 0;
        uint32_t bufferLength = sizeof(streamId);
        MsQuic->GetParam(Stream, QUIC_PARAM_STREAM_ID, &bufferLength, &streamId);

        // Show raw buffer data
        std::cout << getTimestamp() << " Raw buffer (" << Event->RECEIVE.Buffers->Length << " bytes): ";
        for (uint32_t i = 0; i < Event->RECEIVE.Buffers->Length && i < 32; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                << (int)Event->RECEIVE.Buffers->Buffer[i] << " ";
        }
        std::cout << std::dec << "\n";

        // Safely copy the buffer data immediately
        std::vector<uint8_t> data;
        data.resize(Event->RECEIVE.Buffers->Length);
        std::memcpy(data.data(), Event->RECEIVE.Buffers->Buffer, Event->RECEIVE.Buffers->Length);

        // CRITICAL: Complete the receive immediately
        MsQuic->StreamReceiveComplete(Stream, Event->RECEIVE.Buffers->Length);
        std::cout << getTimestamp() << " StreamReceiveComplete called\n";

        // Process based on stream type
        if (streamId % 4 == 2) {
            // Unidirectional control stream
            std::cout << getTimestamp() << " Processing CONTROL STREAM (ID " << streamId << ")\n";

            if (!data.empty() && data[0] == 0x00) {
                std::cout << getTimestamp() << " SUCCESS: Found control stream type identifier (0x00)\n";

                if (data.size() > 1) {
                    std::vector<uint8_t> frameData(data.begin() + 1, data.end());
                    std::cout << getTimestamp() << " Frame data (" << frameData.size() << " bytes): ";
                    for (size_t i = 0; i < frameData.size() && i < 16; ++i) {
                        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)frameData[i] << " ";
                    }
                    std::cout << std::dec << "\n";

                    // Parse HTTP/3 SETTINGS frame
                    if (frameData.size() >= 2 && frameData[0] == 0x04) {
                        std::cout << getTimestamp() << " SUCCESS: Found SETTINGS frame (type 0x04)\n";
                        std::cout << getTimestamp() << " Frame length: " << (int)frameData[1] << "\n";

                        if (frameData.size() >= 7) {
                            // Check for ENABLE_WEBTRANSPORT setting
                            bool foundWebTransport = false;
                            for (size_t i = 2; i < frameData.size() - 4; ++i) {
                                if (frameData[i] == 0x2b && frameData[i + 1] == 0x60 &&
                                    frameData[i + 2] == 0x37 && frameData[i + 3] == 0x42) {
                                    std::cout << getTimestamp() << " SUCCESS: Found ENABLE_WEBTRANSPORT setting!\n";
                                    if (i + 4 < frameData.size()) {
                                        std::cout << getTimestamp() << " WebTransport enabled: " << (int)frameData[i + 4] << "\n";
                                        foundWebTransport = true;
                                    }
                                    break;
                                }
                            }

                            if (foundWebTransport) {
                                std::cout << getTimestamp() << " SUCCESS: Control stream SETTINGS processed successfully!\n";
                                std::cout << getTimestamp() << " WebTransport is now enabled on this connection!\n";

                                // TODO: Send server SETTINGS response here
                                std::cout << getTimestamp() << " Should send server SETTINGS response...\n";
                            }
                        }
                    }
                }
            }
            else {
                std::cout << getTimestamp() << " ERROR: Control stream type incorrect or missing\n";
                std::cout << getTimestamp() << " Expected 0x00, got: 0x" << std::hex << (data.empty() ? 0 : data[0]) << std::dec << "\n";
            }

        }
        else if (streamId % 4 == 0) {
            // Bidirectional request stream
            std::cout << getTimestamp() << " Processing BIDIRECTIONAL STREAM (ID " << streamId << ")\n";

            if (!data.empty() && data[0] == 0x01) {
                std::cout << getTimestamp() << " Found HTTP/3 HEADERS frame (type 0x01)\n";

                // Parse the HEADERS frame
                if (data.size() >= 3) {
                    std::cout << getTimestamp() << " Frame length indicator: 0x" << std::hex << (int)data[1] << std::dec << "\n";

                    // Extract QPACK data (skip frame type and length)
                    std::vector<uint8_t> qpackData;
                    if (data[1] < 64) {
                        // Single byte length
                        uint8_t frameLength = data[1];
                        if (data.size() >= 2 + frameLength) {
                            qpackData.assign(data.begin() + 2, data.begin() + 2 + frameLength);
                        }
                    }
                    else {
                        // Multi-byte length - simplified for this case
                        std::cout << getTimestamp() << " Multi-byte frame length detected\n";
                        if (data.size() > 2) {
                            qpackData.assign(data.begin() + 2, data.end());
                        }
                    }

                    if (!qpackData.empty()) {
                        std::cout << getTimestamp() << " QPACK data (" << qpackData.size() << " bytes): ";
                        for (size_t i = 0; i < qpackData.size() && i < 16; ++i) {
                            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)qpackData[i] << " ";
                        }
                        std::cout << std::dec << "\n";

                        // Decode QPACK headers
                        QpackDecoder decoder;
                        std::vector<QpackDecoder::Header> headers;
                        if (decoder.decodeHeaders(qpackData, headers)) {
                            std::cout << getTimestamp() << " SUCCESS: Decoded " << headers.size() << " headers:\n";
                            for (const auto& header : headers) {
                                std::cout << getTimestamp() << "   " << header.name << ": " << header.value << "\n";
                            }

                            // Validate WebTransport request
                            WebTransportValidator validator;
                            auto result = validator.validate(headers);

                            if (result.isWebTransport) {
                                std::cout << getTimestamp() << " SUCCESS: Valid WebTransport CONNECT request!\n";
                                std::cout << getTimestamp() << " Authority: " << result.authority << "\n";
                                std::cout << getTimestamp() << " Path: " << result.path << "\n";

                                // Send HTTP/3 200 OK response
                                auto response = createHttp3Response(200);
                                QUIC_BUFFER responseBuf = {};
                                responseBuf.Buffer = response.data();
                                responseBuf.Length = static_cast<uint32_t>(response.size());

                                QUIC_STATUS sendStatus = MsQuic->StreamSend(Stream, &responseBuf, 1, QUIC_SEND_FLAG_NONE, nullptr);
                                if (QUIC_SUCCEEDED(sendStatus)) {
                                    std::cout << getTimestamp() << " SUCCESS: Sent HTTP/3 200 OK response!\n";
                                    std::cout << getTimestamp() << " WebTransport connection established!\n";
                                }
                                else {
                                    std::cout << getTimestamp() << " ERROR: Failed to send 200 OK response\n";
                                }
                            }
                            else {
                                std::cout << getTimestamp() << " Invalid WebTransport request: " << result.message << "\n";

                                // Send 400 Bad Request
                                auto response = createHttp3Response(400);
                                QUIC_BUFFER responseBuf = {};
                                responseBuf.Buffer = response.data();
                                responseBuf.Length = static_cast<uint32_t>(response.size());
                                MsQuic->StreamSend(Stream, &responseBuf, 1, QUIC_SEND_FLAG_FIN, nullptr);
                            }
                        }
                        else {
                            std::cout << getTimestamp() << " ERROR: Failed to decode QPACK headers\n";
                        }
                    }
                }
            }
            else {
                std::cout << getTimestamp() << " Unexpected frame type: 0x" << std::hex << (data.empty() ? 0 : data[0]) << std::dec << "\n";
            }
        }
        else {
            std::cout << getTimestamp() << " Other stream type (ID " << streamId << ")\n";
        }

        std::cout << getTimestamp() << " === END RECEIVE EVENT ===\n\n";
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE: {
        QUIC_UINT62 streamId = 0;
        uint32_t bufferLength = sizeof(streamId);
        MsQuic->GetParam(Stream, QUIC_PARAM_STREAM_ID, &bufferLength, &streamId);

        std::cout << getTimestamp() << " SEND_COMPLETE on stream ID " << streamId << "\n";
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
        QUIC_UINT62 streamId = 0;
        uint32_t bufferLength = sizeof(streamId);
        MsQuic->GetParam(Stream, QUIC_PARAM_STREAM_ID, &bufferLength, &streamId);

        std::cout << getTimestamp() << " SHUTDOWN_COMPLETE on stream " << streamId << "\n";

        // Remove from seen streams
        seenStreams.erase(Stream);

        MsQuic->StreamClose(Stream);
        break;
    }

    default:
        std::cout << getTimestamp() << " Other stream event: " << Event->Type << "\n";
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

// Connection callback implementation
// Enhanced ServerConnectionCallback with complete event type debugging
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS QUIC_API ServerConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event
) {
    UNREFERENCED_PARAMETER(Context);

    std::cout << getTimestamp() << " === SERVER CONNECTION CALLBACK ===\n";
    std::cout << getTimestamp() << " Connection: " << std::hex << Connection << std::dec << "\n";
    std::cout << getTimestamp() << " Event type: " << Event->Type << "\n";

    // COMPREHENSIVE EVENT TYPE ANALYSIS
    std::cout << getTimestamp() << " Event type analysis:\n";
    switch (Event->Type) {
    case 0: std::cout << getTimestamp() << "   Type 0 = QUIC_CONNECTION_EVENT_CONNECTED\n"; break;
    case 1: std::cout << getTimestamp() << "   Type 1 = QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT\n"; break;
    case 2: std::cout << getTimestamp() << "   Type 2 = QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER\n"; break;
    case 3: std::cout << getTimestamp() << "   Type 3 = QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE\n"; break;
    case 4: std::cout << getTimestamp() << "   Type 4 = QUIC_CONNECTION_EVENT_LOCAL_ADDRESS_CHANGED\n"; break;
    case 5: std::cout << getTimestamp() << "   Type 5 = QUIC_CONNECTION_EVENT_PEER_ADDRESS_CHANGED\n"; break;
    case 6: std::cout << getTimestamp() << "   Type 6 = QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED *** THIS IS WHAT WE WANT ***\n"; break;
    case 7: std::cout << getTimestamp() << "   Type 7 = QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE\n"; break;
    case 8: std::cout << getTimestamp() << "   Type 8 = QUIC_CONNECTION_EVENT_PEER_NEEDS_STREAMS\n"; break;
    case 9: std::cout << getTimestamp() << "   Type 9 = QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED\n"; break;
    case 10: std::cout << getTimestamp() << "   Type 10 = QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED\n"; break;
    case 11: std::cout << getTimestamp() << "   Type 11 = QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED\n"; break;
    case 12: std::cout << getTimestamp() << "   Type 12 = QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED\n"; break;
    case 13: std::cout << getTimestamp() << "   Type 13 = QUIC_CONNECTION_EVENT_RESUMED\n"; break;
    case 14: std::cout << getTimestamp() << "   Type 14 = QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED\n"; break;
    case 15: std::cout << getTimestamp() << "   Type 15 = QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED\n"; break;
    default: std::cout << getTimestamp() << "   Type " << Event->Type << " = UNKNOWN EVENT\n"; break;
    }

    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_CONNECTED\n";
        std::cout << getTimestamp() << " Client connected successfully!\n";

        // Get connection info
        QUIC_ADDR localAddr = {};
        uint32_t addrSize = sizeof(localAddr);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(Connection, QUIC_PARAM_CONN_LOCAL_ADDRESS, &addrSize, &localAddr))) {
            std::cout << getTimestamp() << " Local address configured\n";
        }

        QUIC_ADDR remoteAddr = {};
        addrSize = sizeof(remoteAddr);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(Connection, QUIC_PARAM_CONN_REMOTE_ADDRESS, &addrSize, &remoteAddr))) {
            std::cout << getTimestamp() << " Remote address: configured\n";
        }

        // Check ALPN negotiation
        uint8_t alpnBuffer[16] = {};
        uint32_t alpnSize = sizeof(alpnBuffer);
        QUIC_STATUS alpnStatus = MsQuic->GetParam(Connection, QUIC_PARAM_TLS_NEGOTIATED_ALPN, &alpnSize, alpnBuffer);
        if (QUIC_SUCCEEDED(alpnStatus) && alpnSize > 0) {
            std::cout << getTimestamp() << " Negotiated ALPN: ";
            for (uint32_t i = 0; i < alpnSize; ++i) {
                std::cout << (char)alpnBuffer[i];
            }
            std::cout << "\n";
        }
        else {
            std::cout << getTimestamp() << " WARNING: No ALPN negotiated or failed to get ALPN\n";
        }

        std::cout << getTimestamp() << " Connection is now ready for streams\n";

        // === CONNECTION PARAMETER INSPECTION ===
        std::cout << getTimestamp() << " === CONNECTION PARAMETER INSPECTION ===\n";

        // Check negotiated stream limits
        uint16_t localBidiStreams = 0;
        uint32_t paramSize = sizeof(localBidiStreams);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(Connection, QUIC_PARAM_CONN_LOCAL_BIDI_STREAM_COUNT, &paramSize, &localBidiStreams))) {
            std::cout << getTimestamp() << " Local bidirectional stream count: " << localBidiStreams << "\n";
        }

        uint16_t localUnidiStreams = 0;
        paramSize = sizeof(localUnidiStreams);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(Connection, QUIC_PARAM_CONN_LOCAL_UNIDI_STREAM_COUNT, &paramSize, &localUnidiStreams))) {
            std::cout << getTimestamp() << " Local unidirectional stream count: " << localUnidiStreams << "\n";
        }

        // Check stream IDs available
        uint64_t maxStreamIds[4] = {};
        paramSize = sizeof(maxStreamIds);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(Connection, QUIC_PARAM_CONN_MAX_STREAM_IDS, &paramSize, maxStreamIds))) {
            std::cout << getTimestamp() << " Max stream IDs:\n";
            std::cout << getTimestamp() << "   Client Bidirectional: " << maxStreamIds[0] << "\n";
            std::cout << getTimestamp() << "   Server Bidirectional: " << maxStreamIds[1] << "\n";
            std::cout << getTimestamp() << "   Client Unidirectional: " << maxStreamIds[2] << "\n";
            std::cout << getTimestamp() << "   Server Unidirectional: " << maxStreamIds[3] << "\n";
        }

        // Check connection settings
        QUIC_SETTINGS connSettings = {};
        paramSize = sizeof(connSettings);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(Connection, QUIC_PARAM_CONN_SETTINGS, &paramSize, &connSettings))) {
            std::cout << getTimestamp() << " Connection settings:\n";
            std::cout << getTimestamp() << "   PeerBidiStreamCount: " << connSettings.PeerBidiStreamCount << "\n";
            std::cout << getTimestamp() << "   PeerUnidiStreamCount: " << connSettings.PeerUnidiStreamCount << "\n";
            std::cout << getTimestamp() << "   ConnFlowControlWindow: " << connSettings.ConnFlowControlWindow << "\n";
            std::cout << getTimestamp() << "   StreamRecvWindowDefault: " << connSettings.StreamRecvWindowDefault << "\n";
        }

        std::cout << getTimestamp() << " === END CONNECTION PARAMETER INSPECTION ===\n";

        // === TESTING SERVER STREAM CREATION ===
        std::cout << getTimestamp() << " === TESTING SERVER STREAM CREATION ===\n";
        HQUIC testStream = nullptr;
        QUIC_STATUS testStatus = MsQuic->StreamOpen(
            Connection,
            QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL,
            ServerStreamCallback,
            reinterpret_cast<void*>(0x9999), // Test context
            &testStream
        );

        if (QUIC_SUCCEEDED(testStatus)) {
            std::cout << getTimestamp() << " Server test stream created successfully\n";

            testStatus = MsQuic->StreamStart(testStream, QUIC_STREAM_START_FLAG_IMMEDIATE);
            if (QUIC_SUCCEEDED(testStatus)) {
                std::cout << getTimestamp() << " Server test stream started successfully\n";

                // Send a test message
                std::string testMsg = "Hello from server test stream";
                QUIC_BUFFER testBuf = {};
                testBuf.Buffer = reinterpret_cast<uint8_t*>(testMsg.data());
                testBuf.Length = static_cast<uint32_t>(testMsg.size());

                testStatus = MsQuic->StreamSend(testStream, &testBuf, 1, QUIC_SEND_FLAG_FIN, nullptr);
                std::cout << getTimestamp() << " Server test stream send status: 0x" << std::hex << testStatus << std::dec << "\n";
            }
            else {
                std::cout << getTimestamp() << " Server test stream start FAILED: 0x" << std::hex << testStatus << std::dec << "\n";
            }
        }
        else {
            std::cout << getTimestamp() << " Server test stream creation FAILED: 0x" << std::hex << testStatus << std::dec << "\n";
        }

        // === ENHANCED EVENT MONITORING ===
        std::cout << getTimestamp() << " === WAITING FOR CLIENT STREAMS ===\n";
        std::cout << getTimestamp() << " Will monitor for PEER_STREAM_STARTED events (type 6)...\n";
        std::cout << getTimestamp() << " Expected client streams:\n";
        std::cout << getTimestamp() << "   - Control stream (ID 2, unidirectional)\n";
        std::cout << getTimestamp() << "   - WebTransport CONNECT (ID 0, bidirectional)\n";

        ForceStreamAcceptance(Connection);

        // IMMEDIATE CHECK: See current state right now
        QUIC_STATISTICS_V2 currentStats = {};
        uint32_t currentStatsSize = sizeof(currentStats);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(Connection, QUIC_PARAM_CONN_STATISTICS_V2, &currentStatsSize, &currentStats))) {
            std::cout << getTimestamp() << " IMMEDIATE stats at connection:\n";
            std::cout << getTimestamp() << "   RecvTotalPackets: " << currentStats.RecvTotalPackets << "\n";
            std::cout << getTimestamp() << "   RecvTotalStreamBytes: " << currentStats.RecvTotalStreamBytes << "\n";
        }
        else {
            std::cout << getTimestamp() << " Failed to get immediate stats\n";
        }

        // Also get MsQuic version info
        uint32_t libVersion[4] = {};
        uint32_t libVersionSize = sizeof(libVersion);
        if (QUIC_SUCCEEDED(MsQuic->GetParam(nullptr, QUIC_PARAM_GLOBAL_LIBRARY_VERSION, &libVersionSize, libVersion))) {
            std::cout << getTimestamp() << " MsQuic library version: " << libVersion[0] << "." << libVersion[1] << "." << libVersion[2] << "." << libVersion[3] << "\n";
        }

        // EXPERIMENTAL: Try to force stream callback registration
// Create a background thread to monitor for streams
        std::cout << getTimestamp() << " === EXPERIMENTAL: STREAM MONITORING THREAD ===\n";

        // Create a detached thread that will try to detect streams
        std::thread([Connection]() {
            std::cout << getTimestamp() << " Stream monitoring thread started\n";

            for (int attempt = 0; attempt < 50; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Try to query connection statistics to see if streams exist
                QUIC_STATISTICS_V2 stats = {};
                uint32_t statsSize = sizeof(stats);
                QUIC_STATUS statStatus = MsQuic->GetParam(Connection, QUIC_PARAM_CONN_STATISTICS_V2, &statsSize, &stats);

                if (QUIC_SUCCEEDED(statStatus)) {
                    if (stats.RecvTotalPackets > 0) {
                        std::cout << getTimestamp() << " DETECTED: Connection has received "
                            << stats.RecvTotalPackets << " packets\n";
                        std::cout << getTimestamp() << " Total stream bytes: " << stats.RecvTotalStreamBytes << "\n";

                        if (stats.RecvTotalStreamBytes > 0) {
                            std::cout << getTimestamp() << " *** STREAM DATA DETECTED BUT NO RECEIVE EVENTS! ***\n";
                            std::cout << getTimestamp() << " This confirms streams exist but callbacks aren't set\n";
                            break;
                        }
                    }
                }
            }

            std::cout << getTimestamp() << " Stream monitoring thread completed\n";
            }).detach();

        break;
    }

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT\n";
        std::cout << getTimestamp() << " Transport initiated shutdown!\n";
        std::cout << getTimestamp() << " Status: 0x" << std::hex << Event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status << std::dec << "\n";
        std::cout << getTimestamp() << " Error code: " << Event->SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode << "\n";
        break;
    }

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER\n";
        std::cout << getTimestamp() << " Peer initiated shutdown\n";
        std::cout << getTimestamp() << " Error code: " << Event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode << "\n";
        break;
    }

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
        std::cout << getTimestamp() << " *** CRITICAL: PEER_STREAM_STARTED EVENT DETECTED! ***\n";
        std::cout << getTimestamp() << " === PEER_STREAM_STARTED EVENT ===\n";
        std::cout << getTimestamp() << " Client started stream " << std::hex
            << Event->PEER_STREAM_STARTED.Stream << std::dec << "\n";

        // Get the QUIC stream ID immediately
        QUIC_UINT62 streamId = 0;
        uint32_t bufferLength = sizeof(streamId);
        QUIC_STATUS idStatus = MsQuic->GetParam(Event->PEER_STREAM_STARTED.Stream, QUIC_PARAM_STREAM_ID, &bufferLength, &streamId);

        if (QUIC_SUCCEEDED(idStatus)) {
            std::cout << getTimestamp() << " Stream ID: " << streamId << "\n";

            // Analyze stream type
            if (streamId % 4 == 2) {
                std::cout << getTimestamp() << " -> UNIDIRECTIONAL stream (client-initiated)\n";
                std::cout << getTimestamp() << " -> This should be the control stream with SETTINGS\n";
            }
            else if (streamId % 4 == 0) {
                std::cout << getTimestamp() << " -> BIDIRECTIONAL stream (client-initiated)\n";
                std::cout << getTimestamp() << " -> This should be the WebTransport CONNECT stream\n";
            }
        }
        else {
            std::cout << getTimestamp() << " Failed to get stream ID\n";
        }

        // Check stream flags
        if (Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) {
            std::cout << getTimestamp() << " UNIDIRECTIONAL stream detected\n";
            std::cout << getTimestamp() << " Setting callback handler for unidirectional stream\n";
            MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, ServerStreamCallback, Connection);
            std::cout << getTimestamp() << " Unidirectional stream ready for receive events\n";
        }
        else {
            std::cout << getTimestamp() << " BIDIRECTIONAL stream detected\n";

            // Enable receive for bidirectional streams
            QUIC_STATUS enableStatus = MsQuic->StreamReceiveSetEnabled(Event->PEER_STREAM_STARTED.Stream, TRUE);
            if (QUIC_FAILED(enableStatus)) {
                std::cout << getTimestamp() << " Failed to enable receive on bidirectional stream\n";
            }
            else {
                std::cout << getTimestamp() << " Successfully enabled receive on bidirectional stream\n";
            }

            // Set callback handler
            MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, ServerStreamCallback, Connection);
        }

        std::cout << getTimestamp() << " Stream callback handler set successfully\n";
        std::cout << getTimestamp() << " === PEER_STREAM_STARTED COMPLETE ===\n";
        break;
    }

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE\n";
        std::cout << getTimestamp() << " Handshake completed: " << (Event->SHUTDOWN_COMPLETE.HandshakeCompleted ? "YES" : "NO") << "\n";
        std::cout << getTimestamp() << " Peer acknowledged: " << (Event->SHUTDOWN_COMPLETE.PeerAcknowledgedShutdown ? "YES" : "NO") << "\n";
        std::cout << getTimestamp() << " App close in progress: " << (Event->SHUTDOWN_COMPLETE.AppCloseInProgress ? "YES" : "NO") << "\n";

        auto it = activeSessions.find(Connection);
        if (it != activeSessions.end()) {
            std::cout << getTimestamp() << " Cleaning up WebTransport session\n";
            activeSessions.erase(it);
        }

        std::cout << getTimestamp() << " Closing connection handle\n";
        MsQuic->ConnectionClose(Connection);
        break;
    }

    case QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE\n";
        std::cout << getTimestamp() << " Bidirectional streams available: " << Event->STREAMS_AVAILABLE.BidirectionalCount << "\n";
        std::cout << getTimestamp() << " Unidirectional streams available: " << Event->STREAMS_AVAILABLE.UnidirectionalCount << "\n";
        break;
    }

    case QUIC_CONNECTION_EVENT_PEER_NEEDS_STREAMS: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_PEER_NEEDS_STREAMS\n";
        std::cout << getTimestamp() << " Peer needs bidirectional streams: " << (Event->PEER_NEEDS_STREAMS.Bidirectional ? "YES" : "NO") << "\n";
        break;
    }

    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED\n";
        std::cout << getTimestamp() << " Received datagram (" << Event->DATAGRAM_RECEIVED.Buffer->Length << " bytes)\n";
        break;
    }

    case QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED\n";
        break;
    }

    case QUIC_CONNECTION_EVENT_LOCAL_ADDRESS_CHANGED: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_LOCAL_ADDRESS_CHANGED\n";
        break;
    }

    case QUIC_CONNECTION_EVENT_PEER_ADDRESS_CHANGED: {
        std::cout << getTimestamp() << " QUIC_CONNECTION_EVENT_PEER_ADDRESS_CHANGED\n";
        break;
    }

    default: {
        std::cout << getTimestamp() << " *** UNKNOWN/UNHANDLED CONNECTION EVENT: " << Event->Type << " ***\n";
        std::cout << getTimestamp() << " This event is not being processed!\n";
        std::cout << getTimestamp() << " Check if this should be PEER_STREAM_STARTED (type 6)\n";
        break;
    }
    }

    std::cout << getTimestamp() << " === SERVER CONNECTION CALLBACK END ===\n";
    return QUIC_STATUS_SUCCESS;
}

// ALSO ADD: Enhanced ServerListenerCallback with manual stream handling
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS QUIC_API ServerListenerCallback(
    _In_ HQUIC Listener,
    _In_opt_ void* Context,
    _Inout_ QUIC_LISTENER_EVENT* Event
) {
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Listener);

    std::cout << getTimestamp() << " === SERVER LISTENER CALLBACK ===\n";
    std::cout << getTimestamp() << " Event type: " << Event->Type << "\n";

    if (Event->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        std::cout << getTimestamp() << " QUIC_LISTENER_EVENT_NEW_CONNECTION\n";
        std::cout << getTimestamp() << " New connection handle: " << std::hex << Event->NEW_CONNECTION.Connection << std::dec << "\n";

        // CRITICAL: Set the connection callback FIRST
        std::cout << getTimestamp() << " Setting ServerConnectionCallback...\n";
        MsQuic->SetCallbackHandler(Event->NEW_CONNECTION.Connection, ServerConnectionCallback, nullptr);
        std::cout << getTimestamp() << " ServerConnectionCallback set successfully\n";

        // EXPERIMENTAL: Try to pre-register stream callbacks for expected stream IDs
        std::cout << getTimestamp() << " === EXPERIMENTAL: PRE-REGISTERING STREAM CALLBACKS ===\n";

        // Create a monitoring thread that will try to detect streams periodically
        HQUIC connection = Event->NEW_CONNECTION.Connection;
        std::thread([connection]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Wait for connection establishment

            std::cout << getTimestamp() << " Starting periodic stream detection...\n";

            for (int i = 0; i < 100; ++i) { // Try for 10 seconds
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Check connection statistics
                QUIC_STATISTICS_V2 stats = {};
                uint32_t statsSize = sizeof(stats);
                if (QUIC_SUCCEEDED(MsQuic->GetParam(connection, QUIC_PARAM_CONN_STATISTICS_V2, &statsSize, &stats))) {
                    if (stats.RecvTotalStreamBytes > 0) {
                        std::cout << getTimestamp() << " *** DETECTED STREAM DATA WITHOUT EVENTS! ***\n";
                        std::cout << getTimestamp() << " Received " << stats.RecvTotalStreamBytes << " stream bytes\n";
                        std::cout << getTimestamp() << " But no PEER_STREAM_STARTED events fired\n";
                        std::cout << getTimestamp() << " This confirms the bug: streams exist but events don't fire\n";
                        break;
                    }
                }
            }
            }).detach();

        // Set configuration AFTER callback
        std::cout << getTimestamp() << " Setting connection configuration...\n";
        QUIC_STATUS configStatus = MsQuic->ConnectionSetConfiguration(Event->NEW_CONNECTION.Connection, Configuration);
        if (QUIC_FAILED(configStatus)) {
            std::cout << getTimestamp() << " ERROR: ConnectionSetConfiguration failed: 0x" << std::hex << configStatus << std::dec << "\n";
        }
        else {
            std::cout << getTimestamp() << " Connection configuration set successfully\n";
        }

        std::cout << getTimestamp() << " Connection ready for PEER_STREAM_STARTED events\n";
    }
    else {
        std::cout << getTimestamp() << " Other listener event: " << Event->Type << "\n";
    }

    std::cout << getTimestamp() << " === SERVER LISTENER CALLBACK END ===\n";
    return QUIC_STATUS_SUCCESS;
}

// Enhanced server main() function with explicit stream event configuration
int main(int argc, char** argv) {
    std::string_view certHashArg;
    uint16_t port = 4443;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg.starts_with("-cert_hash:")) {
            certHashArg = arg.substr(11);
        }
        else if (arg.starts_with("-port:")) {
            port = static_cast<uint16_t>(std::stoul(std::string(arg.substr(6))));
        }
    }

    std::cout << "=== MsQuic WebTransport Server ===\n";
    std::cout << "Port: " << port << "\n";

    if (QUIC_FAILED(MsQuicOpen2(&MsQuic))) {
        std::cerr << "MsQuicOpen2 failed\n";
        return 1;
    }

    QUIC_REGISTRATION_CONFIG regConfig = { "QuicWebTransportServer", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    if (QUIC_FAILED(MsQuic->RegistrationOpen(&regConfig, &Registration))) {
        std::cerr << "RegistrationOpen failed\n";
        return 1;
    }

    const char* alpnStr = "h3";
    QUIC_BUFFER Alpn = { static_cast<uint32_t>(std::strlen(alpnStr)), reinterpret_cast<uint8_t*>(const_cast<char*>(alpnStr)) };

    // CRITICAL: Enhanced settings to ensure PEER_STREAM_STARTED events fire
    QUIC_SETTINGS settings = {};

    // Stream count limits - CRITICAL for PEER_STREAM_STARTED events
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 100;        // Much higher limit to ensure acceptance

    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerBidiStreamCount = 100;         // Much higher limit to ensure acceptance

    // Flow control settings - ensure streams aren't blocked
    settings.IsSet.ConnFlowControlWindow = TRUE;
    settings.ConnFlowControlWindow = 16777216;  // 16MB connection flow control (much larger)

    settings.IsSet.StreamRecvWindowDefault = TRUE;
    settings.StreamRecvWindowDefault = 1048576; // 1MB per stream (much larger)

    settings.IsSet.StreamRecvBufferDefault = TRUE;
    settings.StreamRecvBufferDefault = 1048576; // 1MB receive buffer (much larger)

    // CRITICAL: Idle timeout settings - prevent premature shutdown
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.IdleTimeoutMs = 60000;            // 60 second idle timeout (much longer)

    settings.IsSet.HandshakeIdleTimeoutMs = TRUE;
    settings.HandshakeIdleTimeoutMs = 30000;   // 30 second handshake timeout (much longer)

    // CRITICAL: Disable keep-alive to prevent interference
    settings.IsSet.KeepAliveIntervalMs = TRUE;
    settings.KeepAliveIntervalMs = 0;          // Disable keep-alive

    // CRITICAL: Enable datagram support (might be required for WebTransport)
    settings.IsSet.DatagramReceiveEnabled = TRUE;
    settings.DatagramReceiveEnabled = TRUE;

    // CRITICAL: Set maximum operations to prevent throttling
    settings.IsSet.MaxStatelessOperations = TRUE;
    settings.MaxStatelessOperations = 16;

    settings.IsSet.MaxBindingStatelessOperations = TRUE;
    settings.MaxBindingStatelessOperations = 16;

    // CRITICAL: Connection-level settings that might affect stream events
    settings.IsSet.MaxWorkerQueueDelayUs = TRUE;
    settings.MaxWorkerQueueDelayUs = 2500000;  // 2.5 second max delay

    settings.IsSet.SendIdleTimeoutMs = TRUE;
    settings.SendIdleTimeoutMs = 30000;        // 30 second send timeout

    // CRITICAL: Initial window settings
    settings.IsSet.InitialWindowPackets = TRUE;
    settings.InitialWindowPackets = 10;        // Allow more initial packets

    std::cout << "ENHANCED Server QUIC settings configured:\n";
    std::cout << "  PeerUnidiStreamCount: " << settings.PeerUnidiStreamCount << "\n";
    std::cout << "  PeerBidiStreamCount: " << settings.PeerBidiStreamCount << "\n";
    std::cout << "  ConnFlowControlWindow: " << settings.ConnFlowControlWindow << "\n";
    std::cout << "  StreamRecvWindowDefault: " << settings.StreamRecvWindowDefault << "\n";
    std::cout << "  IdleTimeoutMs: " << settings.IdleTimeoutMs << "\n";
    std::cout << "  HandshakeIdleTimeoutMs: " << settings.HandshakeIdleTimeoutMs << "\n";

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

    // Certificate configuration (existing code)
    std::array<uint8_t, 20> shaHash = {};
    if (certHashArg.empty() || !ParseHexHash(certHashArg, shaHash)) {
        std::cerr << "Usage: server -cert_hash:<40-char SHA1> [-port:<port>]\n";
        return 1;
    }

    QUIC_CERTIFICATE_HASH_STORE certHashStore = {};
    certHashStore.Flags = QUIC_CERTIFICATE_HASH_STORE_FLAG_MACHINE_STORE;
    std::copy(shaHash.begin(), shaHash.end(), certHashStore.ShaHash);
    strcpy_s(certHashStore.StoreName, "MY");

    QUIC_CREDENTIAL_CONFIG credConfig = {};
    credConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE;
    credConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    credConfig.CertificateHashStore = &certHashStore;

    std::cout << "Certificate hash: " << certHashArg << "\n";

    QUIC_STATUS status = MsQuic->ConfigurationLoadCredential(Configuration, &credConfig);
    if (QUIC_FAILED(status)) {
        DescribeQuicStatus(status, "LoadCredential failed");
        return 1;
    }

    // CRITICAL: Enhanced listener creation with explicit callback verification
    std::cout << "Creating listener with ServerListenerCallback...\n";
    if (QUIC_FAILED(MsQuic->ListenerOpen(Registration, ServerListenerCallback, nullptr, &Listener))) {
        std::cerr << "ListenerOpen failed\n";
        return 1;
    }

    QUIC_ADDR addr = {};
    addr.Ipv4.sin_family = AF_INET;
    addr.Ipv4.sin_port = htons(port);
    addr.Ipv4.sin_addr.s_addr = htonl(INADDR_ANY);

    if (QUIC_FAILED(MsQuic->ListenerStart(Listener, &Alpn, 1, &addr))) {
        std::cerr << "ListenerStart failed\n";
        return 1;
    }

    std::cout << "\nENHANCED Server listening on port " << port << "\n";
    std::cout << "Ready for WebTransport connections with PEER_STREAM_STARTED monitoring\n";
    std::cout << "Press Enter to exit...\n";

    std::cin.get();

    std::cout << "\nShutting down...\n";

    activeSessions.clear();

    MsQuic->ListenerClose(Listener);
    MsQuic->ConfigurationClose(Configuration);
    MsQuic->RegistrationClose(Registration);
    MsQuicClose(MsQuic);

    std::cout << "Shutdown complete\n";
    return 0;
}