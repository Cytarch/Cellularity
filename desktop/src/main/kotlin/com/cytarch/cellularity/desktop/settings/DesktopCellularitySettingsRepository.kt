/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.settings

import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.CellularitySettingsJson
import com.cytarch.cellularity.shared.settings.CellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.SettingsError
import com.cytarch.cellularity.shared.settings.SettingsErrorCode
import com.cytarch.cellularity.shared.settings.SettingsRecoveryState
import com.cytarch.cellularity.shared.settings.SettingsResult
import java.io.IOException
import java.nio.charset.StandardCharsets
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/**
 * JVM settings storage for the desktop launcher.
 *
 * The Android implementation keeps its state in an internal atomic store that lives in
 * the application module, so it cannot be reused here. This is the same contract
 * written against java.nio instead: the settings document is written to a temporary
 * file, read back and compared before it replaces the real one, so a crash mid-write
 * leaves the previous document intact rather than a truncated one. That matters more
 * on a desktop than it does on a phone, because the file is visible and editable.
 *
 * A document that cannot be decoded is never discarded. It is moved aside as a recovery
 * copy and replaced with defaults, and [recovery] reports that this happened, because
 * silently overwriting a file the user edited by hand is worse than refusing to load it.
 */
class DesktopCellularitySettingsRepository(
    private val settingsFile: Path
) : CellularitySettingsRepository {

    private val mutex = Mutex()
    private val normalizedDefault = CellularitySettings.Default.normalized()
    private val mutableState = MutableStateFlow(normalizedDefault)
    private val mutableRecovery = MutableStateFlow<SettingsRecoveryState?>(null)

    private var initializationResult: SettingsResult<CellularitySettings>? = null
    private var pendingRecoveryError: SettingsError? = null
    private var hasCommittedDocument = false

    override val settings: StateFlow<CellularitySettings> = mutableState.asStateFlow()
    override val recovery: StateFlow<SettingsRecoveryState?> = mutableRecovery.asStateFlow()

    override suspend fun initialize(): SettingsResult<CellularitySettings> =
        onStorageDispatcher { checkCancellation ->
            mutex.withLock {
                initializationResult ?: initializeLocked(checkCancellation).also { result ->
                    initializationResult = result
                }
            }
        }

    override suspend fun writeSettings(
        settings: CellularitySettings
    ): SettingsResult<CellularitySettings> = onStorageDispatcher { checkCancellation ->
        mutex.withLock {
            if (initializationResult == null) {
                initializationResult = initializeLocked(checkCancellation)
            }
            // A document that failed to decode is moved aside before anything is
            // written over it. Writing first would destroy the only copy of whatever
            // the user had.
            preservePendingDocumentLocked(checkCancellation)?.let { return@withLock it }

            val normalized = settings.normalized()
            if (hasCommittedDocument && normalized == mutableState.value) {
                return@withLock SettingsResult.Success(normalized)
            }
            persistLocked(normalized, checkCancellation).also { result ->
                if (result is SettingsResult.Success) {
                    mutableState.value = normalized
                    hasCommittedDocument = true
                    initializationResult = result
                }
            }
        }
    }

    override suspend fun resetSettings(): SettingsResult<CellularitySettings> =
        onStorageDispatcher { checkCancellation ->
            mutex.withLock {
                persistLocked(normalizedDefault, checkCancellation).also { result ->
                    if (result is SettingsResult.Success) {
                        mutableState.value = normalizedDefault
                        mutableRecovery.value = null
                        pendingRecoveryError = null
                        hasCommittedDocument = true
                        initializationResult = result
                    }
                }
            }
        }

    private fun initializeLocked(
        checkCancellation: () -> Unit
    ): SettingsResult<CellularitySettings> {
        checkCancellation()
        return try {
            val directory = settingsFile.parent
            if (directory != null && !Files.isDirectory(directory)) {
                Files.createDirectories(directory)
            }
            if (!Files.exists(settingsFile)) {
                return persistLocked(mutableState.value, checkCancellation).also { result ->
                    if (result is SettingsResult.Success) hasCommittedDocument = true
                }
            }
            if (!Files.isRegularFile(settingsFile)) {
                return registerPendingRecovery(
                    SettingsError(
                        SettingsErrorCode.IO_FAILURE,
                        "Settings path is not a file"
                    )
                )
            }
            when (val decoded = readAndDecode(checkCancellation)) {
                is SettingsResult.Success -> {
                    val normalized = decoded.value.normalized()
                    mutableState.value = normalized
                    hasCommittedDocument = true
                    SettingsResult.Success(normalized)
                }
                is SettingsResult.Failure -> recoverAndResetLocked(
                    decoded.error,
                    checkCancellation
                )
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: Exception) {
            registerPendingRecovery(ioError(failure))
        }
    }

    /**
     * Moves an unreadable document aside and installs defaults.
     *
     * The reported result is the failure, not the successful reset: a caller that only
     * looked at the result would otherwise believe it had loaded the user's settings.
     */
    private fun recoverAndResetLocked(
        error: SettingsError,
        checkCancellation: () -> Unit
    ): SettingsResult<CellularitySettings> {
        pendingRecoveryError = error
        preservePendingDocumentLocked(checkCancellation)?.let { return it }

        val persisted = persistLocked(mutableState.value, checkCancellation)
        return if (persisted is SettingsResult.Success) {
            hasCommittedDocument = true
            SettingsResult.Failure(error)
        } else {
            persisted
        }
    }

    /** Returns a failure only when an unreadable document could not be preserved. */
    private fun preservePendingDocumentLocked(
        checkCancellation: () -> Unit
    ): SettingsResult.Failure? {
        val originalError = pendingRecoveryError ?: return null
        if (!Files.exists(settingsFile)) {
            pendingRecoveryError = null
            return null
        }
        if (!Files.isRegularFile(settingsFile)) {
            mutableRecovery.value = SettingsRecoveryState(originalError, false)
            return ioFailure("Unreadable settings path could not be preserved")
        }
        return try {
            Files.move(settingsFile, nextRecoveryFile())
            mutableRecovery.value = SettingsRecoveryState(originalError, true)
            pendingRecoveryError = null
            hasCommittedDocument = false
            null
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: Exception) {
            mutableRecovery.value = SettingsRecoveryState(originalError, false)
            SettingsResult.Failure(ioError(failure))
        }
    }

    private fun readAndDecode(
        checkCancellation: () -> Unit
    ): SettingsResult<CellularitySettings> {
        checkCancellation()
        return try {
            val size = Files.size(settingsFile)
            if (size > MAXIMUM_DOCUMENT_BYTES) {
                return SettingsResult.Failure(
                    SettingsError(
                        SettingsErrorCode.TOO_LARGE,
                        "Settings document is too large"
                    )
                )
            }
            // The size is checked before the read, so a file that is over the limit is
            // never pulled into memory just to be rejected.
            val bytes = Files.readAllBytes(settingsFile)
            if (bytes.size.toLong() > MAXIMUM_DOCUMENT_BYTES) {
                return SettingsResult.Failure(
                    SettingsError(
                        SettingsErrorCode.TOO_LARGE,
                        "Settings document grew while it was being read"
                    )
                )
            }
            CellularitySettingsJson.decode(String(bytes, StandardCharsets.UTF_8))
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: Exception) {
            SettingsResult.Failure(ioError(failure))
        }
    }

    private fun persistLocked(
        value: CellularitySettings,
        checkCancellation: () -> Unit
    ): SettingsResult<CellularitySettings> {
        val normalized = value.normalized()
        val encoded = try {
            CellularitySettingsJson.encode(normalized).toByteArray(StandardCharsets.UTF_8)
        } catch (failure: Exception) {
            return ioFailure(failure.message.orEmpty())
        }
        if (encoded.size.toLong() > MAXIMUM_DOCUMENT_BYTES) {
            return SettingsResult.Failure(
                SettingsError(SettingsErrorCode.TOO_LARGE, "Settings document is too large")
            )
        }

        return try {
            val directory = settingsFile.parent
            if (directory != null && !Files.isDirectory(directory)) {
                Files.createDirectories(directory)
            }
            checkCancellation()

            // Written beside the target and then moved over it. The move is atomic on
            // the same volume, so a reader either sees the whole previous document or
            // the whole new one, never a half-written file.
            val temporary = Files.createTempFile(
                directory,
                settingsFile.fileName.toString(),
                ".tmp"
            )
            try {
                Files.write(
                    temporary,
                    encoded,
                    StandardOpenOption.WRITE,
                    StandardOpenOption.TRUNCATE_EXISTING
                )
                checkCancellation()
                // Read back what was actually written. A document that round-trips to
                // something else means the encoder or the normalizer is not stable, and
                // committing that would make every later load disagree with what the
                // user set.
                val decoded = decodeFile(temporary)
                when (decoded) {
                    is SettingsResult.Success -> if (decoded.value.normalized() != normalized) {
                        throw IOException("Encoded settings changed during validation")
                    }
                    is SettingsResult.Failure -> throw IOException(
                        "Encoded settings failed validation: ${decoded.error.code}"
                    )
                }
                // Replacing an existing file atomically is asked for first and tolerated when it
                // is refused. Not every file system can replace a file in one step, and the ones
                // that cannot say so by refusing the call rather than by doing it badly -- so the
                // refusal is caught and answered with the ordinary move, which is not atomic
                // between the unlink and the rename but is what every platform can do.
                //
                // Asking for both options and expecting them all to be honoured is how a settings
                // document that already exists becomes one that can never be written again: the
                // file is there, the replacement is refused, and every later attempt fails the
                // same way for the same reason. The fallback is the difference between a
                // preference and a requirement.
                try {
                    Files.move(
                        temporary,
                        settingsFile,
                        StandardCopyOption.REPLACE_EXISTING,
                        StandardCopyOption.ATOMIC_MOVE
                    )
                } catch (atomic: AtomicMoveNotSupportedException) {
                    Files.move(
                        temporary,
                        settingsFile,
                        StandardCopyOption.REPLACE_EXISTING
                    )
                }
            } catch (failure: Exception) {
                Files.deleteIfExists(temporary)
                throw failure
            }
            SettingsResult.Success(normalized)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: Exception) {
            SettingsResult.Failure(ioError(failure))
        }
    }

    private fun decodeFile(file: Path): SettingsResult<CellularitySettings> {
        val bytes = Files.readAllBytes(file)
        return CellularitySettingsJson.decode(String(bytes, StandardCharsets.UTF_8))
    }

    private fun registerPendingRecovery(error: SettingsError): SettingsResult.Failure {
        pendingRecoveryError = error
        mutableRecovery.value = SettingsRecoveryState(error, false)
        return SettingsResult.Failure(error)
    }

    /**
     * Recovery copies are numbered rather than overwritten, so a second failure does
     * not destroy the document the first one preserved.
     */
    private fun nextRecoveryFile(): Path {
        val baseName = "${settingsFile.fileName}.recovery"
        val directory = settingsFile.parent
        for (index in 0 until MAXIMUM_RECOVERY_FILES) {
            val suffix = if (index == 0) "" else ".$index"
            val candidate = if (directory == null) {
                settingsFile.fileSystem.getPath("$baseName$suffix")
            } else {
                directory.resolve("$baseName$suffix")
            }
            if (!Files.exists(candidate)) return candidate
        }
        throw IOException("Too many settings recovery files")
    }

    private suspend fun <R> onStorageDispatcher(
        operation: suspend (checkCancellation: () -> Unit) -> R
    ): R = withContext(Dispatchers.IO) {
        val context = currentCoroutineContext()
        context.ensureActive()
        operation { context.ensureActive() }
    }

    /**
     * What went wrong, in words that identify it.
     *
     * The message alone is not enough and this was learned from a real report: an access refusal
     * carries the path and nothing else, so the interface said "not saved: IO_FAILURE, <path>" and
     * the path was already known -- it was in the same sentence. A failure that names what happened
     * rather than where is the difference between a report somebody can act on and one that only
     * says a thing did not work.
     */
    private fun ioError(failure: Exception): SettingsError = SettingsError(
        SettingsErrorCode.IO_FAILURE,
        buildString {
            append(failure.javaClass.simpleName)
            val message = failure.message
            if (!message.isNullOrBlank()) {
                append(": ")
                append(message)
            }
            // The cause is where the useful sentence usually lives: the platform's own exception
            // wraps ours and keeps the reason in the inner one.
            val cause = failure.cause
            if (cause != null && cause !== failure) {
                append(" (caused by ")
                append(cause.javaClass.simpleName)
                val causeMessage = cause.message
                if (!causeMessage.isNullOrBlank()) {
                    append(": ")
                    append(causeMessage)
                }
                append(')')
            }
        }
    )

    private fun ioFailure(detail: String): SettingsResult.Failure =
        SettingsResult.Failure(SettingsError(SettingsErrorCode.IO_FAILURE, detail))

    private companion object {
        /** Generous for a settings document, tight enough to bound a hostile file. */
        const val MAXIMUM_DOCUMENT_BYTES = 1L * 1024L * 1024L
        const val MAXIMUM_RECOVERY_FILES = 16
    }
}
