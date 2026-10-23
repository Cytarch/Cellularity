package com.cytarch.cellularity.platform.settings

import com.cytarch.cellularity.platform.files.AtomicFileStore
import com.cytarch.cellularity.platform.files.FileSizeLimitExceededException
import com.cytarch.cellularity.platform.files.copyWithLimit
import com.cytarch.cellularity.shared.settings.SettingsError
import com.cytarch.cellularity.shared.settings.SettingsErrorCode
import com.cytarch.cellularity.shared.settings.SettingsRecoveryState
import com.cytarch.cellularity.shared.settings.SettingsResult
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
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
 * Owns one small versioned JSON document.
 *
 * A decoded value is published only after a complete read or durable commit. An unreadable
 * current document is moved to a sibling recovery file before normal writes may replace it.
 */
internal class AtomicJsonSettingsStore<T>(
    private val target: File,
    defaultValue: T,
    private val normalize: (T) -> T,
    private val encode: (T) -> String,
    private val decode: (String) -> SettingsResult<T>,
    private val atomicFiles: AtomicFileStore = AtomicFileStore(),
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val maximumBytes: Long = MAX_SETTINGS_BYTES
) {
    private val mutex = Mutex()
    private val normalizedDefault = normalize(defaultValue)
    private val mutableState = MutableStateFlow(normalizedDefault)
    private val mutableRecovery = MutableStateFlow<SettingsRecoveryState?>(null)

    private var initializationResult: SettingsResult<T>? = null
    private var pendingRecoveryError: SettingsError? = null
    private var hasCommittedDocument = false

    val state: StateFlow<T> = mutableState.asStateFlow()
    val recovery: StateFlow<SettingsRecoveryState?> = mutableRecovery.asStateFlow()

    suspend fun initialize(): SettingsResult<T> = onStorageDispatcher { checkCancellation ->
        mutex.withLock {
            initializationResult ?: initializeLocked(checkCancellation).also { result ->
                initializationResult = result
            }
        }
    }

    suspend fun write(value: T): SettingsResult<T> = onStorageDispatcher { checkCancellation ->
        mutex.withLock {
            if (initializationResult == null) {
                initializationResult = initializeLocked(checkCancellation)
            }
            val recoveryResult = preservePendingDocumentLocked(checkCancellation)
            if (recoveryResult != null) return@withLock recoveryResult

            val normalized = normalize(value)
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

    suspend fun reset(): SettingsResult<T> = onStorageDispatcher { checkCancellation ->
        mutex.withLock {
            val defaults = normalizedDefault
            persistLocked(defaults, checkCancellation).also { result ->
                if (result is SettingsResult.Success) {
                    mutableState.value = defaults
                    mutableRecovery.value = null
                    pendingRecoveryError = null
                    hasCommittedDocument = true
                    initializationResult = result
                }
            }
        }
    }

    private fun initializeLocked(checkCancellation: () -> Unit): SettingsResult<T> {
        checkCancellation()
        try {
            val directory = target.parentFile?.canonicalFile
                ?: return ioFailure("Settings directory is unavailable")
            if (!directory.isDirectory && !directory.mkdirs()) {
                return ioFailure("Could not create settings directory")
            }
            atomicFiles.cleanupTemporaryFiles(directory)
            if (!target.exists()) {
                val defaults = mutableState.value
                return persistLocked(defaults, checkCancellation).also { result ->
                    if (result is SettingsResult.Success) hasCommittedDocument = true
                }
            }
            if (!target.isFile) {
                return registerPendingRecovery(
                    SettingsError(SettingsErrorCode.IO_FAILURE, "Settings path is not a file")
                )
            }

            return when (val decoded = readAndDecodeFile(target, checkCancellation)) {
                is SettingsResult.Success -> {
                    val normalized = normalize(decoded.value)
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
            return registerPendingRecovery(ioError(failure))
        }
    }

    private fun recoverAndResetLocked(
        error: SettingsError,
        checkCancellation: () -> Unit
    ): SettingsResult<T> {
        pendingRecoveryError = error
        val preserveFailure = preservePendingDocumentLocked(checkCancellation)
        if (preserveFailure != null) return preserveFailure

        val defaults = mutableState.value
        val persisted = persistLocked(defaults, checkCancellation)
        if (persisted is SettingsResult.Success) {
            hasCommittedDocument = true
            return SettingsResult.Failure(error)
        }
        return persisted
    }

    /** Returns a failure only when a current unreadable document could not be preserved. */
    private fun preservePendingDocumentLocked(
        checkCancellation: () -> Unit
    ): SettingsResult.Failure? {
        val originalError = pendingRecoveryError ?: return null
        if (!target.exists()) {
            pendingRecoveryError = null
            return null
        }
        if (!target.isFile) {
            mutableRecovery.value = SettingsRecoveryState(
                error = originalError,
                recoverableCopyAvailable = false
            )
            return ioFailure("Unreadable settings path could not be preserved")
        }

        return try {
            atomicFiles.rename(
                source = target,
                target = nextRecoveryFile(),
                replaceExisting = false,
                checkCancellation = checkCancellation
            )
            mutableRecovery.value = SettingsRecoveryState(
                error = originalError,
                recoverableCopyAvailable = true
            )
            pendingRecoveryError = null
            hasCommittedDocument = false
            null
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: Exception) {
            mutableRecovery.value = SettingsRecoveryState(
                error = originalError,
                recoverableCopyAvailable = false
            )
            SettingsResult.Failure(ioError(failure))
        }
    }

    private fun readAndDecodeFile(
        file: File,
        checkCancellation: () -> Unit
    ): SettingsResult<T> {
        if (file.length() > maximumBytes) {
            return SettingsResult.Failure(
                SettingsError(SettingsErrorCode.TOO_LARGE, "Settings document is too large")
            )
        }
        return try {
            val bytes = ByteArrayOutputStream()
            FileInputStream(file).use { input ->
                copyWithLimit(input, bytes, maximumBytes, checkCancellation)
            }
            decode(bytes.toString(Charsets.UTF_8.name()))
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: FileSizeLimitExceededException) {
            SettingsResult.Failure(
                SettingsError(SettingsErrorCode.TOO_LARGE, "Settings document is too large")
            )
        } catch (failure: Exception) {
            SettingsResult.Failure(ioError(failure))
        }
    }

    private fun persistLocked(
        value: T,
        checkCancellation: () -> Unit
    ): SettingsResult<T> {
        val encoded = try {
            encode(normalize(value)).toByteArray(Charsets.UTF_8)
        } catch (failure: Exception) {
            return ioFailure(failure.message.orEmpty())
        }
        if (encoded.size.toLong() > maximumBytes) {
            return SettingsResult.Failure(
                SettingsError(SettingsErrorCode.TOO_LARGE, "Settings document is too large")
            )
        }

        return try {
            val directory = target.parentFile?.canonicalFile
                ?: return ioFailure("Settings directory is unavailable")
            if (!directory.isDirectory && !directory.mkdirs()) {
                return ioFailure("Could not create settings directory")
            }
            atomicFiles.replace(
                target = target,
                maximumBytes = maximumBytes,
                checkCancellation = checkCancellation,
                produce = { temporaryFile ->
                    FileOutputStream(temporaryFile).use { output -> output.write(encoded) }
                },
                validate = { temporaryFile ->
                    when (val validation = readAndDecodeFile(temporaryFile, checkCancellation)) {
                        is SettingsResult.Success -> if (
                            normalize(validation.value) != normalize(value)
                        ) {
                            throw IOException("Encoded settings changed during validation")
                        }
                        is SettingsResult.Failure -> throw IOException(
                            "Encoded settings failed validation: ${validation.error.code}"
                        )
                    }
                }
            )
            SettingsResult.Success(normalize(value))
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (_: FileSizeLimitExceededException) {
            SettingsResult.Failure(
                SettingsError(SettingsErrorCode.TOO_LARGE, "Settings document is too large")
            )
        } catch (failure: Exception) {
            SettingsResult.Failure(ioError(failure))
        }
    }

    private fun registerPendingRecovery(error: SettingsError): SettingsResult.Failure {
        pendingRecoveryError = error
        mutableRecovery.value = SettingsRecoveryState(
            error = error,
            recoverableCopyAvailable = false
        )
        return SettingsResult.Failure(error)
    }

    private fun nextRecoveryFile(): File {
        val directory = requireNotNull(target.parentFile)
        val baseName = "${target.name}.recovery"
        var index = 0
        while (index < MAX_RECOVERY_FILES) {
            val suffix = if (index == 0) "" else ".$index"
            val candidate = File(directory, "$baseName$suffix")
            if (!candidate.exists()) return candidate
            index++
        }
        throw IOException("Too many settings recovery files")
    }

    private suspend fun <R> onStorageDispatcher(
        operation: suspend (checkCancellation: () -> Unit) -> R
    ): R = withContext(dispatcher) {
        val context = currentCoroutineContext()
        context.ensureActive()
        operation { context.ensureActive() }
    }

    private fun ioFailure(detail: String): SettingsResult.Failure = SettingsResult.Failure(
        SettingsError(SettingsErrorCode.IO_FAILURE, detail)
    )

    private fun ioError(failure: Exception): SettingsError = SettingsError(
        SettingsErrorCode.IO_FAILURE,
        failure.message.orEmpty()
    )

    private companion object {
        const val MAX_SETTINGS_BYTES = 256L * 1024L
        const val MAX_RECOVERY_FILES = 1_000
    }
}
