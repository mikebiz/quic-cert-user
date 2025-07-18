// integrated_server.cpp - MsQuic Server with HTTP/3 WebTransport Support
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

#pragma comment(lib, "msquic.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Ws2_32.lib")

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

// Stream callback implementation
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS QUIC_API ServerStreamCallback(
    _In_ HQUIC Stream,
    _In_opt_ void* Context,
    _Inout_ QUIC_STREAM_EVENT* Event
) {
    switch (Event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE: {
        // Get the actual QUIC stream ID FIRST
        QUIC_UINT62 streamId = 0;
        uint32_t bufferLength = sizeof(streamId);
        QUIC_STATUS status = MsQuic->GetParam(Stream, QUIC_PARAM_STREAM_ID, &bufferLength, &streamId);

        std::cout << "[Server] Received " << Event->RECEIVE.Buffers->Length
            << " bytes on QUIC stream ID " << streamId << std::endl;

        // Show the first 16 bytes of raw data
        std::cout << "[Server] First 16 bytes: ";
        for (uint32_t i = 0; i < Event->RECEIVE.Buffers->Length && i < 16; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                << (int)Event->RECEIVE.Buffers->Buffer[i] << " ";
        }
        std::cout << std::dec << std::endl;

        // Safely copy the buffer data immediately
        std::vector<uint8_t> data;
        data.resize(Event->RECEIVE.Buffers->Length);
        std::memcpy(data.data(), Event->RECEIVE.Buffers->Buffer, Event->RECEIVE.Buffers->Length);

        // CRITICAL: Tell MsQuic we've consumed all the data
        MsQuic->StreamReceiveComplete(Stream, Event->RECEIVE.Buffers->Length);

        // Handle control stream (ID 2) differently
        if (streamId == 2 && !data.empty()) {
            std::cout << "[Server] Processing control stream\n";

            // First byte should be stream type (0x00 for control stream)
            if (data[0] == 0x00) {
                std::cout << "[Server] Found control stream type identifier (0x00)\n";

                // Remove the stream type byte and parse the rest as HTTP/3 frames
                std::vector<uint8_t> frameData(data.begin() + 1, data.end());

                std::cout << "[Server] Frame data after removing stream type: ";
                for (size_t i = 0; i < frameData.size() && i < 16; ++i) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)frameData[i] << " ";
                }
                std::cout << std::dec << std::endl;

                // Now parse as HTTP/3 frame
                Http3FrameParser parser;
                auto frame = parser.parseFrame(frameData);

                if (frame.valid && frame.type == 0x04) { // SETTINGS frame
                    std::cout << "[Server] Successfully parsed SETTINGS frame from control stream!\n";
                    // Don't send response on control stream
                }
                else {
                    std::cout << "[Server] Failed to parse control stream frame: " << frame.error << "\n";
                }
            }
            else {
                std::cout << "[Server] ERROR: Expected control stream type 0x00, got: " << std::hex << (int)data[0] << std::dec << "\n";
            }

            return QUIC_STATUS_SUCCESS; // Don't process control stream further
        }

        // Handle bidirectional stream (ID 0) - your existing HEADERS logic
        if (streamId == 0) {
            std::cout << "[Server] Processing bidirectional request stream\n";

            Http3FrameParser parser;
            auto frame = parser.parseFrame(data);

            if (frame.valid && frame.type == 0x01) { // HEADERS frame
                std::cout << "[Server] Processing HEADERS frame\n";

                QpackDecoder decoder;
                std::vector<QpackDecoder::Header> headers;

                if (decoder.decodeHeaders(frame.payload, headers)) {
                    std::cout << "[Server] Decoded " << headers.size() << " headers:\n";
                    for (const auto& header : headers) {
                        std::cout << "  " << header.name << ": " << header.value << "\n";
                    }

                    WebTransportValidator validator;
                    auto result = validator.validate(headers);

                    if (result.isValid && result.isWebTransport) {
                        std::cout << "[Server] Valid WebTransport request!\n";

                        // Store session
                        HQUIC connection = reinterpret_cast<HQUIC>(Context);
                        if (connection) {
                            activeSessions[connection] = { connection, result.authority, result.path, true };
                        }

                        // Send 200 OK
                        auto response = createHttp3Response(200);
                        QUIC_BUFFER responseBuf = {};
                        responseBuf.Buffer = response.data();
                        responseBuf.Length = static_cast<uint32_t>(response.size());

                        MsQuic->StreamSend(Stream, &responseBuf, 1, QUIC_SEND_FLAG_NONE, nullptr);
                        std::cout << "[Server] Sent HTTP/3 200 OK\n";
                    }
                    else {
                        // Send 400
                        auto response = createHttp3Response(400);
                        QUIC_BUFFER responseBuf = {};
                        responseBuf.Buffer = response.data();
                        responseBuf.Length = static_cast<uint32_t>(response.size());

                        MsQuic->StreamSend(Stream, &responseBuf, 1, QUIC_SEND_FLAG_FIN, nullptr);
                        std::cout << "[Server] Sent HTTP/3 400 Bad Request: " << result.message << "\n";
                    }
                }
                else {
                    std::cout << "[Server] Failed to decode QPACK headers\n";
                }
            }
            else {
                std::cout << "[Server] Failed to parse HTTP/3 frame: " << frame.error << "\n";
            }

            return QUIC_STATUS_SUCCESS; // Important: return here
        }
        break;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        std::cout << "[Server] Send completed\n";
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        std::cout << "[Server] Stream shutdown\n";
        MsQuic->StreamClose(Stream);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

// Connection callback implementation
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS QUIC_API ServerConnectionCallback(
    _In_ HQUIC Connection,
    _In_opt_ void* Context,
    _Inout_ QUIC_CONNECTION_EVENT* Event
) {
    UNREFERENCED_PARAMETER(Context);

    switch (Event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        std::cout << "[Server] Client connected\n";
        break;

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:{
        std::cout << "[ServerConnectionCallback] Client started stream " << std::hex
            << Event->PEER_STREAM_STARTED.Stream << std::dec;

        // Get the QUIC stream ID
        QUIC_UINT62 streamId = 0;
        uint32_t bufferLength = sizeof(streamId);
        QUIC_STATUS idStatus = MsQuic->GetParam(Event->PEER_STREAM_STARTED.Stream, QUIC_PARAM_STREAM_ID, &bufferLength, &streamId);

        if (QUIC_SUCCEEDED(idStatus)) {
            std::cout << " (QUIC ID " << streamId << ")";
        }

        if (Event->PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) {
            std::cout << " UNIDIRECTIONAL\n";
        }
        else {
            std::cout << " BIDIRECTIONAL\n";
            std::cout << "[Server] Enabling receive on bidirectional stream " << streamId << "\n";

            QUIC_STATUS enableStatus = MsQuic->StreamReceiveSetEnabled(Event->PEER_STREAM_STARTED.Stream, TRUE);
            if (QUIC_FAILED(enableStatus)) {
                DescribeQuicStatus(enableStatus, "[Server] Failed to enable receive on stream");
            }
            else {
                std::cout << "[Server] Successfully enabled receive on stream " << streamId << "\n";

                // ADD THIS: Force the stream to start receiving
                QUIC_STATUS startStatus = MsQuic->StreamStart(Event->PEER_STREAM_STARTED.Stream, QUIC_STREAM_START_FLAG_IMMEDIATE);
                if (QUIC_FAILED(startStatus)) {
                    DescribeQuicStatus(startStatus, "[Server] Failed to start bidirectional stream");
                }
                else {
                    std::cout << "[Server] Successfully started bidirectional stream " << streamId << "\n";
                }
            }
        }
        MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, ServerStreamCallback, Connection);
        break;
    }
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
        std::cout << "[Server] Connection shutdown\n";

        auto it = activeSessions.find(Connection);
        if (it != activeSessions.end()) {
            std::cout << "[Server] Cleaning up session\n";
            activeSessions.erase(it);
        }

        MsQuic->ConnectionClose(Connection);
        break;
    }

    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
        std::cout << "[Server] Received datagram\n";
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

// Listener callback implementation
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS QUIC_API ServerListenerCallback(
    _In_ HQUIC Listener,
    _In_opt_ void* Context,
    _Inout_ QUIC_LISTENER_EVENT* Event
) {
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Listener);

    if (Event->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        std::cout << "[Server] New connection\n";
        MsQuic->SetCallbackHandler(Event->NEW_CONNECTION.Connection, ServerConnectionCallback, nullptr);
        MsQuic->ConnectionSetConfiguration(Event->NEW_CONNECTION.Connection, Configuration);
    }

    return QUIC_STATUS_SUCCESS;
}

// Main function
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

    QUIC_SETTINGS settings = {};
    settings.IsSet.PeerUnidiStreamCount = TRUE;
    settings.PeerUnidiStreamCount = 8;
    settings.IsSet.PeerBidiStreamCount = TRUE;
    settings.PeerBidiStreamCount = 8;
    settings.IsSet.DatagramReceiveEnabled = TRUE;
    settings.DatagramReceiveEnabled = TRUE;

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

    std::cout << "\nServer listening on port " << port << "\n";
    std::cout << "Ready for WebTransport connections\n";
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