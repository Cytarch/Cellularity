package com.cytarch.cellularity.shared.files

/** Platform-neutral completion result for a system share-sheet request. */
sealed interface StoredFileShareOutcome {
    /** The platform accepted the request and opened its chooser. */
    data object Started : StoredFileShareOutcome

    /** No installed application can handle the share request. */
    data object NoCompatibleTarget : StoredFileShareOutcome

    /** Preparing or launching the shared file failed. */
    data class Failure(val error: StorageError) : StoredFileShareOutcome
}
