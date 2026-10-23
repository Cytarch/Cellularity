#include "formats/SnapshotFileIO.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <zlib.h>

#include "core/SimulationMemoryPolicy.h"
#include "formats/SnapshotCodecSupport.h"

namespace cellularity::snapshot_codec {

TemporarySnapshotFile::TemporarySnapshotFile(const std::string& adjacentPath) {
#if defined(_WIN32)
    char temporaryName[L_tmpnam_s] = {};
    if (tmpnam_s(temporaryName, sizeof(temporaryName)) != 0) {
        failDecode(DecodeErrorCode::IoFailure,
                "Could not create a temporary substrate path");
    }
    mPath = temporaryName;
    std::ofstream create(mPath, std::ios::binary | std::ios::trunc);
    if (!create) {
        failDecode(DecodeErrorCode::IoFailure,
                "Could not create a temporary substrate file");
    }
#else
    std::filesystem::path parent = std::filesystem::path(adjacentPath).parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    std::string pattern = (parent / ".cellularity-load-XXXXXX").string();
    std::vector<char> writablePattern(pattern.begin(), pattern.end());
    writablePattern.push_back('\0');
    const int descriptor = mkstemp(writablePattern.data());
    if (descriptor < 0) {
        failDecode(DecodeErrorCode::IoFailure,
                "Could not create a temporary substrate file");
    }
    mPath = writablePattern.data();
    if (close(descriptor) != 0) {
        std::remove(mPath.c_str());
        mPath.clear();
        failDecode(DecodeErrorCode::IoFailure,
                "Could not close a temporary substrate file");
    }
#endif
}

TemporarySnapshotFile::~TemporarySnapshotFile() {
    if (!mPath.empty()) {
        std::remove(mPath.c_str());
    }
}

TemporarySnapshotFile::TemporarySnapshotFile(TemporarySnapshotFile&& other) noexcept
        : mPath(std::move(other.mPath)) {
    other.mPath.clear();
}

const std::string& TemporarySnapshotFile::path() const noexcept {
    return mPath;
}

SnapshotInputFile openSnapshotFile(const std::string& path) {
    if (path.empty()) {
        failDecode(DecodeErrorCode::InvalidValue,
                "Substrate input path is empty");
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        failDecode(DecodeErrorCode::IoFailure,
                "Could not open substrate file: " + path);
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 0) {
        failDecode(DecodeErrorCode::IoFailure,
                "Could not determine substrate file size: " + path);
    }
    const std::uint64_t sizeBytes = static_cast<std::uint64_t>(fileSize);
    cellularity::memory::validateSubstrateFileSize(
            sizeBytes, cellularity::memory::currentRuntimeLimits());
    input.seekg(0, std::ios::beg);
    if (!input) {
        failDecode(DecodeErrorCode::IoFailure,
                "Could not seek to the beginning of substrate file: " + path);
    }
    return {std::move(input), sizeBytes};
}

std::vector<std::uint8_t> readSnapshotFilePrefix(
        const std::string& path,
        std::size_t maximumBytes) {
    SnapshotInputFile source = openSnapshotFile(path);
    const std::size_t prefixSize = static_cast<std::size_t>(
            std::min<std::uint64_t>(source.sizeBytes, maximumBytes));
    std::vector<std::uint8_t> bytes;
    try {
        bytes.resize(prefixSize);
    } catch (const std::bad_alloc&) {
        failDecode(DecodeErrorCode::OutOfMemory,
                "Could not allocate substrate inspection buffer");
    }
    if (!bytes.empty()) {
        source.stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        if (!source.stream) {
            failDecode(DecodeErrorCode::IoFailure,
                    "Could not read substrate inspection prefix");
        }
    }
    return bytes;
}

} // namespace cellularity::snapshot_codec

std::vector<std::uint8_t> inflateGzipBytes(
        const std::vector<std::uint8_t>& bytes,
        std::uint64_t maximumOutputBytes) {
    using cellularity::snapshot_codec::failDecode;
    const cellularity::memory::RuntimeLimits runtimeLimits =
            cellularity::memory::currentRuntimeLimits();
    cellularity::memory::validateSubstrateFileSize(bytes.size(), runtimeLimits);
    if (maximumOutputBytes == 0
            || maximumOutputBytes > runtimeLimits.operationalSubstrateFileBytes) {
        failDecode(DecodeErrorCode::InvalidLength,
                "Invalid gzip output safety limit");
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(
            reinterpret_cast<const Bytef*>(bytes.data()));
    stream.avail_in = static_cast<uInt>(bytes.size());
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        failDecode(DecodeErrorCode::InvalidCompression,
                "Could not initialize gzip inflater for legacy substrate");
    }

    struct InflateEndGuard {
        z_stream* stream;

        ~InflateEndGuard() {
            inflateEnd(stream);
        }
    } guard{&stream};

    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, 65536> buffer{};
    while (true) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        const int status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            failDecode(DecodeErrorCode::InvalidCompression,
                    "Legacy substrate gzip body is invalid or truncated");
        }
        const std::size_t produced = buffer.size() - stream.avail_out;
        if (output.size() > maximumOutputBytes
                || produced > maximumOutputBytes - output.size()) {
            failDecode(DecodeErrorCode::LimitExceeded,
                    "Inflated legacy substrate exceeds the output safety limit");
        }
        try {
            output.insert(output.end(), buffer.begin(),
                    buffer.begin() + static_cast<std::ptrdiff_t>(produced));
        } catch (const std::bad_alloc&) {
            failDecode(DecodeErrorCode::OutOfMemory,
                    "Could not allocate memory for inflated legacy substrate data");
        } catch (const std::length_error&) {
            failDecode(DecodeErrorCode::LimitExceeded,
                    "Inflated legacy substrate exceeds the container size limit");
        }
        if (status == Z_STREAM_END) {
            break;
        }
        if (produced == 0 && stream.avail_in == 0) {
            failDecode(DecodeErrorCode::InvalidCompression,
                    "Legacy substrate gzip body is truncated");
        }
    }
    if (stream.avail_in != 0) {
        failDecode(DecodeErrorCode::TrailingData,
                "Unexpected trailing bytes after legacy substrate gzip body");
    }
    return output;
}

std::uint64_t inflateGzipStream(
        std::istream& input,
        std::ostream& output,
        std::uint64_t maximumOutputBytes) {
    using cellularity::snapshot_codec::failDecode;
    const cellularity::memory::RuntimeLimits runtimeLimits =
            cellularity::memory::currentRuntimeLimits();
    if (maximumOutputBytes == 0
            || maximumOutputBytes > runtimeLimits.operationalSubstrateFileBytes) {
        failDecode(DecodeErrorCode::InvalidLength,
                "Invalid gzip output safety limit");
    }

    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        failDecode(DecodeErrorCode::InvalidCompression,
                "Could not initialize gzip inflater for legacy substrate");
    }

    struct InflateEndGuard {
        z_stream* stream;

        ~InflateEndGuard() {
            inflateEnd(stream);
        }
    } guard{&stream};

    std::array<std::uint8_t, 64 * 1024> inputBuffer{};
    std::array<std::uint8_t, 64 * 1024> outputBuffer{};
    std::uint64_t totalOutputBytes = 0;
    bool inputEnded = false;

    while (true) {
        if (stream.avail_in == 0 && !inputEnded) {
            input.read(reinterpret_cast<char*>(inputBuffer.data()),
                    static_cast<std::streamsize>(inputBuffer.size()));
            const std::streamsize readCount = input.gcount();
            if (input.bad()) {
                failDecode(DecodeErrorCode::IoFailure,
                        "Could not read legacy substrate gzip body");
            }
            if (readCount == 0) {
                inputEnded = true;
            } else {
                stream.next_in = reinterpret_cast<Bytef*>(inputBuffer.data());
                stream.avail_in = static_cast<uInt>(readCount);
            }
        }

        stream.next_out = reinterpret_cast<Bytef*>(outputBuffer.data());
        stream.avail_out = static_cast<uInt>(outputBuffer.size());
        const int status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            failDecode(DecodeErrorCode::InvalidCompression,
                    "Legacy substrate gzip body is invalid or truncated");
        }

        const std::size_t produced = outputBuffer.size() - stream.avail_out;
        if (produced > maximumOutputBytes - totalOutputBytes) {
            failDecode(DecodeErrorCode::LimitExceeded,
                    "Inflated legacy substrate exceeds the output safety limit");
        }
        if (produced != 0) {
            output.write(reinterpret_cast<const char*>(outputBuffer.data()),
                    static_cast<std::streamsize>(produced));
            if (!output) {
                failDecode(DecodeErrorCode::IoFailure,
                        "Could not write inflated legacy substrate data");
            }
            totalOutputBytes += produced;
        }

        if (status == Z_STREAM_END) {
            if (stream.avail_in != 0 || input.peek() != std::char_traits<char>::eof()) {
                failDecode(DecodeErrorCode::TrailingData,
                        "Unexpected trailing bytes after legacy substrate gzip body");
            }
            break;
        }
        if (inputEnded && stream.avail_in == 0 && produced == 0) {
            failDecode(DecodeErrorCode::InvalidCompression,
                    "Legacy substrate gzip body is truncated");
        }
    }
    output.flush();
    if (!output) {
        failDecode(DecodeErrorCode::IoFailure,
                "Could not flush inflated legacy substrate data");
    }
    return totalOutputBytes;
}
