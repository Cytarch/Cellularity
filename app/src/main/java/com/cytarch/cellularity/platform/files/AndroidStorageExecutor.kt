package com.cytarch.cellularity.platform.files

import com.cytarch.cellularity.CellularityDataException
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import java.io.FileNotFoundException
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

/** Executes every UI-facing file operation away from the Android main dispatcher. */
internal class AndroidStorageExecutor(
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO
) {
    suspend fun <T> execute(
        operation: (checkCancellation: () -> Unit) -> T
    ): StorageResult<T> = withContext(dispatcher) {
        val operationContext = currentCoroutineContext()
        try {
            operationContext.ensureActive()
            val value = operation { operationContext.ensureActive() }
            operationContext.ensureActive()
            StorageResult.Success(value)
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (failure: StorageOperationException) {
            StorageResult.Failure(StorageError(failure.code, failure.message.orEmpty()))
        } catch (failure: DestinationExistsException) {
            StorageResult.Failure(
                StorageError(StorageErrorCode.NAME_EXISTS, failure.message.orEmpty())
            )
        } catch (failure: FileSizeLimitExceededException) {
            StorageResult.Failure(
                StorageError(StorageErrorCode.TOO_LARGE, failure.message.orEmpty())
            )
        } catch (failure: FileNotFoundException) {
            StorageResult.Failure(
                StorageError(StorageErrorCode.NOT_FOUND, failure.message.orEmpty())
            )
        } catch (failure: SecurityException) {
            StorageResult.Failure(
                StorageError(StorageErrorCode.PERMISSION_REVOKED, failure.message.orEmpty())
            )
        } catch (failure: CellularityDataException) {
            StorageResult.Failure(failure.toStorageError())
        } catch (failure: IllegalArgumentException) {
            StorageResult.Failure(
                StorageError(StorageErrorCode.BAD_NAME, failure.message.orEmpty())
            )
        } catch (failure: IOException) {
            StorageResult.Failure(
                StorageError(StorageErrorCode.IO_FAILURE, failure.message.orEmpty())
            )
        } catch (failure: Exception) {
            StorageResult.Failure(
                StorageError(StorageErrorCode.IO_FAILURE, failure.message.orEmpty())
            )
        }
    }
}

internal class StorageOperationException(
    val code: StorageErrorCode,
    message: String
) : IOException(message)

private fun CellularityDataException.toStorageError(): StorageError {
    val storageCode = when (code) {
        CellularityErrorCode.NEWER_PRODUCT_VERSION,
        CellularityErrorCode.NEWER_SNAPSHOT_VERSION -> StorageErrorCode.NEWER_VERSION

        CellularityErrorCode.UNSUPPORTED_FEATURE -> StorageErrorCode.UNSUPPORTED_FORMAT
        CellularityErrorCode.LIMIT_EXCEEDED,
        CellularityErrorCode.OUT_OF_MEMORY -> StorageErrorCode.TOO_LARGE

        CellularityErrorCode.IO_FAILURE -> StorageErrorCode.IO_FAILURE

        CellularityErrorCode.INVALID_HEADER,
        CellularityErrorCode.TRUNCATED_INPUT,
        CellularityErrorCode.INVALID_VERSION,
        CellularityErrorCode.INVALID_LENGTH,
        CellularityErrorCode.INVALID_COUNT,
        CellularityErrorCode.TRAILING_DATA,
        CellularityErrorCode.INVALID_VALUE,
        CellularityErrorCode.INVALID_COMPRESSION -> StorageErrorCode.CORRUPTED

        CellularityErrorCode.ENVIRONMENT_CHANGE_REJECTED,
        CellularityErrorCode.SESSION_NOT_READY,
        CellularityErrorCode.SESSION_CLOSED,
        CellularityErrorCode.INTERNAL_FAILURE -> StorageErrorCode.IO_FAILURE
    }
    return StorageError(storageCode, message.orEmpty())
}
