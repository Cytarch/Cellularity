#pragma once

#include <cstddef>
#include <cstdio>

#if defined(__ANDROID__) || defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace cellularity::platform {

// Linux and Android expose at most 15 visible bytes for a thread name. Keep
// production names within that common limit so diagnostics never see a
// silently truncated identifier.
template <std::size_t Size>
void setCurrentThreadName(const char (&name)[Size]) noexcept {
    static_assert(Size > 1, "Thread name must not be empty");
    static_assert(Size <= 16, "Thread name must be at most 15 bytes");
#if defined(__ANDROID__) || defined(__linux__)
    (void)pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    (void)pthread_setname_np(name);
#else
    (void)name;
#endif
}

// Linux and Android traces are much easier to audit when every simulation
// worker has a stable role and index. This helper deliberately formats into a
// fixed stack buffer: naming a worker must not allocate or leak vendor thread
// pool details into application code.
inline void setCurrentThreadNameWithIndex(const char* prefix,
        std::size_t index) noexcept {
    char name[16] = {};
    const int written = std::snprintf(name, sizeof(name), "%s%zu", prefix, index);
    if (written <= 0 || written >= static_cast<int>(sizeof(name))) {
        return;
    }
#if defined(__ANDROID__) || defined(__linux__)
    (void)pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    (void)pthread_setname_np(name);
#else
    (void)name;
#endif
}

} // namespace cellularity::platform
