#pragma once

// Creation of the temporary file a snapshot is written through.
//
// The temporary has to sit beside its target, in the same directory, so that
// placing it is a rename within one filesystem and therefore atomic. A system
// temporary directory would break that, and on Windows it would also land the file
// outside the directory whose permissions the caller expects.
//
// The file is created exclusively (O_CREAT | O_EXCL semantics) with an
// unpredictable name, so a concurrent process, a stale leftover, or a planted link
// can never be reused or followed. Only C runtime calls are used, which keeps the
// format layer free of application, JNI, and platform-API dependencies.

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <cerrno>
#include <cstdio>
#include <vector>

#include "core/Utf8Path.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace cellularity::snapshot_codec {

// Creates an empty, exclusively owned file beside adjacentPath and returns its
// path. Returns an empty string when no file could be created, so a caller can
// report a placement failure rather than writing to a surprising path.
[[nodiscard]] inline std::string createExclusiveAdjacentFile(
        const std::string& adjacentPath, const char* stem) {
    std::filesystem::path parent = cellularity::paths::fromUtf8(adjacentPath).parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    const std::string name =
            (stem != nullptr && stem[0] != '\0') ? std::string(stem)
                                                 : std::string(".cellularity-tmp-");
#if defined(_WIN32)
    static constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device entropy;
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::string suffix(16, '0');
        for (char& character : suffix) {
            character = kAlphabet[entropy() % (sizeof(kAlphabet) - 1)];
        }
        const std::filesystem::path candidate =
                parent / cellularity::paths::fromUtf8(name + suffix);
        const int descriptor = _wopen(candidate.c_str(),
                _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY | _O_NOINHERIT,
                _S_IREAD | _S_IWRITE);
        if (descriptor >= 0) {
            _close(descriptor);
            return cellularity::paths::toUtf8(candidate);
        }
        if (errno != EEXIST) {
            return {};
        }
    }
    return {};
#else
    const std::string pattern = cellularity::paths::toUtf8(
            parent / cellularity::paths::fromUtf8(name + "XXXXXX"));
    std::vector<char> writablePattern(pattern.begin(), pattern.end());
    writablePattern.push_back('\0');
    const int descriptor = mkstemp(writablePattern.data());
    if (descriptor < 0) {
        return {};
    }
    std::string result(writablePattern.data());
    if (close(descriptor) != 0) {
        cellularity::paths::removeFile(result);
        return {};
    }
    return result;
#endif
}

} // namespace cellularity::snapshot_codec
