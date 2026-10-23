#pragma once

#include <cstddef>
#include <cstdint>

namespace cellularity::render::jni::vbd1 {

inline constexpr std::uint32_t kMagic = UINT32_C(0x31444256);
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::uint16_t kHeaderSize = 32;
inline constexpr std::size_t kMagicOffset = 0;
inline constexpr std::size_t kVersionOffset = 4;
inline constexpr std::size_t kHeaderSizeOffset = 6;
inline constexpr std::size_t kRevisionOffset = 8;
inline constexpr std::size_t kTotalBytesOffset = 16;
inline constexpr std::size_t kBytesWrittenOffset = 24;
inline constexpr std::size_t kReservedOffset = 28;
inline constexpr std::size_t kPayloadOffset = 32;

} // namespace cellularity::render::jni::vbd1
