#include "render/FileRenderAssets.h"

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cellularity::render {
namespace {

// Joins a root and a relative asset path with a single forward slash. Forward
// slashes are accepted by the Windows file APIs as well as by POSIX, so no
// platform branch is needed here.
std::string joinPath(const std::string& root, const std::string& path) {
    if (root.empty()) {
        return path;
    }
    std::string joined = root;
    if (joined.back() != '/' && joined.back() != '\\') {
        joined.push_back('/');
    }
    joined += path;
    return joined;
}

} // namespace

FileRenderAssets::FileRenderAssets(std::vector<std::string> roots)
        : mRoots(std::move(roots)) {
    if (mRoots.empty()) {
        throw std::invalid_argument(
                "FileRenderAssets requires at least one asset root");
    }
}

FileRenderAssets::FileRenderAssets(std::string root)
        : mRoots{std::move(root)} {
}

const std::vector<std::string>& FileRenderAssets::roots() const noexcept {
    return mRoots;
}

std::string FileRenderAssets::resolve(const std::string& path) const {
    for (const std::string& root : mRoots) {
        std::string candidate = joinPath(root, path);
        if (std::FILE* file = std::fopen(candidate.c_str(), "rb")) {
            std::fclose(file);
            return candidate;
        }
    }
    throw std::runtime_error("Render resource not found: " + path);
}

std::string FileRenderAssets::readText(const std::string& path) const {
    const std::vector<unsigned char> bytes = readBytes(path);
    return std::string(bytes.begin(), bytes.end());
}

std::vector<unsigned char> FileRenderAssets::readBytes(
        const std::string& path) const {
    if (path.empty() || path.front() == '/' || path.front() == '\\'
            || path.find("..") != std::string::npos
            || (path.size() > 1 && path[1] == ':')) {
        throw std::invalid_argument("Invalid render resource path: " + path);
    }

    const std::string resolved = resolve(path);

    std::FILE* file = std::fopen(resolved.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("Render resource not found: " + path);
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        throw std::runtime_error("Could not size render resource: " + path);
    }
    const long length = std::ftell(file);
    if (length < 0) {
        std::fclose(file);
        throw std::runtime_error("Could not size render resource: " + path);
    }
    std::rewind(file);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t read = std::fread(
                bytes.data() + offset, 1, bytes.size() - offset, file);
        if (read == 0) {
            std::fclose(file);
            throw std::runtime_error(
                    "Could not read render resource: " + path);
        }
        offset += read;
    }
    std::fclose(file);
    return bytes;
}

} // namespace cellularity::render
