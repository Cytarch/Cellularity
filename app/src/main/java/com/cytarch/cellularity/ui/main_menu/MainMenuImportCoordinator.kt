package com.cytarch.cellularity.ui.main_menu

import android.content.ContentResolver
import android.content.Context
import android.content.Intent
import android.net.Uri
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.files.ExternalCellularityFile
import com.cytarch.cellularity.platform.files.ExternalCellularityFileKind
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportOutcome
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportedFile
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportedFileKind
import java.util.UUID

internal data class MainMenuImportCommand(
    val commandId: String,
    val contentUri: String?
) {
    init {
        require(commandId.isNotBlank()) { "Import command ID must not be blank" }
    }
}

internal fun interface MainMenuImportExecutor {
    suspend fun execute(command: MainMenuImportCommand): MainMenuImportOutcome
}

/** Owns Android Intent/Uri parsing and ContentResolver-backed import transport. */
internal class AndroidMainMenuImportCoordinator private constructor(
    private val fileRepository: AndroidCellularityFileRepository,
    private val commandIdFactory: () -> String
) : MainMenuImportExecutor {
    constructor(context: Context) : this(
        fileRepository = AndroidCellularityFileRepository(context.applicationContext),
        commandIdFactory = { UUID.randomUUID().toString() }
    )

    fun commandFrom(intent: Intent): MainMenuImportCommand? {
        if (intent.action != Intent.ACTION_VIEW) return null

        val commandId = intent.getStringExtra(EXTRA_IMPORT_COMMAND_ID)
            ?.takeIf(String::isNotBlank)
            ?: commandIdFactory().also { generatedId ->
                intent.putExtra(EXTRA_IMPORT_COMMAND_ID, generatedId)
            }
        return MainMenuImportCommand(
            commandId = commandId,
            contentUri = intent.data?.toString()
        )
    }

    override suspend fun execute(command: MainMenuImportCommand): MainMenuImportOutcome {
        val sourceUri = command.contentUri
            ?.takeIf(String::isNotBlank)
            ?.let(Uri::parse)
        if (sourceUri?.scheme != ContentResolver.SCHEME_CONTENT) {
            return MainMenuImportOutcome.Failure(
                StorageError(
                    code = StorageErrorCode.UNSUPPORTED_FORMAT,
                    detail = "External import requires a content URI"
                )
            )
        }

        return when (val resolved = fileRepository.resolveExternalFile(sourceUri)) {
            is StorageResult.Failure -> MainMenuImportOutcome.Failure(resolved.error)
            is StorageResult.Success -> {
                val importedFile = resolved.value.toMainMenuImportedFile()
                when (val imported = fileRepository.importExternalFile(resolved.value)) {
                    is StorageResult.Success -> MainMenuImportOutcome.Success(importedFile)
                    is StorageResult.Failure -> MainMenuImportOutcome.Failure(
                        error = imported.error,
                        file = importedFile
                    )
                }
            }
        }
    }

    private fun ExternalCellularityFile.toMainMenuImportedFile(): MainMenuImportedFile =
        MainMenuImportedFile(
            displayName = displayName,
            kind = when (kind) {
                ExternalCellularityFileKind.SUBSTRATE -> MainMenuImportedFileKind.SUBSTRATE
                ExternalCellularityFileKind.GENOME -> MainMenuImportedFileKind.GENOME
            }
        )

    private companion object {
        const val EXTRA_IMPORT_COMMAND_ID =
            "com.cytarch.cellularity.extra.MAIN_MENU_IMPORT_COMMAND_ID"
    }
}
