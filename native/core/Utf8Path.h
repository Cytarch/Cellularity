#pragma once

// Paths cross the native boundary as UTF-8 on every platform: JNI hands them over
// that way, and the Windows host produces them that way. POSIX file APIs take the
// bytes as they are, but the narrow Windows CRT and Win32 "A" functions read them in
// the process code page, so a profile with non-ASCII characters (for example a
// Cyrillic or Polish user name) would fail to open. Every native file open goes
// through these helpers so the UTF-8 meaning is kept.

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace cellularity::paths {

[[nodiscard]] inline std::filesystem::path fromUtf8(const std::string& path) {
    return std::filesystem::u8path(path);
}

[[nodiscard]] inline std::string toUtf8(const std::filesystem::path& path) {
    return path.u8string();
}

// fopen for a UTF-8 path. Returns nullptr on failure, like std::fopen.
[[nodiscard]] inline std::FILE* openFile(const std::string& path, const char* mode) {
#if defined(_WIN32)
    std::wstring wideMode;
    for (const char* character = mode; *character != '\0'; ++character) {
        wideMode.push_back(static_cast<wchar_t>(*character));
    }
    try {
        return ::_wfopen(fromUtf8(path).c_str(), wideMode.c_str());
    } catch (...) {
        return nullptr;
    }
#else
    return std::fopen(path.c_str(), mode);
#endif
}

// Removes a file if it exists; failures are ignored, like std::remove's result
// was by every caller.
inline void removeFile(const std::string& path) noexcept {
    try {
        std::error_code ignored;
        std::filesystem::remove(fromUtf8(path), ignored);
    } catch (...) {
    }
}

} // namespace cellularity::paths
