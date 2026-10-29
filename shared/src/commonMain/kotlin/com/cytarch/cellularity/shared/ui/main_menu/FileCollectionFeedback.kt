package com.cytarch.cellularity.shared.ui.main_menu

import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.file_collection_invalid_name_message
import com.cytarch.cellularity.shared.generated.resources.file_collection_file_too_large_message
import com.cytarch.cellularity.shared.generated.resources.file_collection_operation_failed_message
import com.cytarch.cellularity.shared.generated.resources.file_collection_genome_name_already_exists_message
import com.cytarch.cellularity.shared.generated.resources.file_collection_no_share_target_message
import com.cytarch.cellularity.shared.generated.resources.file_collection_substrate_name_already_exists_message
import com.cytarch.cellularity.shared.generated.resources.file_collection_newer_version_message
import com.cytarch.cellularity.shared.generated.resources.file_operation_saved_message
import org.jetbrains.compose.resources.StringResource

enum class FileCollectionOperation {
    REFRESH,
    DELETE,
    RENAME,
    SHARE,

    /**
     * A file arriving from outside the application.
     *
     * Kept apart from the others because it is the only one of them that is not about the
     * collection's own files: nothing in a collection is renamed, deleted or shared, and a
     * diagnostic that said so would send a reader looking in the wrong place.
     */
    IMPORT
}

enum class FileCollectionFeedback {
    BAD_NAME,
    SUBSTRATE_NAME_EXISTS,
    GENOME_NAME_EXISTS,
    FILE_TOO_LARGE,
    NEWER_VERSION,
    FILE_UNAVAILABLE,
    NO_SHARE_TARGET,

    /** The operation did what it was asked to, which some callers report as well. */
    SAVED
}

/** User-facing meaning plus the exact storage cause retained for diagnostics. */
data class FileCollectionNotice(
    val operation: FileCollectionOperation,
    val feedback: FileCollectionFeedback,
    val storageError: StorageError? = null
)

/**
 * Which collection a file belongs to.
 *
 * Public because the launcher turns an import failure into a message with the same mapping
 * the collection screens use, and that mapping is written in terms of this. The alternative
 * -- a second mapping on the desktop -- would have to be told about every future reason
 * separately, and the one that was forgotten would be the one nobody tested.
 */
enum class FileCollectionKind {
    SUBSTRATES,
    GENOMES
}

fun storageNotice(
    kind: FileCollectionKind,
    operation: FileCollectionOperation,
    error: StorageError
): FileCollectionNotice = FileCollectionNotice(
    operation = operation,
    feedback = when (error.code) {
        StorageErrorCode.BAD_NAME -> FileCollectionFeedback.BAD_NAME
        StorageErrorCode.NAME_EXISTS -> when (kind) {
            FileCollectionKind.SUBSTRATES -> FileCollectionFeedback.SUBSTRATE_NAME_EXISTS
            FileCollectionKind.GENOMES -> FileCollectionFeedback.GENOME_NAME_EXISTS
        }
        StorageErrorCode.TOO_LARGE -> FileCollectionFeedback.FILE_TOO_LARGE
        StorageErrorCode.NEWER_VERSION -> FileCollectionFeedback.NEWER_VERSION
        StorageErrorCode.NOT_FOUND,
        StorageErrorCode.UNSUPPORTED_FORMAT,
        StorageErrorCode.CORRUPTED,
        StorageErrorCode.PERMISSION_REVOKED,
        StorageErrorCode.IO_FAILURE -> FileCollectionFeedback.FILE_UNAVAILABLE
    },
    storageError = error
)

fun FileCollectionFeedback.messageResource(): StringResource = when (this) {
    FileCollectionFeedback.BAD_NAME -> Res.string.file_collection_invalid_name_message
    FileCollectionFeedback.SUBSTRATE_NAME_EXISTS ->
        Res.string.file_collection_substrate_name_already_exists_message
    FileCollectionFeedback.GENOME_NAME_EXISTS -> Res.string.file_collection_genome_name_already_exists_message
    FileCollectionFeedback.FILE_TOO_LARGE -> Res.string.file_collection_file_too_large_message
    FileCollectionFeedback.NEWER_VERSION -> Res.string.file_collection_newer_version_message
    FileCollectionFeedback.FILE_UNAVAILABLE -> Res.string.file_collection_operation_failed_message
    FileCollectionFeedback.NO_SHARE_TARGET -> Res.string.file_collection_no_share_target_message
    FileCollectionFeedback.SAVED -> Res.string.file_operation_saved_message
}
