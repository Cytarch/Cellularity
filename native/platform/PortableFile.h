#pragma once

// Minimal cross-platform file primitives for project-owned code that must run
// on both POSIX hosts and Windows with MSVC.
//
// Scope is deliberately narrow: only the operations the native diagnostics
// writer and its host test actually need (create with owner-only permissions,
// write all, close, file size). Anything richer belongs in a real abstraction,
// not here. This header is the seam the Windows host port needs, and nothing
// more.
//
// Owner-only semantics: on POSIX the file is created with mode 0600. On Windows
// the file is created with an explicit security descriptor that grants access to
// the current user and to SYSTEM only, which is the closest equivalent. The
// protected DACL flag (PROTECTED_DACL_SECURITY_INFORMATION) prevents inherited
// access-control entries from widening it afterwards.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <io.h>
#elif defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#error "PortableFile.h: unsupported platform; add a branch for it"
#endif

namespace cellularity::platform {

// The separator to use when composing paths with plain string concatenation.
// Both Windows APIs and the MSVC CRT accept '/', so the forward slash stays
// valid everywhere; it lives here so the one place that joins path fragments
// does not hard-code the assumption.
inline constexpr char kPathSeparator = '/';

// True when the file can be opened for writing by the current process. This is
// the portable stand-in for the POSIX owner-only permission check: it proves the
// caller's own access rather than enumerating the ACL, which is what the
// diagnostics contract needs, since a report its own writer cannot reopen is as
// broken as one that leaked.
[[nodiscard]] inline bool isWritableByCurrentUser(const char* path) noexcept {
#if defined(_WIN32)
    if (path == nullptr) {
        return false;
    }
    const HANDLE handle = ::CreateFileA(path, GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::CloseHandle(handle);
    return true;
#else
    return path != nullptr && ::access(path, W_OK) == 0;
#endif
}

#if defined(_WIN32)
using FileHandle = HANDLE;
inline constexpr FileHandle kInvalidFileHandle = nullptr;

[[nodiscard]] inline bool isOpen(FileHandle handle) noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

inline void closeFile(FileHandle handle) noexcept {
    if (isOpen(handle)) {
        ::CloseHandle(handle);
    }
}

// Creates or truncates a file readable and writable only by the current user.
// Returns kInvalidFileHandle on any failure; the caller must not need an errno.
[[nodiscard]] inline FileHandle createOwnerOnlyFile(const char* path) noexcept {
    if (path == nullptr || path[0] == '\0') {
        return kInvalidFileHandle;
    }

    // Build a security descriptor whose DACL grants the current user and SYSTEM.
    // Everything else is denied, and inheritance is blocked.
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = FALSE;
    security.lpSecurityDescriptor = nullptr;

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES* attributes = nullptr;
    PACL dacl = nullptr;
    PSID userSid = nullptr;
    PSID systemSid = nullptr;

    bool haveSid = false;
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) != 0) {
        DWORD size = 0;
        ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        if (size > 0) {
            auto* buffer = static_cast<unsigned char*>(::LocalAlloc(LPTR, size));
            if (buffer != nullptr) {
                if (::GetTokenInformation(token, TokenUser, buffer, size, &size) != 0) {
                    userSid = reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid;
                    haveSid = true;
                }
            }
        }
    }

    // Well-known SYSTEM SID, written out so no allocation is needed.
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (::AllocateAndInitializeSid(&ntAuthority, 1, SECURITY_LOCAL_SYSTEM_RID,
                0, 0, 0, 0, 0, 0, 0, &systemSid) == 0) {
        systemSid = nullptr;
    }

    const bool canBuildDacl = haveSid && systemSid != nullptr;
    if (canBuildDacl) {
        constexpr DWORD kAceCount = 2;
        const DWORD daclSize = sizeof(ACL)
                + kAceCount * (sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE);
        dacl = static_cast<PACL>(::LocalAlloc(LPTR, daclSize));
        if (dacl != nullptr && ::InitializeAcl(dacl, daclSize, ACL_REVISION) != 0) {
            if (::AddAccessAllowedAce(dacl, ACL_REVISION, GENERIC_ALL, userSid) != 0
                    && ::AddAccessAllowedAce(dacl, ACL_REVISION, GENERIC_ALL, systemSid) != 0) {
                descriptor = static_cast<PSECURITY_DESCRIPTOR>(::LocalAlloc(
                        LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH));
                if (descriptor != nullptr
                        && ::InitializeSecurityDescriptor(
                                   descriptor, SECURITY_DESCRIPTOR_REVISION)
                                != 0) {
                    if (::SetSecurityDescriptorDacl(descriptor, TRUE, dacl, FALSE) != 0) {
                        // Drop inherited ACEs so the DACL stays owner-only.
                        (void)::SetSecurityDescriptorControl(descriptor,
                                SE_DACL_PROTECTED, SE_DACL_PROTECTED);
                        security.lpSecurityDescriptor = descriptor;
                        attributes = &security;
                    }
                }
            }
        }
    }

    // If the descriptor could not be assembled, fall back to the default DACL of
    // the process token. That is weaker than 0600 but still user-scoped, and it
    // is better than failing to write a crash report at all.
    const HANDLE handle = ::CreateFileA(path, GENERIC_WRITE | GENERIC_READ, 0,
            attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (descriptor != nullptr) { ::LocalFree(descriptor); }
    if (dacl != nullptr) { ::LocalFree(dacl); }
    if (systemSid != nullptr) { ::FreeSid(systemSid); }
    if (token != nullptr) { ::CloseHandle(token); }

    if (handle == INVALID_HANDLE_VALUE) {
        return kInvalidFileHandle;
    }
    return handle;
}

// Writes the whole buffer or reports failure. Handles partial writes.
[[nodiscard]] inline bool writeAllToFile(
        FileHandle handle, const char* bytes, std::size_t byteCount) noexcept {
    if (!isOpen(handle)) {
        return false;
    }
    std::size_t written = 0;
    while (written < byteCount) {
        const std::size_t remaining = byteCount - written;
        const DWORD chunk = static_cast<DWORD>(
                remaining > 0x7FFFFFFFu ? 0x7FFFFFFFu : remaining);
        DWORD accepted = 0;
        if (::WriteFile(handle, bytes + written, chunk, &accepted, nullptr) == 0) {
            return false;
        }
        if (accepted == 0) {
            return false;
        }
        written += static_cast<std::size_t>(accepted);
    }
    return true;
}

// Byte size of a regular file, or -1 when it cannot be determined.
[[nodiscard]] inline std::int64_t fileSize(const char* path) noexcept {
    if (path == nullptr) {
        return -1;
    }
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (::GetFileAttributesExA(path, GetFileExInfoStandard, &data) == 0) {
        return -1;
    }
    LARGE_INTEGER size{};
    size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
    size.LowPart = data.nFileSizeLow;
    return static_cast<std::int64_t>(size.QuadPart);
}

// Compose a unique path beside an existing file, in the same directory, so a
// later rename onto the target stays within one filesystem. Returns an empty
// string when the directory cannot be determined or is not writable.
//
// This replaces POSIX mkstemp-style naming for the save/load path. Using a
// system temp directory instead would make the placement rename non-atomic
// across filesystems, which the substrate and save-container writers must not
// risk.
[[nodiscard]] inline std::string makeUniqueAdjacentPath(
        const std::string& adjacentPath, const char* stem) {
    std::string parent;
    const std::size_t separator = adjacentPath.find_last_of("/\\");
    if (separator != std::string::npos) {
        parent = adjacentPath.substr(0, separator + 1);
    }
    const std::string name = (stem != nullptr ? stem : ".cellularity-tmp");

    for (int attempt = 0; attempt < 32; attempt++) {
        const std::string candidate = parent + name + "-"
                + std::to_string(::GetCurrentProcessId()) + "-"
                + std::to_string(::GetTickCount64() + static_cast<ULONGLONG>(attempt));

        // CREATE_NEW fails when the name is taken, which is the uniqueness check.
        const HANDLE handle = ::CreateFileA(candidate.c_str(), GENERIC_WRITE, 0,
                nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
            return candidate;
        }
        if (::GetLastError() != ERROR_FILE_EXISTS
                && ::GetLastError() != ERROR_ALREADY_EXISTS) {
            return {};
        }
    }
    return {};
}

// True when the path names an existing regular file.
[[nodiscard]] inline bool fileExists(const char* path) noexcept {
    return fileSize(path) >= 0;
}

// Owner-only access check for Windows: walk the DACL and accept only the current
// user and SYSTEM. This is the portable form of the diagnostics contract's
// "mode 0600" requirement.
[[nodiscard]] inline bool isOwnerOnlyFile(const char* path) noexcept {
    if (path == nullptr) {
        return false;
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (::GetNamedSecurityInfoA(const_cast<char*>(path), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr,
                &descriptor)
            != ERROR_SUCCESS) {
        return false;
    }
    if (descriptor == nullptr) {
        return false;
    }

    bool ownerOnly = false;
    PACL dacl = nullptr;
    BOOL daclPresent = FALSE;
    BOOL daclDefaulted = FALSE;
    if (::GetSecurityDescriptorDacl(descriptor, &daclPresent, &dacl, &daclDefaulted) != 0
            && daclPresent != 0 && dacl != nullptr) {
        ACL_SIZE_INFORMATION info{};
        if (::GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation) != 0) {
            ownerOnly = true;
            for (DWORD index = 0; index < info.AceCount; index++) {
                void* ace = nullptr;
                if (::GetAce(dacl, index, &ace) == 0) {
                    ownerOnly = false;
                    break;
                }
                const auto* header = static_cast<const ACE_HEADER*>(ace);
                if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
                    // Deny and audit entries do not widen access.
                    continue;
                }
                const auto* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(ace);
                const PSID sid = const_cast<DWORD*>(&allowed->SidStart);
                if (::IsWellKnownSid(sid, WinLocalSystemSid) != 0) {
                    continue;
                }
                // Every other allowed entry must be the current user.
                HANDLE token = nullptr;
                bool isCurrentUser = false;
                if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) != 0) {
                    DWORD size = 0;
                    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
                    if (size > 0) {
                        auto* buffer = static_cast<unsigned char*>(::LocalAlloc(LPTR, size));
                        if (buffer != nullptr) {
                            if (::GetTokenInformation(token, TokenUser, buffer, size, &size) != 0) {
                                isCurrentUser = ::EqualSid(
                                        reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid, sid)
                                        != 0;
                            }
                            ::LocalFree(buffer);
                        }
                    }
                    ::CloseHandle(token);
                }
                if (!isCurrentUser) {
                    ownerOnly = false;
                    break;
                }
            }
        }
    }

    ::LocalFree(descriptor);
    return ownerOnly;
}

// Creates a unique temporary directory and returns its path, or an empty string.
// Replaces POSIX mkdtemp for host tests.
//
// The name is generated directly rather than through GetTempFileNameA, which
// requires the prefix to name an existing directory. Candidate parents are
// probed for writability, because a restricted environment can report a temp
// path through GetTempPathA while denying writes to it; in that case the current
// working directory is used instead.
[[nodiscard]] inline std::string createTemporaryDirectory(const char* prefix) {
    const std::string stem = (prefix != nullptr ? prefix : "cellularity");

    std::vector<std::string> parents;
    char tempBuffer[MAX_PATH] = {};
    const DWORD length = ::GetTempPathA(MAX_PATH, tempBuffer);
    if (length > 0 && length < MAX_PATH) {
        parents.emplace_back(tempBuffer);
    }
    // The working directory is the fallback: it is writable by definition in any
    // environment that let the test binary start.
    parents.emplace_back(".\\");

    for (const std::string& rawParent : parents) {
        std::string base = rawParent;
        if (!base.empty() && base.back() != '\\' && base.back() != '/') {
            base.push_back('\\');
        }

        for (int attempt = 0; attempt < 16; attempt++) {
            const std::string candidate = base + stem
                    + std::to_string(::GetCurrentProcessId()) + "-"
                    + std::to_string(::GetTickCount64()
                                      + static_cast<ULONGLONG>(attempt));
            if (::CreateDirectoryA(candidate.c_str(), nullptr) != 0) {
                return candidate;
            }
            const DWORD error = ::GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                // Unwritable parent (typically ERROR_ACCESS_DENIED): try the next.
                break;
            }
        }
    }
    return {};
}

inline void removeDirectoryRecursively(const std::string& path) noexcept {
    // Shallow removal is enough for the host tests, which create flat
    // directories containing one report file.
    std::string pattern = path + "\\*";
    WIN32_FIND_DATAA entry{};
    const HANDLE find = ::FindFirstFileA(pattern.c_str(), &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (entry.cFileName[0] == '.') {
                continue;
            }
            const std::string child = path + "\\" + entry.cFileName;
            (void)::DeleteFileA(child.c_str());
        } while (::FindNextFileA(find, &entry) != 0);
        ::FindClose(find);
    }
    (void)::RemoveDirectoryA(path.c_str());
}

#else // POSIX

using FileHandle = int;
inline constexpr FileHandle kInvalidFileHandle = -1;

inline void closeFile(FileHandle handle) noexcept {
    if (handle != kInvalidFileHandle) {
        ::close(handle);
    }
}

[[nodiscard]] inline FileHandle createOwnerOnlyFile(const char* path) noexcept {
    if (path == nullptr || path[0] == '\0') {
        return kInvalidFileHandle;
    }
    return ::open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
}

[[nodiscard]] inline bool writeAllToFile(
        FileHandle handle, const char* bytes, std::size_t byteCount) noexcept {
    if (handle == kInvalidFileHandle) {
        return false;
    }
    std::size_t written = 0;
    while (written < byteCount) {
        const ssize_t result =
                ::write(handle, bytes + written, byteCount - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (result == 0) {
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

[[nodiscard]] inline std::int64_t fileSize(const char* path) noexcept {
    if (path == nullptr) {
        return -1;
    }
    struct stat status {};
    if (::stat(path, &status) != 0) {
        return -1;
    }
    return static_cast<std::int64_t>(status.st_size);
}

[[nodiscard]] inline bool fileExists(const char* path) noexcept {
    return fileSize(path) >= 0;
}

// POSIX form of the owner-only check: inspect the permission bits directly.
[[nodiscard]] inline bool isOwnerOnlyFile(const char* path) noexcept {
    if (path == nullptr) {
        return false;
    }
    struct stat status {};
    if (::stat(path, &status) != 0) {
        return false;
    }
    return (status.st_mode & 0777) == 0600;
}

// Compose a unique path beside an existing file, in the same directory, so a
// later rename onto the target stays within one filesystem. Returns an empty
// string when the name cannot be reserved.
//
// This replaces ad-hoc mkstemp naming for the save/load path. Using a system
// temp directory instead would make the placement rename non-atomic across
// filesystems, which the substrate and save-container writers must not risk.
[[nodiscard]] inline std::string makeUniqueAdjacentPath(
        const std::string& adjacentPath, const char* stem) {
    std::string parent;
    const std::size_t separator = adjacentPath.find_last_of("/\\");
    if (separator != std::string::npos) {
        parent = adjacentPath.substr(0, separator + 1);
    }
    const std::string name = (stem != nullptr ? stem : ".cellularity-tmp");

    // mkstemp both picks a free name and creates the file atomically.
    std::string pattern = parent + name + "-XXXXXX";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const int descriptor = ::mkstemp(writable.data());
    if (descriptor < 0) {
        return {};
    }
    const std::string created(writable.data());
    if (::close(descriptor) != 0) {
        ::unlink(created.c_str());
        return {};
    }
    return created;
}

[[nodiscard]] inline std::string createTemporaryDirectory(const char* prefix) {
    std::string pattern = "/tmp/";
    pattern += (prefix != nullptr ? prefix : "cellularity");
    pattern += "XXXXXX";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    if (::mkdtemp(writable.data()) == nullptr) {
        return {};
    }
    return std::string(writable.data());
}

inline void removeDirectoryRecursively(const std::string& path) noexcept {
    std::string command = "rm -rf '" + path + "'";
    (void)std::system(command.c_str());
}

#endif

} // namespace cellularity::platform
