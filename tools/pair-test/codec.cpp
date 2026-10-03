// Record-only test boundary around the production protocol codec.
// This executable has no network or native input-device implementation.
#include "barrier/ProtocolUtil.h"
#include "barrier/protocol_types.h"
#include "io/IStream.h"
#include "io/XIO.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

class MemoryStream : public barrier::IStream {
public:
    std::vector<unsigned char> bytes;
    size_t position = 0;
    void close() override { position = bytes.size(); }
    UInt32 read(void* target, UInt32 count) override {
        count = static_cast<UInt32>(std::min<size_t>(count, getSize()));
        if (target && count) std::memcpy(target, bytes.data() + position, count);
        position += count;
        return count;
    }
    void write(const void* data, UInt32 count) override {
        const auto* first = static_cast<const unsigned char*>(data);
        bytes.insert(bytes.end(), first, first + count);
    }
    void flush() override {}
    void shutdownInput() override { close(); }
    void shutdownOutput() override {}
    void* getEventTarget() const override { return nullptr; }
    bool isReady() const override { return getSize() != 0; }
    UInt32 getSize() const override { return static_cast<UInt32>(bytes.size() - position); }
};

unsigned number(const char* text, unsigned maximum) {
    size_t consumed = 0;
    const std::string input(text);
    if (input.empty() || input.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("expected unsigned decimal integer");
    unsigned long value = std::stoul(input, &consumed);
    if (consumed != input.size() || value > maximum)
        throw std::runtime_error("integer out of range");
    return static_cast<unsigned>(value);
}

void checkName(const std::string& name) {
    if (name.empty() || name.size() > 64 ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") != std::string::npos)
        throw std::runtime_error("invalid test peer name");
}

void require(bool condition) {
    if (!condition) throw std::runtime_error("invalid or truncated production protocol message");
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "describe") {
            std::cout << "{\"protocol\":[" << kProtocolMajorVersion << ',' << kProtocolMinorVersion
                      << "],\"source_commit\":\"" << PAIR_SOURCE_COMMIT
                      << "\",\"source_digest\":\"" << PAIR_SOURCE_DIGEST
                      << "\",\"source_fingerprint\":\"" << PAIR_SOURCE_FINGERPRINT
                      << "\",\"source_dirty\":" << (PAIR_SOURCE_DIRTY ? "true" : "false")
                      << ",\"boundary\":\"production_ProtocolUtil\",\"native_input\":false}\n";
            return 0;
        }
        if (argc < 3) throw std::runtime_error("usage: pair-codec encode EVENT [ARGS] | decode HEX | describe");
        MemoryStream stream;
        const std::string mode(argv[1]);
        if (mode == "encode") {
            const std::string event(argv[2]);
            if (event == "hello" && argc == 3)
                ProtocolUtil::writef(&stream, kMsgHello, kProtocolMajorVersion, kProtocolMinorVersion);
            else if (event == "hello_back" && argc == 4) {
                const std::string name(argv[3]);
                checkName(name);
                ProtocolUtil::writef(&stream, kMsgHelloBack, kProtocolMajorVersion, kProtocolMinorVersion, &name);
            }
            else if ((event == "key_down" || event == "key_up") && argc == 6)
                ProtocolUtil::writef(&stream, event == "key_down" ? kMsgDKeyDown : kMsgDKeyUp,
                    number(argv[3], 65535), number(argv[4], 65535), number(argv[5], 65535));
            else if ((event == "mouse_down" || event == "mouse_up") && argc == 4)
                ProtocolUtil::writef(&stream, event == "mouse_down" ? kMsgDMouseDown : kMsgDMouseUp,
                    number(argv[3], 255));
            else if (event == "noop" && argc == 3)
                ProtocolUtil::writef(&stream, kMsgCNoop);
            else throw std::runtime_error("unknown event or argument count");
            std::ostringstream hex;
            for (unsigned char byte : stream.bytes)
                hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
            std::cout << "{\"hex\":\"" << hex.str() << "\"}\n";
        }
        else if (mode == "decode" && argc == 3) {
            const std::string hex(argv[2]);
            if (hex.empty() || hex.size() % 2 || hex.size() > 1024 ||
                hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                throw std::runtime_error("invalid hex message");
            for (size_t i = 0; i < hex.size(); i += 2)
                stream.bytes.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
            std::ostringstream result;
            const std::string code(stream.bytes.begin(), stream.bytes.begin() + std::min<size_t>(4, stream.bytes.size()));
            if (stream.bytes.size() >= 7 && std::memcmp(stream.bytes.data(), "Barrier", 7) == 0) {
                UInt16 major = 0, minor = 0;
                if (stream.bytes.size() == 11) {
                    require(ProtocolUtil::readf(&stream, kMsgHello, &major, &minor));
                    result << "{\"event\":\"hello\",\"protocol\":[" << major << ',' << minor << "]}";
                }
                else {
                    // Bound the test peer name before the production parser
                    // allocates its length field. This is a smoke-only subset.
                    require(stream.bytes.size() >= 15);
                    const unsigned length = (unsigned(stream.bytes[11]) << 24) |
                        (unsigned(stream.bytes[12]) << 16) |
                        (unsigned(stream.bytes[13]) << 8) | unsigned(stream.bytes[14]);
                    require(length > 0 && length <= 64 && stream.bytes.size() == 15 + length);
                    std::string name;
                    require(ProtocolUtil::readf(&stream, kMsgHelloBack, &major, &minor, &name));
                    checkName(name);
                    result << "{\"event\":\"hello_back\",\"protocol\":[" << major << ',' << minor
                           << "],\"name\":\"" << name << "\"}";
                }
            }
            else if (code == "DKDN" || code == "DKUP") {
                UInt16 key = 0, mask = 0, button = 0;
                require(ProtocolUtil::readf(&stream, code == "DKDN" ? kMsgDKeyDown : kMsgDKeyUp,
                    &key, &mask, &button));
                result << "{\"event\":\"" << (code == "DKDN" ? "key_down" : "key_up")
                       << "\",\"key\":" << key << ",\"mask\":" << mask << ",\"button\":" << button << '}';
            }
            else if (code == "DMDN" || code == "DMUP") {
                UInt8 button = 0;
                require(ProtocolUtil::readf(&stream, code == "DMDN" ? kMsgDMouseDown : kMsgDMouseUp, &button));
                result << "{\"event\":\"" << (code == "DMDN" ? "mouse_down" : "mouse_up")
                       << "\",\"button\":" << static_cast<unsigned>(button) << '}';
            }
            else if (code == "CNOP") {
                require(ProtocolUtil::readf(&stream, kMsgCNoop));
                result << "{\"event\":\"noop\"}";
            }
            else throw std::runtime_error("message not in the smoke subset");
            require(stream.getSize() == 0);
            std::cout << result.str() << '\n';
        }
        else throw std::runtime_error("invalid codec operation");
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
