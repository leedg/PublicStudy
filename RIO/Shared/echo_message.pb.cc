#include "echo_message.pb.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

inline void AppendVarint32(uint32_t value, std::string& out) {
    while (value > 0x7F) {
        out.push_back(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

inline bool ParseVarint32(const uint8_t*& ptr, const uint8_t* end, uint32_t& value) {
    uint32_t result = 0;
    int shift = 0;

    while (ptr < end && shift <= 28) {
        uint8_t byte = *ptr++;
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            value = result;
            return true;
        }
        shift += 7;
    }

    return false;
}

inline bool SkipField(uint32_t wireType, const uint8_t*& ptr, const uint8_t* end) {
    switch (wireType) {
    case 0: { // Varint
        uint32_t dummy = 0;
        return ParseVarint32(ptr, end, dummy);
    }
    case 2: { // Length-delimited
        uint32_t len = 0;
        if (!ParseVarint32(ptr, end, len)) {
            return false;
        }
        if (ptr + len > end) {
            return false;
        }
        ptr += len;
        return true;
    }
    default:
        return false;
    }
}

inline int VarintSize32(uint32_t value) {
    int size = 1;
    while (value > 0x7F) {
        value >>= 7;
        ++size;
    }
    return size;
}

} // namespace

namespace rio::echo {

bool EchoMessage::SerializeToString(std::string* out) const {
    if (!out) {
        return false;
    }

    out->clear();
    out->reserve(ByteSizeLong());

    out->push_back(static_cast<char>((1 << 3) | 2)); // field 1, wire type 2
    AppendVarint32(static_cast<uint32_t>(text_.size()), *out);
    out->append(text_);

    return true;
}

bool EchoMessage::ParseFromArray(const void* data, int size) {
    if (!data || size < 0) {
        return false;
    }

    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    const uint8_t* end = ptr + size;
    text_.clear();

    while (ptr < end) {
        uint32_t key = 0;
        if (!ParseVarint32(ptr, end, key)) {
            return false;
        }

        uint32_t fieldNumber = key >> 3;
        uint32_t wireType = key & 0x7;

        if (fieldNumber == 1 && wireType == 2) {
            uint32_t len = 0;
            if (!ParseVarint32(ptr, end, len)) {
                return false;
            }
            if (ptr + len > end) {
                return false;
            }
            text_.assign(reinterpret_cast<const char*>(ptr), len);
            ptr += len;
        } else {
            if (!SkipField(wireType, ptr, end)) {
                return false;
            }
        }
    }

    return true;
}

int EchoMessage::ByteSizeLong() const {
    const uint32_t payloadLen = static_cast<uint32_t>(text_.size());
    return 1 + VarintSize32(payloadLen) + static_cast<int>(payloadLen);
}

} // namespace rio::echo
