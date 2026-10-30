/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.files.DesktopExternalFile
import com.cytarch.cellularity.desktop.files.DesktopExternalFileKind
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuAction
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportOutcome
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportedFile
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportedFileKind
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuStore
import java.nio.file.Path
import java.util.UUID

/**
 * Brings a file the user opened the application with into a collection.
 *
 * This is the desktop's replacement for the Android import coordinator, and it exists because the
 * phone's imports arrive as an `ACTION_VIEW` intent while a window has nothing of the kind: what
 * Windows gives a desktop application is a path on its command line. Everything after that is the
 * shared machinery the phone already drives -- the store is told an import started and then how it
 * ended, and the store is what advances the collection, moves to the page the file belongs to,
 * reports the outcome, and opens an imported plate.
 *
 * Nothing is dispatched until the store has been told the import started, because that is what
 * puts the progress overlay in front of a person who has just double-clicked a file: a copy of a
 * large plate is not instant, and a window that answers a double-click with nothing at all looks
 * like a file association that does not work.
 */
internal object DesktopMainMenuImport {

    /**
     * Asks the person which file to bring in, through the system's own chooser.
     *
     * This is the desktop's half of what a phone does when a file manager hands the application a
     * document: the phone is started *with* a file, a window has to ask for one. The filter is by
     * extension because that is what decides the kind on both platforms -- the phone's resolver
     * follows the same rule -- so the filter and the import cannot disagree about what the file is.
     *
     * Null means the person closed the dialog, which is not a failure and is not reported.
     */
    fun chooseFile(parent: java.awt.Frame?): Path? {
        val dialog = java.awt.FileDialog(parent, CHOOSER_TITLE, java.awt.FileDialog.LOAD)
        dialog.setFilenameFilter { _, name ->
            val lower = name.lowercase()
            lower.endsWith(SUBSTRATE_SUFFIX) || lower.endsWith(GENOME_SUFFIX)
        }
        dialog.isVisible = true
        val directory = dialog.directory ?: return null
        val chosen = dialog.file ?: return null
        return Path.of(directory, chosen)
    }

    /**
     * Imports [source] and reports the outcome to [store].
     *
     * Returns the file as the interface sees it, or null when it was refused, so a caller can say
     * what happened without reading the store's state back.
     */
    suspend fun run(
        repository: DesktopCellularityFileRepository,
        store: MainMenuStore,
        source: Path
    ): MainMenuImportedFile? {
        if (store.state.value.importInProgress) {
            // A second file opened while the first is still arriving is dropped rather than
            // queued. Reporting it as a failure would be wrong -- nothing was attempted -- and
            // the store's own rule is one import at a time.
            return null
        }
        val commandId = UUID.randomUUID().toString()
        store.dispatch(MainMenuAction.ImportStarted(commandId))

        val outcome = when (val resolved = repository.resolveExternalFile(source)) {
            is StorageResult.Failure -> MainMenuImportOutcome.Failure(resolved.error)

            is StorageResult.Success -> {
                val file = resolved.value.toMainMenuImportedFile()
                when (val imported = repository.importExternalFile(resolved.value)) {
                    is StorageResult.Success -> MainMenuImportOutcome.Success(file)

                    is StorageResult.Failure -> MainMenuImportOutcome.Failure(
                        error = imported.error,
                        file = file
                    )
                }
            }
        }
        store.dispatch(MainMenuAction.ImportFinished(commandId, outcome))
        return (outcome as? MainMenuImportOutcome.Success)?.file
    }

    /**
     * The kind the main menu shows a file as.
     *
     * The two enumerations exist on opposite sides of the storage boundary, as they do on the
     * phone: the repository speaks about files, the menu about collections, and the translation
     * is the same one line there and here.
     */
    private fun DesktopExternalFile.toMainMenuImportedFile(): MainMenuImportedFile =
        MainMenuImportedFile(
            displayName = displayName,
            kind = when (kind) {
                DesktopExternalFileKind.SUBSTRATE -> MainMenuImportedFileKind.SUBSTRATE
                DesktopExternalFileKind.GENOME -> MainMenuImportedFileKind.GENOME
            }
        )

    /** What the title bar of the chooser says; the interface's own words are not available here. */
    private const val CHOOSER_TITLE = "Import a plate or a genome"

    private const val SUBSTRATE_SUFFIX = ".substrate"
    private const val GENOME_SUFFIX = ".genome"
}
