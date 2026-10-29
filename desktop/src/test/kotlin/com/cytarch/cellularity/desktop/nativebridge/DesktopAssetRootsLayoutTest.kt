/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import java.nio.file.Files
import java.nio.file.Path
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Where the renderer's files are found, in both of the layouts it has to work in.
 *
 * This is the part of packaging that a successful build says nothing about. A distributable
 * can contain every file and still have the application report the renderer as unavailable,
 * because the files are in the package and the runtime is looking somewhere else -- which is
 * what happened: the discovery walked up from the working directory, and a packaged launch
 * gets whatever working directory the user started it from.
 *
 * The layouts are created here rather than a real package being inspected, so what is
 * covered is the decision the code makes: which directory it looks in, and whether it
 * accepts what it finds. The renderer resolves `vulkan/shaders/<name>` and
 * `textures/<name>.png` against one list of roots, so there is one layout to get right and
 * these tests hold it to that shape.
 */
class DesktopAssetRootsLayoutTest {

    private var propertyBefore: String? = null

    @Before
    fun rememberTheProperty() {
        propertyBefore = System.getProperty(RESOURCES_PROPERTY)
    }

    @After
    fun restoreTheProperty() {
        // The property is process-wide, so leaving it set would decide the answer for every
        // other test in this JVM.
        if (propertyBefore == null) {
            System.clearProperty(RESOURCES_PROPERTY)
        } else {
            System.setProperty(RESOURCES_PROPERTY, propertyBefore)
        }
    }

    @Test
    fun `finds the shaders and textures of a packaged application`() {
        val assets = packagedLayout()
        val resources = assets.parent
        System.setProperty(RESOURCES_PROPERTY, resources.toString())

        val roots = DesktopAssetRoots.resolve()

        assertNotNull("a complete package must be recognised", roots)
        assertTrue(
            "the shader the renderer opens first must be reachable from the answered roots",
            Files.isRegularFile(
                roots!!.shaderRoot.resolve("vulkan/shaders/background.vert.spv")
            ) || Files.isRegularFile(
                roots.textureRoot.resolve("vulkan/shaders/background.vert.spv")
            )
        )
        assertTrue(
            "the cell texture must be reachable from the answered roots",
            Files.isRegularFile(roots.shaderRoot.resolve("textures/small_dots.png")) ||
                Files.isRegularFile(roots.textureRoot.resolve("textures/small_dots.png"))
        )
    }

    @Test
    fun `rejects a package whose shaders were not copied in`() {
        // The shader directory exists and is empty, which is what a staging step that
        // creates directories before copying files leaves behind. Accepting it would mean a
        // package that starts a session and cannot compile a program.
        val assets = packagedLayout(withShaders = false)
        System.setProperty(RESOURCES_PROPERTY, assets.parent.toString())
        val packaged = DesktopAssetRoots.Roots(assets, assets)

        assertFalse(
            "a package whose shader directory is empty must not be accepted",
            packaged.isValid
        )
    }

    @Test
    fun `rejects a package without textures`() {
        val assets = packagedLayout(withTextures = false)
        System.setProperty(RESOURCES_PROPERTY, assets.parent.toString())
        val packaged = DesktopAssetRoots.Roots(assets, assets)

        assertFalse(
            "a package without its cell texture must not be accepted",
            packaged.isValid
        )
    }

    @Test
    fun `a development run still finds the build tree`() {
        // The packaged layout is tried first, so the property being absent is what makes a
        // run from the repository behave as it did. This asserts the fallback is reachable
        // rather than only the new path being correct.
        System.clearProperty(RESOURCES_PROPERTY)

        val roots = DesktopAssetRoots.resolve()

        // A machine that has not compiled the native side has no build tree to find, which
        // is a legitimate state and not a failure of the discovery. This machine has one, so
        // the fallback is required to work rather than merely allowed to.
        val buildTree = Path.of("").toAbsolutePath()
            .let { working ->
                // Spelled out rather than read from the code under test, so this finds the
                // tree by the same fact an outside observer would use.
                generateSequence(working) { it.parent }
                    .firstOrNull {
                        it.resolve("build/win-cmake/generated/vulkanShaders/host/assets")
                            .let(Files::isDirectory)
                    }
            }
        if (buildTree != null) {
            assertNotNull(
                "a development run with a build tree must resolve its assets",
                roots
            )
        }

        // Whatever is accepted has to be usable, whichever layout answered. A development
        // run splits the two: the compiled shaders are in the build tree and the textures are
        // beside the sources, so each asset is in a different root and looking for one in the
        // root named after the other would reject a layout that works.
        if (roots != null) {
            assertTrue(
                "an accepted root pair must hold the shader the renderer opens first",
                reachable(roots, "vulkan/shaders/background.vert.spv")
            )
            assertTrue(
                "an accepted root pair must hold the cell texture",
                reachable(roots, "textures/small_dots.png")
            )
        }
    }

    private fun reachable(roots: DesktopAssetRoots.Roots, relative: String): Boolean =
        Files.isRegularFile(roots.shaderRoot.resolve(relative)) ||
            Files.isRegularFile(roots.textureRoot.resolve(relative))

    /** Creates a staged application tree and answers the directory its assets live in. */
    private fun packagedLayout(
        withShaders: Boolean = true,
        withTextures: Boolean = true
    ): Path {
        val resources = Files.createTempDirectory("cellularity-package")
        val assets = resources.resolve("native")
        Files.createDirectories(assets)
        if (withShaders) {
            writeFile(assets.resolve("vulkan/shaders/background.vert.spv"))
        } else {
            // The directory is created and left empty, which is the case worth telling apart
            // from an absent one.
            Files.createDirectories(assets.resolve("vulkan/shaders"))
        }
        if (withTextures) {
            writeFile(assets.resolve("textures/small_dots.png"))
        }
        return assets
    }

    private fun writeFile(path: Path) {
        Files.createDirectories(path.parent)
        Files.write(path, byteArrayOf(0))
    }

    private companion object {
        const val RESOURCES_PROPERTY = "compose.application.resources.dir"
    }
}
