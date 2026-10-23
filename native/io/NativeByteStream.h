#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <new>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "formats/FormatLimits.h"
#include "io/DecodeError.h"

class ByteReader {
public:
    explicit ByteReader(std::vector<uint8_t> bytes) : mBytes(std::move(bytes)) {
        mData = mBytes.data();
        mDataSize = mBytes.size();
    }

    /** Reads immutable caller-owned memory without copying it. */
    ByteReader(const uint8_t* bytes, size_t byteCount)
            : mData(bytes), mDataSize(byteCount) {
        if (bytes == nullptr && byteCount != 0) {
            throw DecodeError(DecodeErrorCode::InvalidValue,
                    "Native snapshot memory is null");
        }
    }

    /** Reads a bounded payload without retaining the complete file in memory. */
    ByteReader(std::istream& input, std::uint64_t byteCount)
            : mInput(&input) {
        if (byteCount > std::numeric_limits<size_t>::max()) {
            throw DecodeError(DecodeErrorCode::LimitExceeded,
                    "Native snapshot input exceeds the addressable stream size");
        }
        mStreamRemaining = static_cast<size_t>(byteCount);
    }

    ByteReader(const ByteReader&) = delete;
    ByteReader& operator=(const ByteReader&) = delete;
    ByteReader(ByteReader&&) = delete;
    ByteReader& operator=(ByteReader&&) = delete;

    bool readBool() {
        return readByte() != 0;
    }

    int16_t readShort() {
        uint16_t value = static_cast<uint16_t>(readByte()) << 8;
        value |= readByte();
        return static_cast<int16_t>(value);
    }

    int32_t readInt() {
        uint32_t value = static_cast<uint32_t>(readByte()) << 24;
        value |= static_cast<uint32_t>(readByte()) << 16;
        value |= static_cast<uint32_t>(readByte()) << 8;
        value |= readByte();
        return static_cast<int32_t>(value);
    }

    float readFloat() {
        uint32_t bits = static_cast<uint32_t>(readByte()) << 24;
        bits |= static_cast<uint32_t>(readByte()) << 16;
        bits |= static_cast<uint32_t>(readByte()) << 8;
        bits |= readByte();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    double readDouble() {
        uint64_t bits = static_cast<uint64_t>(readByte()) << 56;
        bits |= static_cast<uint64_t>(readByte()) << 48;
        bits |= static_cast<uint64_t>(readByte()) << 40;
        bits |= static_cast<uint64_t>(readByte()) << 32;
        bits |= static_cast<uint64_t>(readByte()) << 24;
        bits |= static_cast<uint64_t>(readByte()) << 16;
        bits |= static_cast<uint64_t>(readByte()) << 8;
        bits |= readByte();
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    int64_t readLong() {
        uint64_t value = static_cast<uint64_t>(readByte()) << 56;
        value |= static_cast<uint64_t>(readByte()) << 48;
        value |= static_cast<uint64_t>(readByte()) << 40;
        value |= static_cast<uint64_t>(readByte()) << 32;
        value |= static_cast<uint64_t>(readByte()) << 24;
        value |= static_cast<uint64_t>(readByte()) << 16;
        value |= static_cast<uint64_t>(readByte()) << 8;
        value |= readByte();
        return static_cast<int64_t>(value);
    }

    std::vector<uint8_t> readBytes(int32_t count) {
        if (count < 0) {
            throw DecodeError(DecodeErrorCode::InvalidLength,
                    "Negative native snapshot byte array length");
        }
        if (static_cast<size_t>(count) > remaining()) {
            throw DecodeError(DecodeErrorCode::TruncatedInput,
                    "Native snapshot byte array ended before the declared byte array");
        }
        std::vector<uint8_t> result;
        try {
            result.resize(static_cast<size_t>(count));
        } catch (const std::bad_alloc&) {
            throw DecodeError(DecodeErrorCode::OutOfMemory,
                    "Could not allocate native snapshot byte array");
        } catch (const std::length_error&) {
            throw DecodeError(DecodeErrorCode::LimitExceeded,
                    "Native snapshot byte array exceeds the container limit");
        }
        if (mInput != nullptr) {
            if (!result.empty()) {
                mInput->read(reinterpret_cast<char*>(result.data()),
                        static_cast<std::streamsize>(result.size()));
                if (!*mInput) {
                    throw DecodeError(DecodeErrorCode::TruncatedInput,
                            "Native snapshot stream ended before the declared byte array");
                }
            }
            mStreamRemaining -= result.size();
        } else {
            std::copy_n(mData + mPosition, result.size(), result.begin());
            mPosition += result.size();
        }
        return result;
    }

    size_t remaining() const {
        return mInput != nullptr ? mStreamRemaining : mDataSize - mPosition;
    }

private:
    std::vector<uint8_t> mBytes;
    const uint8_t* mData = nullptr;
    size_t mDataSize = 0;
    std::istream* mInput = nullptr;
    size_t mPosition = 0;
    size_t mStreamRemaining = 0;

    uint8_t readByte() {
        if (remaining() == 0) {
            throw DecodeError(DecodeErrorCode::TruncatedInput,
                    "Native snapshot byte array ended unexpectedly");
        }
        if (mInput != nullptr) {
            const int value = mInput->get();
            if (value == std::char_traits<char>::eof()) {
                throw DecodeError(DecodeErrorCode::TruncatedInput,
                        "Native snapshot stream ended unexpectedly");
            }
            mStreamRemaining--;
            return static_cast<uint8_t>(value);
        }
        return mData[mPosition++];
    }
};

class ByteWriter {
public:
    static constexpr std::uint64_t MAX_BYTES =
            cellularity::format_limits::kMaximumSaveFileBytes;

    ByteWriter() = default;

    explicit ByteWriter(std::uint64_t maximumBytes)
            : mMaximumBytes(validateMaximumBytes(maximumBytes)) {
    }

    /** Writes through a bounded buffer instead of retaining the complete payload. */
    explicit ByteWriter(std::ostream& output) : ByteWriter(output, MAX_BYTES) {
    }

    ByteWriter(std::ostream& output, std::uint64_t maximumBytes)
            : mOutput(&output),
              mMaximumBytes(validateMaximumBytes(maximumBytes)) {
        try {
            mBytes.reserve(STREAM_BUFFER_SIZE);
        } catch (const std::bad_alloc&) {
            throw DecodeError(DecodeErrorCode::OutOfMemory,
                    "Could not allocate native snapshot output buffer");
        }
    }

    void writeByte(uint8_t value) {
        appendByte(value);
    }

    void writeBool(bool value) {
        appendByte(value ? 1 : 0);
    }

    void writeShort(int16_t value) {
        auto bits = static_cast<uint16_t>(value);
        appendByte(static_cast<uint8_t>((bits >> 8) & 0xff));
        appendByte(static_cast<uint8_t>(bits & 0xff));
    }

    void writeInt(int32_t value) {
        auto bits = static_cast<uint32_t>(value);
        appendByte(static_cast<uint8_t>((bits >> 24) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 16) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 8) & 0xff));
        appendByte(static_cast<uint8_t>(bits & 0xff));
    }

    void writeFloat(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(value));
        writeInt(static_cast<int32_t>(bits));
    }

    void writeDouble(double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(value));
        appendByte(static_cast<uint8_t>((bits >> 56) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 48) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 40) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 32) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 24) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 16) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 8) & 0xff));
        appendByte(static_cast<uint8_t>(bits & 0xff));
    }

    void writeLong(int64_t value) {
        auto bits = static_cast<uint64_t>(value);
        appendByte(static_cast<uint8_t>((bits >> 56) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 48) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 40) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 32) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 24) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 16) & 0xff));
        appendByte(static_cast<uint8_t>((bits >> 8) & 0xff));
        appendByte(static_cast<uint8_t>(bits & 0xff));
    }

    void writeRawBytes(const std::vector<uint8_t>& values) {
        ensureCapacity(values.size());
        if (mOutput == nullptr) {
            try {
                mBytes.insert(mBytes.end(), values.begin(), values.end());
            } catch (const std::bad_alloc&) {
                throw DecodeError(DecodeErrorCode::OutOfMemory,
                        "Could not allocate native snapshot output memory");
            } catch (const std::length_error&) {
                throw DecodeError(DecodeErrorCode::LimitExceeded,
                        "Native snapshot output exceeds the container size limit");
            }
            mWrittenBytes += values.size();
            return;
        }

        size_t position = 0;
        while (position < values.size()) {
            const size_t available = STREAM_BUFFER_SIZE - mBytes.size();
            const size_t count = std::min(available, values.size() - position);
            try {
                mBytes.insert(mBytes.end(),
                        values.begin() + static_cast<std::ptrdiff_t>(position),
                        values.begin() + static_cast<std::ptrdiff_t>(position + count));
            } catch (const std::bad_alloc&) {
                throw DecodeError(DecodeErrorCode::OutOfMemory,
                        "Could not allocate native snapshot output buffer");
            }
            position += count;
            if (mBytes.size() == STREAM_BUFFER_SIZE) {
                flushStreamBuffer();
            }
        }
        mWrittenBytes += values.size();
    }

    void finish() {
        if (mOutput != nullptr) {
            flushStreamBuffer();
            mOutput->flush();
            if (!*mOutput) {
                throw DecodeError(DecodeErrorCode::IoFailure,
                        "Could not flush native snapshot output");
            }
        }
    }

    std::uint64_t size() const {
        return mWrittenBytes;
    }

    const std::vector<uint8_t>& bytes() const {
        if (mOutput != nullptr) {
            throw std::logic_error("Streaming ByteWriter does not retain its output");
        }
        return mBytes;
    }

    std::vector<uint8_t> takeBytes() {
        if (mOutput != nullptr) {
            throw std::logic_error("Streaming ByteWriter does not retain its output");
        }
        return std::move(mBytes);
    }

private:
    static constexpr size_t STREAM_BUFFER_SIZE = 64 * 1024;

    void appendByte(uint8_t value) {
        ensureCapacity(1);
        try {
            mBytes.push_back(value);
        } catch (const std::bad_alloc&) {
            throw DecodeError(DecodeErrorCode::OutOfMemory,
                    "Could not allocate native snapshot output memory");
        } catch (const std::length_error&) {
            throw DecodeError(DecodeErrorCode::LimitExceeded,
                    "Native snapshot output exceeds the container size limit");
        }
        mWrittenBytes++;
        if (mOutput != nullptr && mBytes.size() == STREAM_BUFFER_SIZE) {
            flushStreamBuffer();
        }
    }

    void ensureCapacity(size_t additionalBytes) const {
        if (static_cast<std::uint64_t>(additionalBytes)
                > mMaximumBytes - mWrittenBytes) {
            throw DecodeError(DecodeErrorCode::LimitExceeded,
                    "Native snapshot output exceeds the configured safety limit");
        }
    }

    void flushStreamBuffer() {
        if (mBytes.empty()) {
            return;
        }
        mOutput->write(reinterpret_cast<const char*>(mBytes.data()),
                static_cast<std::streamsize>(mBytes.size()));
        if (!*mOutput) {
            throw DecodeError(DecodeErrorCode::IoFailure,
                    "Could not write native snapshot output");
        }
        mBytes.clear();
    }

    static std::uint64_t validateMaximumBytes(std::uint64_t maximumBytes) {
        if (maximumBytes == 0 || maximumBytes > MAX_BYTES) {
            throw DecodeError(DecodeErrorCode::InvalidLength,
                    "Invalid native snapshot output limit");
        }
        return maximumBytes;
    }

    std::ostream* mOutput = nullptr;
    std::vector<uint8_t> mBytes;
    std::uint64_t mWrittenBytes = 0;
    std::uint64_t mMaximumBytes = MAX_BYTES;
};
