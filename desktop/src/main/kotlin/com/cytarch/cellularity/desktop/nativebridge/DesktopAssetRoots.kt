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
 * So the caller states them, and this finds them in one of two layouts.
 *
 * A packaged application carries its own copy, under the resource directory Compose
 * Desktop passes as `compose.application.resources.dir`; that is tried first because it is
 * the only layout an install has. A development run reads the build tree instead, found by
 * walking up from the working directory, which is what both Gradle and a run from the
 * repository root give us.
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

        packagedRoots()?.let { return it }

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

    /**
     * The roots inside a packaged application, or null when this is not one.
     *
     * The two roots are not siblings and are not the same directory. The renderer opens
     * `vulkan/shaders/<name>.spv` inside the shader root and `textures/<name>.png` inside the
     * texture root, so the shader root has to be the directory that *contains* `vulkan`, and
     * the texture root the one that contains `textures`. The staging puts both under one
     * `native` directory, which makes them look interchangeable here; they are not, and
     * pointing the shader root at the texture directory produces a package that finds no
     * shaders at all and fails while starting a session rather than at build time.
     *
     * Both halves have to be present for this to answer: a resources directory that holds
     * only one of them is an incomplete package, and falling through to the build-tree walk
     * lets a development run that happens to have the property set still start.
     */
    private fun packagedRoots(): Roots? {
        val resources = System.getProperty(RESOURCES_DIRECTORY_PROPERTY)
            ?.takeIf { it.isNotBlank() }
            ?.let { Path.of(it) }
            ?: return null
        val native = resources.resolve(PACKAGED_NATIVE_DIRECTORY)
        val roots = Roots(
            shaderRoot = native,
            textureRoot = native.resolve(PACKAGED_TEXTURE_RELATIVE)
        )
        return roots.takeIf { it.isValid }
    }

    /** Where Compose Desktop leaves a packaged application's resources. */
    private const val RESOURCES_DIRECTORY_PROPERTY = "compose.application.resources.dir"

    /** The layout `stageNativeAssets` in desktop/build.gradle stages into. */
    private const val PACKAGED_NATIVE_DIRECTORY = "native"
    private const val PACKAGED_TEXTURE_RELATIVE = "textures"

    /**
     * The shader root relative to the build tree, and the directory the renderer opens
     * inside whichever root it is given.
     */
    private const val SHADER_RELATIVE =
        "build/win-cmake/generated/vulkanShaders/host/assets"
    private const val TEXTURE_RELATIVE = "native/rendering/assets"
}
