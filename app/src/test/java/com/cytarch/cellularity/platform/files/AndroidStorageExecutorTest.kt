package com.cytarch.cellularity.platform.files

import com.cytarch.cellularity.CellularityDataException
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import java.io.FileNotFoundException
import java.io.IOException
import java.util.concurrent.Executors
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class AndroidStorageExecutorTest {
    @Test
    fun operationRunsOnTheConfiguredStorageDispatcher() {
        val executor = Executors.newSingleThreadExecutor { task ->
            Thread(task, "cellularity-storage-test")
        }
        val dispatcher = executor.asCoroutineDispatcher()
        try {
            val result = runBlocking {
                AndroidStorageExecutor(dispatcher).execute { Thread.currentThread().name }
            }

            assertTrue(
                (result as StorageResult.Success).value
                    .startsWith("cellularity-storage-test")
            )
        } finally {
            dispatcher.close()
        }
    }

    @Test
    fun expectedFailuresAreMappedToThePublicTypedContract() {
        val cases = listOf(
            FileNotFoundException("missing") to StorageErrorCode.NOT_FOUND,
            DestinationExistsException() to StorageErrorCode.NAME_EXISTS,
            IllegalArgumentException("name") to StorageErrorCode.BAD_NAME,
            FileSizeLimitExceededException() to StorageErrorCode.TOO_LARGE,
            SecurityException("grant") to StorageErrorCode.PERMISSION_REVOKED,
            IOException("disk") to StorageErrorCode.IO_FAILURE,
            CellularityDataException(
                CellularityErrorCode.NEWER_PRODUCT_VERSION,
                "newer"
            ) to StorageErrorCode.NEWER_VERSION,
            CellularityDataException(
                CellularityErrorCode.UNSUPPORTED_FEATURE,
                "unsupported"
            ) to StorageErrorCode.UNSUPPORTED_FORMAT,
            CellularityDataException(
                CellularityErrorCode.INVALID_HEADER,
                "broken"
            ) to StorageErrorCode.CORRUPTED
        )

        cases.forEach { (failure, expectedCode) ->
            val result = runBlocking {
                AndroidStorageExecutor().execute<Unit> { throw failure }
            }
            assertEquals(
                expectedCode,
                (result as StorageResult.Failure).error.code
            )
        }
    }

    @Test
    fun cancellationIsPropagatedInsteadOfBecomingAStorageFailure() {
        assertThrows(CancellationException::class.java) {
            runBlocking {
                AndroidStorageExecutor().execute<Unit> {
                    throw CancellationException("cancelled")
                }
            }
        }
    }
}
