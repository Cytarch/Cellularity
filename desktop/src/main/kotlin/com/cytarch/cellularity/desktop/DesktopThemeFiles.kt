/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.shared.settings.CellularityThemeColors
import com.cytarch.cellularity.shared.settings.CellularityThemeComponents
import com.cytarch.cellularity.shared.settings.CellularityThemeFileJson
import java.awt.FileDialog
import java.awt.Frame
import java.nio.file.Files
import java.nio.file.Path

/**
 * Reading and writing theme files, through the picker a desktop actually has.
 *
 * The format is not written here. `CellularityThemeFileJson` is shared, so a theme exported
 * on a phone opens on a desktop and the other way round; what is desktop-specific is only
 * where the bytes go, which is a file the person chooses rather than a share sheet.
 *
 * A picker rather than a fixed directory is the point of the button. Writing to a path the
 * application picked would produce a file that exports and imports and is invisible in
 * between, which is not what a person means by exporting a theme -- they mean handing it to
 * someone. The picker is the platform's own, so it is also the dialog they already know.
 */
object DesktopThemeFiles {

    const val EXTENSION = "cltheme"

    /**
     * The file name offered when exporting this theme.
     *
     * Exposed rather than kept private because the cleaning is a rule about file systems
     * rather than about the dialog, and it is the part that can be wrong in a way nobody
     * sees until a person names a theme awkwardly.
     */
    fun suggestedFileNameFor(colors: CellularityThemeColors): String =
        "${safeFileName(colors.name)}.$EXTENSION"

    /**
     * Asks for a place to write the theme and writes it there.
     *
     * Returns a description of what happened rather than throwing: this runs from a click,
     * and a cancelled dialog is not a failure, so it is reported as an ordinary outcome. The
     * result is the text the settings screen shows back.
     */
    fun export(
        parent: Frame?,
        colors: CellularityThemeColors,
        components: CellularityThemeComponents
    ): String {
        val target = chooseFile(
            parent = parent,
            title = "Export theme",
            mode = FileDialog.SAVE,
            suggestedName = suggestedFileNameFor(colors)
        ) ?: return "Export cancelled"
        return try {
            Files.write(target, CellularityThemeFileJson.encode(colors, components).toByteArray())
            "Theme exported to ${target.fileName}"
        } catch (thrown: Exception) {
            "The theme could not be written: ${thrown.message ?: thrown::class.simpleName}"
        }
    }

    /**
     * Asks for a theme file and returns what it contains.
     *
     * A file that is not a theme is reported as such rather than as a crash. `decode` refuses
     * a wrong format marker and a newer version by throwing, which is right -- it cannot
     * invent a meaning for them -- so the refusal is translated here into something the
     * settings screen can say.
     */
    fun import(parent: Frame?): ImportedTheme {
        val source = chooseFile(
            parent = parent,
            title = "Import theme",
            mode = FileDialog.LOAD,
            suggestedName = null
        ) ?: return ImportedTheme(
            colors = null,
            components = null,
            message = "Import cancelled"
        )
        return try {
            val text = String(Files.readAllBytes(source))
            val preset = CellularityThemeFileJson.decode(text)
            ImportedTheme(
                colors = preset.colors,
                components = preset.components,
                message = "Theme imported from ${source.fileName}"
            )
        } catch (thrown: Exception) {
            ImportedTheme(
                colors = null,
                components = null,
                message = "That file is not a theme: ${thrown.message ?: thrown::class.simpleName}"
            )
        }
    }

    /** What an import attempt produced: a theme, or the reason there is not one. */
    data class ImportedTheme(
        val colors: CellularityThemeColors?,
        val components: CellularityThemeComponents?,
        val message: String
    ) {
        val succeeded: Boolean
            get() = colors != null && components != null
    }

    private fun chooseFile(
        parent: Frame?,
        title: String,
        mode: Int,
        suggestedName: String?
    ): Path? {
        val dialog = FileDialog(parent, title, mode)
        if (suggestedName != null) {
            dialog.file = suggestedName
        } else {
            dialog.setFilenameFilter { _, name -> name.endsWith(".$EXTENSION") }
        }
        dialog.isVisible = true

        val directory = dialog.directory ?: return null
        val chosen = dialog.file ?: return null
        return Path.of(directory, chosen)
    }

    /**
     * A theme's name as something a file system will take.
     *
     * A theme is free text and can be called anything, including things a file cannot be
     * called. The fallback is the extension's own stem so that a theme with an unusable name
     * still produces a file rather than a silent failure.
     */
    private fun safeFileName(name: String): String {
        val cleaned = name.trim().map { character ->
            if (character.isLetterOrDigit() || character == '-' || character == '_') {
                character
            } else {
                '_'
            }
        }.joinToString("")
        return cleaned.trim('_').ifEmpty { "theme" }
    }
}
