#include "io/Sha256.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cellularity::io {
namespace {

std::uint32_t rotateRight(std::uint32_t value, std::uint32_t count) noexcept {
    return (value >> count) | (value << (32U - count));
}

} // namespace

Sha256::Sha256()
        : mState{
                  0x6a09e667U,
                  0xbb67ae85U,
                  0x3c6ef372U,
                  0xa54ff53aU,
                  0x510e527fU,
                  0x9b05688cU,
                  0x1f83d9abU,
                  0x5be0cd19U
          } {
}

void Sha256::update(const std::uint8_t* data, std::size_t size) {
    if (mFinished) {
        throw std::logic_error("SHA-256 digest was already finalized");
    }
    if (size != 0 && data == nullptr) {
        throw std::invalid_argument("SHA-256 input pointer is null");
    }
    if (size > std::numeric_limits<std::uint64_t>::max() - mTotalBytes) {
        throw std::length_error("SHA-256 input length overflow");
    }
    mTotalBytes += size;
    while (size > 0) {
        const std::size_t copied = std::min(size, mBlock.size() - mBlockSize);
        std::memcpy(mBlock.data() + mBlockSize, data, copied);
        mBlockSize += copied;
        data += copied;
        size -= copied;
        if (mBlockSize == mBlock.size()) {
            transform(mBlock.data());
            mBlockSize = 0;
        }
    }
}

std::string Sha256::finishHex() {
    if (mFinished) {
        return mResult;
    }
    if (mTotalBytes > std::numeric_limits<std::uint64_t>::max() / 8U) {
        throw std::length_error("SHA-256 bit length overflow");
    }
    const std::uint64_t bitLength = mTotalBytes * 8U;
    mBlock[mBlockSize++] = 0x80;
    if (mBlockSize > 56) {
        std::fill(
                mBlock.begin() + static_cast<std::ptrdiff_t>(mBlockSize),
                mBlock.end(),
                0);
        transform(mBlock.data());
        mBlockSize = 0;
    }
    std::fill(
            mBlock.begin() + static_cast<std::ptrdiff_t>(mBlockSize),
            mBlock.begin() + 56,
            0);
    for (int shift = 56; shift >= 0; shift -= 8) {
        mBlock[56 + static_cast<std::size_t>((56 - shift) / 8)] =
                static_cast<std::uint8_t>(bitLength >> shift);
    }
    transform(mBlock.data());

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::uint32_t value : mState) {
        result << std::setw(8) << value;
    }
    mResult = result.str();
    mFinished = true;
    return mResult;
}

void Sha256::transform(const std::uint8_t* block) {
    static constexpr std::array<std::uint32_t, 64> constants{
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
            0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
            0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
            0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
            0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
            0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
            0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
            0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
            0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
            0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = static_cast<std::uint32_t>(block[index * 4]) << 24U
                | static_cast<std::uint32_t>(block[index * 4 + 1]) << 16U
                | static_cast<std::uint32_t>(block[index * 4 + 2]) << 8U
                | static_cast<std::uint32_t>(block[index * 4 + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const std::uint32_t first = rotateRight(words[index - 15], 7)
                ^ rotateRight(words[index - 15], 18)
                ^ (words[index - 15] >> 3U);
        const std::uint32_t second = rotateRight(words[index - 2], 17)
                ^ rotateRight(words[index - 2], 19)
                ^ (words[index - 2] >> 10U);
        words[index] = words[index - 16] + first + words[index - 7] + second;
    }

    std::uint32_t a = mState[0];
    std::uint32_t b = mState[1];
    std::uint32_t c = mState[2];
    std::uint32_t d = mState[3];
    std::uint32_t e = mState[4];
    std::uint32_t f = mState[5];
    std::uint32_t g = mState[6];
    std::uint32_t h = mState[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const std::uint32_t sum1 = rotateRight(e, 6)
                ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t first = h + sum1 + choice
                + constants[index] + words[index];
        const std::uint32_t sum0 = rotateRight(a, 2)
                ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    mState[0] += a;
    mState[1] += b;
    mState[2] += c;
    mState[3] += d;
    mState[4] += e;
    mState[5] += f;
    mState[6] += g;
    mState[7] += h;
}

} // namespace cellularity::io
