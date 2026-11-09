#pragma once

// Naming of the temporary file a snapshot is written through.
//
// The temporary has to sit beside its target, in the same directory, so that
// placing it is a rename within one filesystem and therefore atomic. A system
// temporary directory would break that, and on Windows it would also land the file
// outside the directory whose permissions the caller expects.
//
// This lives in the format layer rather than in the platform layer on purpose: the
// format layer must not depend on application, render, JNI or platform code, and
// nothing here needs a platform API. Uniqueness is established by asking the
// filesystem whether a candidate name is free, which is the same guarantee the
// earlier mkstemp-style implementation provided in practice.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cellularity::snapshot_codec {

// Composes a unique path beside an existing file. Returns an empty string when the
// parent directory cannot be determined or when no free name could be found, so a
// caller can report a placement failure rather than writing to a surprising path.
[[nodiscard]] inline std::string makeUniqueAdjacentPath(
        const std::string& adjacentPath, const char* stem) {
    const std::filesystem::path target(adjacentPath);
    const std::filesystem::path parent = target.parent_path();
    const std::string name =
            (stem != nullptr && stem[0] != '\0') ? std::string(stem)
                                                 : std::string(".cellularity-tmp");

    // A per-process counter separates concurrent placements inside one process and
    // costs no platform call, which keeps this usable from the format layer on
    // every platform. The counter plus the existence check is what the earlier
    // mkstemp-style naming provided in practice.
    static std::atomic<std::uint64_t> sequence{0};
    const std::uint64_t slot =
            sequence.fetch_add(1, std::memory_order_relaxed);
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::filesystem::path candidate(parent);
        candidate /= name + "-" + std::to_string(slot) + "-"
                + std::to_string(attempt);
        std::error_code status;
        const bool taken = std::filesystem::exists(candidate, status);
        if (status) {
            // The directory could not be inspected; report failure rather than
            // returning a name that may already be in use.
            return {};
        }
        if (!taken) {
            return candidate.string();
        }
    }
    return {};
}

} // namespace cellularity::snapshot_codec
