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
        /**
         * Whether the renderer would find its assets through this root pair.
         *
         * The renderer is given both roots and searches them in order for every resource it
         * asks for, so a resource does not have to be in the root named after it. Both of the
         * layouts in use rely on that and in opposite ways: a development run keeps the
         * compiled shaders in the build tree and the textures beside the sources, so each
         * asset is in a different root, while a package keeps both in one. What has to hold
         * is that each required asset is in one of them.
         *
         * Existence of the directories alone is not enough. A staging step that creates the
         * shader directory and copies nothing into it leaves a layout that looks complete and
         * a session that cannot compile a program, so the assets themselves are asked for.
         * The two named here are the ones the renderer opens before it can draw at all.
         */
        val isValid: Boolean
            get() = holds(REQUIRED_SHADER) && holds(REQUIRED_TEXTURE)

        private fun holds(relative: String): Boolean =
            Files.isRegularFile(shaderRoot.resolve(relative)) ||
                Files.isRegularFile(textureRoot.resolve(relative))
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
            val roots = Roots(shader, texture)
            if (roots.isValid) return roots
            directory = directory.parent
        }
        return null
    }

    /**
     * The roots inside a packaged application, or null when this is not one.
     *
     * Both halves are the same directory, because a package is staged with its shaders and
     * its textures under one root. The pair is still reported as a pair rather than
     * collapsed to a single value, so that the shape the rest of the code sees does not
     * depend on which layout answered.
     *
     * A package that does not hold both required assets is not accepted, and falling through
     * to the build-tree walk is what lets a development run that happens to have the
     * property set still start.
     */
    private fun packagedRoots(): Roots? {
        val resources = System.getProperty(RESOURCES_DIRECTORY_PROPERTY)
            ?.takeIf { it.isNotBlank() }
            ?.let { Path.of(it) }
            ?: return null
        val native = resources.resolve(PACKAGED_NATIVE_DIRECTORY)
        val roots = Roots(shaderRoot = native, textureRoot = native)
        return roots.takeIf { it.isValid }
    }

    /** Where Compose Desktop leaves a packaged application's resources. */
    private const val RESOURCES_DIRECTORY_PROPERTY = "compose.application.resources.dir"

    /** The layout `stageNativeAssets` in desktop/build.gradle stages into. */
    private const val PACKAGED_NATIVE_DIRECTORY = "native"

    /**
     * The shader root relative to the build tree, and the directory the renderer opens
     * inside whichever root it is given.
     */
    private const val SHADER_RELATIVE =
        "build/win-cmake/generated/vulkanShaders/host/assets"
    private const val TEXTURE_RELATIVE = "native/rendering/assets"

    /**
     * Assets a usable root must hold to be worth choosing.
     *
     * The renderer opens the background program and the cell texture before it can draw
     * anything, so their absence is what separates a root that works from one that only
     * looks like it does.
     */
    private const val REQUIRED_SHADER = "vulkan/shaders/background.vert.spv"
    private const val REQUIRED_TEXTURE = "textures/small_dots.png"
}
