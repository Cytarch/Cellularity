#pragma once

#include "tests/support/TestAssertions.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <zlib.h>

namespace cellularity::tests {

inline std::vector<uint8_t> bytes(std::initializer_list<uint8_t> values) {
    return std::vector<uint8_t>(values);
}

inline std::vector<uint8_t> javaPrimitiveStream(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> stream{0xac, 0xed, 0x00, 0x05};
    if (payload.size() <= 0xff) {
        stream.push_back(0x77);
        stream.push_back(static_cast<uint8_t>(payload.size()));
    } else {
        stream.push_back(0x7a);
        const uint32_t size = static_cast<uint32_t>(payload.size());
        stream.push_back(static_cast<uint8_t>((size >> 24) & 0xff));
        stream.push_back(static_cast<uint8_t>((size >> 16) & 0xff));
        stream.push_back(static_cast<uint8_t>((size >> 8) & 0xff));
        stream.push_back(static_cast<uint8_t>(size & 0xff));
    }
    stream.insert(stream.end(), payload.begin(), payload.end());
    return stream;
}

inline std::vector<uint8_t> gzipBytes(const std::vector<uint8_t>& input) {
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (deflateInit2(&stream,
                Z_DEFAULT_COMPRESSION,
                Z_DEFLATED,
                16 + MAX_WBITS,
                8,
                Z_DEFAULT_STRATEGY)
            != Z_OK) {
        fail("Could not initialize synthetic legacy gzip stream");
    }

    std::vector<uint8_t> output;
    std::array<uint8_t, 256> buffer{};
    int status;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = deflate(&stream, Z_FINISH);
        if (status != Z_OK && status != Z_STREAM_END) {
            deflateEnd(&stream);
            fail("Could not create synthetic legacy gzip stream");
        }
        const size_t produced = buffer.size() - stream.avail_out;
        output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    } while (status != Z_STREAM_END);
    deflateEnd(&stream);
    return output;
}

} // namespace cellularity::tests
