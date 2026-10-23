package com.cytarch.cellularity.platform.settings

import com.cytarch.cellularity.platform.files.AtomicFileStore
import com.cytarch.cellularity.platform.files.DurableFileOperations
import com.cytarch.cellularity.shared.settings.AndroidDisplaySettings
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendPreference
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendSettings
import com.cytarch.cellularity.shared.settings.CollectionLayoutMode
import com.cytarch.cellularity.shared.settings.CollectionSortDirection
import com.cytarch.cellularity.shared.settings.CellularityLanguage
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.CellularitySettingsJson
import com.cytarch.cellularity.shared.settings.ExperimentCollectionPreferences
import com.cytarch.cellularity.shared.settings.ExperimentSortField
import com.cytarch.cellularity.shared.settings.GeneBankCollectionPreferences
import com.cytarch.cellularity.shared.settings.GeneBankSortField
import com.cytarch.cellularity.shared.settings.SettingsErrorCode
import com.cytarch.cellularity.shared.settings.SettingsResult
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.concurrent.Executors
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class AndroidSettingsRepositoryTest {
    @Test
    fun ioRunsOnConfiguredDispatcherAndCommittedStateSurvivesANewOwner() = withDirectory { directory ->
        val executor = Executors.newSingleThreadExecutor { task ->
            Thread(task, "cellularity-settings-test")
        }
        val dispatcher = executor.asCoroutineDispatcher()
        val operations = JvmSettingsFileOperations()
        val settingsFile = File(directory, "settings.json")
        try {
            val repository = AndroidCellularitySettingsRepository(
                settingsFile,
                AtomicFileStore(operations),
                dispatcher
            )
            assertTrue(runBlocking { repository.initialize() } is SettingsResult.Success)

            val expected = CellularitySettings(
                genomePreviewMaxCells = 144,
                performanceHudEnabled = true,
                language = CellularityLanguage.POLISH,
                keepOn = true,
                experimentCollection = ExperimentCollectionPreferences(
                    sortField = ExperimentSortField.CELL_COUNT,
                    sortDirection = CollectionSortDirection.DESCENDING,
                    layoutMode = CollectionLayoutMode.LIST
                ),
                geneBankCollection = GeneBankCollectionPreferences(
                    sortField = GeneBankSortField.GENE_COUNT,
                    sortDirection = CollectionSortDirection.DESCENDING,
                    layoutMode = CollectionLayoutMode.LIST
                )
            )
            assertEquals(
                expected,
                (runBlocking { repository.writeSettings(expected) } as SettingsResult.Success).value
            )
            assertEquals(expected, repository.settings.value)
            assertTrue(operations.threadNames.all { it.startsWith("cellularity-settings-test") })
            assertEquals(
                listOf("sync-file", "replace", "sync-directory"),
                operations.calls.takeLast(3)
            )

            val reopened = AndroidCellularitySettingsRepository(
                settingsFile,
                AtomicFileStore(JvmSettingsFileOperations()),
                dispatcher
            )
            assertTrue(runBlocking { reopened.initialize() } is SettingsResult.Success)
            assertEquals(expected, reopened.settings.value)
        } finally {
            dispatcher.close()
        }
    }

    @Test
    fun interruptedWriteKeepsBothCommittedFileAndPublishedSnapshot() = withDirectory { directory ->
        val operations = JvmSettingsFileOperations()
        val settingsFile = File(directory, "settings.json")
        val repository = AndroidCellularitySettingsRepository(
            settingsFile,
            AtomicFileStore(operations)
        )
        runBlocking { repository.initialize() }
        val beforeBytes = settingsFile.readBytes()
        val beforeState = repository.settings.value
        operations.failNextFileSync = true

        val result = runBlocking {
            repository.writeSettings(beforeState.copy(genomePreviewMaxCells = 200))
        }

        assertEquals(
            SettingsErrorCode.IO_FAILURE,
            (result as SettingsResult.Failure).error.code
        )
        assertTrue(beforeBytes.contentEquals(settingsFile.readBytes()))
        assertEquals(beforeState, repository.settings.value)
        assertTrue(directory.listFiles().orEmpty().none { it.name.startsWith(".cellularity-transaction-") })
    }

    @Test
    fun cancellationPropagatesAndCannotPublishOrReplaceSettings() = withDirectory { directory ->
        val operations = JvmSettingsFileOperations()
        val settingsFile = File(directory, "settings.json")
        val repository = AndroidCellularitySettingsRepository(
            settingsFile,
            AtomicFileStore(operations)
        )
        runBlocking { repository.initialize() }
        val beforeBytes = settingsFile.readBytes()
        val beforeState = repository.settings.value
        operations.cancelNextFileSync = true

        assertThrows(CancellationException::class.java) {
            runBlocking {
                repository.writeSettings(beforeState.copy(askSterilize = true))
            }
        }

        assertTrue(beforeBytes.contentEquals(settingsFile.readBytes()))
        assertEquals(beforeState, repository.settings.value)
    }

    @Test
    fun malformedCurrentDocumentIsPreservedBeforeDefaultsAreCommitted() = withDirectory { directory ->
        val settingsFile = File(directory, "settings.json").apply {
            writeText("{broken pre-release settings")
        }
        val repository = AndroidCellularitySettingsRepository(
            settingsFile,
            AtomicFileStore(JvmSettingsFileOperations())
        )

        val result = runBlocking { repository.initialize() }

        assertEquals(
            SettingsErrorCode.MALFORMED,
            (result as SettingsResult.Failure).error.code
        )
        assertEquals(CellularitySettings.Default, repository.settings.value)
        assertTrue(repository.recovery.value?.recoverableCopyAvailable == true)
        assertEquals(
            "{broken pre-release settings",
            File(directory, "settings.json.recovery").readText()
        )
        assertEquals(
            CellularitySettings.Default,
            (CellularitySettingsJson.decode(settingsFile.readText()) as SettingsResult.Success).value
        )
    }

    @Test
    fun failedRecoveryNeverOverwritesOriginalWithoutExplicitReset() = withDirectory { directory ->
        val original = "{broken but valuable"
        val settingsFile = File(directory, "settings.json").apply { writeText(original) }
        val operations = JvmSettingsFileOperations(failRecoveryRename = true)
        val repository = AndroidCellularitySettingsRepository(
            settingsFile,
            AtomicFileStore(operations)
        )

        assertTrue(runBlocking { repository.initialize() } is SettingsResult.Failure)
        assertEquals(original, settingsFile.readText())
        assertFalse(repository.recovery.value?.recoverableCopyAvailable ?: true)

        val normalWrite = runBlocking {
            repository.writeSettings(CellularitySettings.Default.copy(keepOn = true))
        }
        assertTrue(normalWrite is SettingsResult.Failure)
        assertEquals(original, settingsFile.readText())

        val reset = runBlocking { repository.resetSettings() }
        assertTrue(reset is SettingsResult.Success)
        assertEquals(CellularitySettings.Default, repository.settings.value)
        assertTrue(CellularitySettingsJson.decode(settingsFile.readText()) is SettingsResult.Success)
    }

    @Test
    fun androidOnlySettingsUseTheSameVersionedAtomicStoreContract() = withDirectory { directory ->
        val settingsFile = File(directory, "android-display-settings.json")
        val repository = AndroidDisplaySettingsRepository(
            settingsFile,
            AtomicFileStore(JvmSettingsFileOperations())
        )
        runBlocking { repository.initialize() }

        val expected = AndroidDisplaySettings(avoidCameraCutout = true)
        val result = runBlocking { repository.writeSettings(expected) }

        assertEquals(expected, (result as SettingsResult.Success).value)
        assertEquals(expected, repository.settings.value)
        assertTrue(settingsFile.readText().contains("\"schemaVersion\": 1"))
    }

    @Test
    fun renderBackendPreferenceUsesAnIndependentDurableDocument() = withDirectory { directory ->
        val renderFile = File(directory, "android-render-backend-settings.json")
        val displayFile = File(directory, "android-display-settings.json")
        val repository = AndroidRenderBackendSettingsRepository(
            renderFile,
            AtomicFileStore(JvmSettingsFileOperations())
        )
        runBlocking { repository.initialize() }

        val expected = AndroidRenderBackendSettings(AndroidRenderBackendPreference.AUTO)
        val result = runBlocking { repository.writeSettings(expected) }

        assertEquals(expected, (result as SettingsResult.Success).value)
        assertTrue(renderFile.readText().contains("\"backend\": \"auto\""))
        assertFalse(displayFile.exists())

        val reopened = AndroidRenderBackendSettingsRepository(
            renderFile,
            AtomicFileStore(JvmSettingsFileOperations())
        )
        assertTrue(runBlocking { reopened.initialize() } is SettingsResult.Success)
        assertEquals(expected, reopened.settings.value)
    }

    private fun withDirectory(block: (File) -> Unit) {
        val directory = Files.createTempDirectory("cellularity-settings-test-").toFile()
        try {
            block(directory)
        } finally {
            assertTrue(directory.deleteRecursively())
        }
    }
}

private class JvmSettingsFileOperations(
    private val failRecoveryRename: Boolean = false
) : DurableFileOperations {
    val threadNames = mutableListOf<String>()
    val calls = mutableListOf<String>()
    var failNextFileSync = false
    var cancelNextFileSync = false

    override fun syncFile(file: File) {
        threadNames += Thread.currentThread().name
        calls += "sync-file"
        if (cancelNextFileSync) {
            cancelNextFileSync = false
            throw CancellationException("cancelled settings commit")
        }
        if (failNextFileSync) {
            failNextFileSync = false
            throw IOException("interrupted durable write")
        }
        FileOutputStream(file, true).use { it.fd.sync() }
    }

    override fun replace(source: File, target: File) {
        threadNames += Thread.currentThread().name
        calls += "replace"
        if (failRecoveryRename && source.name == "settings.json") {
            throw IOException("recovery destination unavailable")
        }
        Files.move(
            source.toPath(),
            target.toPath(),
            StandardCopyOption.REPLACE_EXISTING,
            StandardCopyOption.ATOMIC_MOVE
        )
    }

    override fun syncDirectory(directory: File) {
        threadNames += Thread.currentThread().name
        calls += "sync-directory"
        check(directory.isDirectory)
    }
}
