#pragma once

#include <string>
#include <vector>

#include "render/NativeRenderAssets.h"

namespace cellularity::render {

/**
 * Render assets read from one or more directories on the local filesystem.
 *
 * Search order is the order the roots were supplied, so a build can point at the
 * generated SPIR-V directory and the source asset directory at once without
 * copying either. NativeRenderAssets already hides the platform: Android serves
 * these bytes from AAssetManager, and this implementation serves them from disk.
 *
 * Roots are resolved in this order per request, and a request that no root
 * satisfies throws, matching AndroidRenderAssets. Callers already translate that
 * exception into the asset-unavailable ABI error.
 */
class FileRenderAssets final : public NativeRenderAssets {
public:
    explicit FileRenderAssets(std::vector<std::string> roots);

    // Single-root convenience. Pass the directory that holds "textures" directly.
    explicit FileRenderAssets(std::string root);

    [[nodiscard]] std::string readText(const std::string& path) const override;
    [[nodiscard]] std::vector<unsigned char> readBytes(
            const std::string& path) const override;

    [[nodiscard]] const std::vector<std::string>& roots() const noexcept;

private:
    [[nodiscard]] std::string resolve(const std::string& path) const;

    std::vector<std::string> mRoots;
};

} // namespace cellularity::render
