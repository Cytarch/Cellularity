/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import java.nio.file.Files
import java.nio.file.Path

/**
 * Where the renderer's shaders and textures are on this machine.
 *
 * The native host resolves these itself when it runs standalone, because it can look
 * next to its own executable. A library cannot: it was loaded from somewhere else, and
 * the build tree and an installed layout put these two directories in unrelated places.
 * So the caller states them, and this finds the build layout by walking up from the
 * working directory, which is what both Gradle and a run from the repository root give us.
 */
object DesktopAssetRoots {

    data class Roots(val shaderRoot: Path, val textureRoot: Path) {
        val isValid: Boolean
            get() = Files.isDirectory(shaderRoot) && Files.isDirectory(textureRoot)
    }

    fun resolve(): Roots? {
        val shaderOverride = System.getenv("CELLULARITY_HOST_SHADER_ROOT")
        val textureOverride = System.getenv("CELLULARITY_HOST_TEXTURE_ROOT")
        if (!shaderOverride.isNullOrBlank() && !textureOverride.isNullOrBlank()) {
            val roots = Roots(Path.of(shaderOverride), Path.of(textureOverride))
            if (roots.isValid) return roots
        }

        var directory: Path? = Path.of("").toAbsolutePath()
        while (directory != null) {
            val shader = directory.resolve(SHADER_RELATIVE)
            val texture = directory.resolve(TEXTURE_RELATIVE)
            if (Files.isDirectory(shader) && Files.isDirectory(texture)) {
                return Roots(shader, texture)
            }
            directory = directory.parent
        }
        return null
    }

    private const val SHADER_RELATIVE =
        "build/win-cmake/generated/vulkanShaders/host/assets"
    private const val TEXTURE_RELATIVE = "native/rendering/assets"
}
