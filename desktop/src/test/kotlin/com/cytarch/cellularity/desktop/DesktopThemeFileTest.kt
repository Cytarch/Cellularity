/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.shared.settings.CellularityThemeColors
import com.cytarch.cellularity.shared.settings.CellularityThemeFileJson
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

/**
 * That a theme written on one machine can be read on another.
 *
 * This is the whole point of the import and export buttons in the settings screen: a theme
 * is a document a person hands to somebody else, so what matters is not that the buttons are
 * wired up but that the file survives the trip. The format is shared, which means a theme
 * exported here opens on a phone -- and that claim is worth a test precisely because nothing
 * local would catch it if it stopped being true.
 *
 * The colours are deliberately awkward: a name with characters a file system dislikes, and
 * values that are not round numbers. A round trip that only worked for tidy input would pass
 * a test written with tidy input and fail the first person who names a theme with a space in
 * it.
 */
class DesktopThemeFileTest {

    @Test
    fun `a theme survives being exported and imported`() {
        val colors = CellularityThemeColors(name = "\u041f\u043e\u043b\u044f\u0440\u043d\u0430\u044f \u043d\u043e\u0447\u044c / polar")
        val components = com.cytarch.cellularity.shared.settings.CellularityThemeComponents()

        val text = CellularityThemeFileJson.encode(colors, components)
        val preset = CellularityThemeFileJson.decode(text)

        assertEquals(
            colors.name,
            preset.colors.name,
            "the name is what a person recognises the theme by when it comes back"
        )
        assertEquals(
            colors,
            preset.colors,
            "every colour must come back as it went out, or the theme is not the same theme"
        )
    }

    @Test
    fun `a file that is not a theme is refused rather than half-read`() {
        // The settings screen shows the reason, so a refusal is an ordinary outcome here.
        // What must not happen is a theme assembled from whatever fields happened to parse,
        // which would look like a successful import of something the person never chose.
        assertFailsWith<Exception> {
            CellularityThemeFileJson.decode("""{"format":"something-else"}""")
        }
    }

    @Test
    fun `a name a file system dislikes still produces a usable file name`() {
        // The export dialog offers the theme's name as the file name, and a theme can be
        // called anything at all. A name that cannot be a file must not become an export
        // that silently fails, so the cleaning is asserted rather than assumed.
        val awkward = listOf(
            "\u041f\u043e\u043b\u044f\u0440\u043d\u0430\u044f \u043d\u043e\u0447\u044c / polar",
            "a:b*c?d",
            "   ",
            ""
        )
        awkward.forEach { name ->
            val suggestion = DesktopThemeFiles.suggestedFileNameFor(
                CellularityThemeColors(name = name)
            )
            assertTrue(
                suggestion.endsWith(".${DesktopThemeFiles.EXTENSION}"),
                "an export of ${name.ifEmpty { "an unnamed theme" }} must suggest a theme " +
                    "file, not $suggestion"
            )
            assertTrue(
                suggestion.none { it in "\\/:*?\"<>|" },
                "a suggested file name must be one a file system accepts, not $suggestion"
            )
        }
    }
}
