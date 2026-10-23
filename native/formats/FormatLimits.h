#pragma once

#include <cstddef>
#include <cstdint>

namespace cellularity::format_limits {

inline constexpr std::uint64_t kMaximumSaveFileBytes =
        2ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t k32BitOperationalSubstrateFileBytes =
        512ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kMaximumSubstrateInspectionPrefixBytes =
        2ULL * 1024ULL * 1024ULL;
inline constexpr int kMaximumSerializedCollectionEntries = 4096;

} // namespace cellularity::format_limits
