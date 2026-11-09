#pragma once

// Where native diagnostic logging goes on each platform.
//
// The Vulkan session logs validation-layer messages. On Android that is
// __android_log_print; on Windows there is no equivalent console, so messages
// go to the debugger through OutputDebugStringA and to stderr, which a host tool
// or test can capture.

#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace cellularity::platform {

enum class LogSeverity {
    Warning = 0,
    Error = 1,
};

inline void logMessage(const char* tag, LogSeverity severity,
        const char* message) noexcept {
    if (tag == nullptr || message == nullptr) {
        return;
    }
#if defined(__ANDROID__)
    __android_log_print(severity == LogSeverity::Error ? ANDROID_LOG_ERROR
                                                       : ANDROID_LOG_WARN,
            tag, "%s", message);
#elif defined(_WIN32)
    // OutputDebugStringA does not accept a tag, so compose the line once.
    char line[1024] = {};
    std::snprintf(line, sizeof(line), "%s: %s\n", tag, message);
    ::OutputDebugStringA(line);
    std::fputs(line, stderr);
#else
    std::fprintf(stderr, "%s: %s\n", tag, message);
#endif
}

} // namespace cellularity::platform
