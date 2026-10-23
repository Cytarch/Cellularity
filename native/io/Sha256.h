#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cellularity::io {

/** Small dependency-free streaming SHA-256 used by persistent-state proofs. */
class Sha256 final {
public:
    Sha256();

    void update(const std::uint8_t* data, std::size_t size);
    [[nodiscard]] std::string finishHex();

private:
    void transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> mState;
    std::array<std::uint8_t, 64> mBlock{};
    std::size_t mBlockSize = 0;
    std::uint64_t mTotalBytes = 0;
    bool mFinished = false;
    std::string mResult;
};

} // namespace cellularity::io
