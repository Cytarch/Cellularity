#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

std::vector<std::uint8_t> inflateGzipBytes(
        const std::vector<std::uint8_t>& bytes,
        std::uint64_t maximumOutputBytes);

std::uint64_t inflateGzipStream(
        std::istream& input,
        std::ostream& output,
        std::uint64_t maximumOutputBytes);

namespace cellularity::snapshot_codec {

struct SnapshotInputFile {
    std::ifstream stream;
    std::uint64_t sizeBytes;
};

class TemporarySnapshotFile {
public:
    explicit TemporarySnapshotFile(const std::string& adjacentPath);
    ~TemporarySnapshotFile();

    TemporarySnapshotFile(const TemporarySnapshotFile&) = delete;
    TemporarySnapshotFile& operator=(const TemporarySnapshotFile&) = delete;
    TemporarySnapshotFile(TemporarySnapshotFile&& other) noexcept;
    TemporarySnapshotFile& operator=(TemporarySnapshotFile&& other) = delete;

    [[nodiscard]] const std::string& path() const noexcept;

private:
    std::string mPath;
};

SnapshotInputFile openSnapshotFile(const std::string& path);
std::vector<std::uint8_t> readSnapshotFilePrefix(
        const std::string& path,
        std::size_t maximumBytes);

} // namespace cellularity::snapshot_codec
