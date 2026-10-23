package com.cytarch.cellularity.ui.main_menu

import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuEffect
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportOutcome
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportedFile
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportedFileKind
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuStore
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.joinAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class MainMenuImportOrchestratorTest {
    @Test
    fun duplicateCommandIsRejectedWhileActiveAndAfterCompletion() = runBlocking {
        var executions = 0
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        val store = MainMenuStore()
        val orchestrator = MainMenuImportOrchestrator(
            store = store,
            executor = MainMenuImportExecutor {
                executions++
                entered.complete(Unit)
                release.await()
                success("genome")
            }
        )
        val command = command("same")

        val first = async { orchestrator.submit(command) }
        entered.await()
        assertFalse(orchestrator.submit(command))
        release.complete(Unit)
        assertTrue(first.await())
        assertFalse(orchestrator.submit(command))

        assertEquals(1, executions)
        assertEquals(listOf("same"), orchestrator.completedCommandIds())
        assertFalse(store.state.value.importInProgress)
    }

    @Test
    fun savedCompletionMarkersRemainBoundedToTheLatestCommands() {
        val commandIds = (0 until 40).map { "command-$it" }
        val orchestrator = MainMenuImportOrchestrator(
            store = MainMenuStore(),
            executor = MainMenuImportExecutor { success("unused") },
            restoredCompletedCommandIds = commandIds
        )

        assertEquals(commandIds.takeLast(32), orchestrator.completedCommandIds())
    }

    @Test
    fun restoredCompletedMarkerSuppressesReprocessing() = runBlocking {
        var executions = 0
        val orchestrator = MainMenuImportOrchestrator(
            store = MainMenuStore(),
            executor = MainMenuImportExecutor {
                executions++
                success("unused")
            },
            restoredCompletedCommandIds = listOf("restored")
        )

        assertFalse(orchestrator.submit(command("restored")))

        assertEquals(0, executions)
        assertEquals(listOf("restored"), orchestrator.completedCommandIds())
    }

    @Test
    fun differentCommandsAreSerialized() = runBlocking {
        val firstEntered = CompletableDeferred<Unit>()
        val releaseFirst = CompletableDeferred<Unit>()
        val secondEntered = CompletableDeferred<Unit>()
        val store = MainMenuStore()
        val orchestrator = MainMenuImportOrchestrator(
            store = store,
            executor = MainMenuImportExecutor { request ->
                if (request.commandId == "first") {
                    firstEntered.complete(Unit)
                    releaseFirst.await()
                } else {
                    secondEntered.complete(Unit)
                }
                success(request.commandId)
            }
        )

        val first = launch { orchestrator.submit(command("first")) }
        firstEntered.await()
        val second = launch { orchestrator.submit(command("second")) }

        assertNull(withTimeoutOrNull(25) { secondEntered.await() })
        releaseFirst.complete(Unit)
        joinAll(first, second)

        assertTrue(secondEntered.isCompleted)
        assertEquals(listOf("first", "second"), orchestrator.completedCommandIds())
    }

    @Test
    fun cancellationClearsProgressAndAllowsAnExplicitRetry() {
        var executions = 0
        val store = MainMenuStore()
        val orchestrator = MainMenuImportOrchestrator(
            store = store,
            executor = MainMenuImportExecutor {
                executions++
                if (executions == 1) throw CancellationException("test")
                success("retried")
            }
        )

        assertThrows(CancellationException::class.java) {
            runBlocking { orchestrator.submit(command("retry")) }
        }
        assertFalse(store.state.value.importInProgress)

        runBlocking { assertTrue(orchestrator.submit(command("retry"))) }

        assertEquals(2, executions)
        assertEquals(listOf("retry"), orchestrator.completedCommandIds())
    }

    @Test
    fun unexpectedProviderFailureBecomesTypedFailureAndDoesNotLeaveProgressStuck() =
        runBlocking {
            val store = MainMenuStore()
            val orchestrator = MainMenuImportOrchestrator(
                store = store,
                executor = MainMenuImportExecutor {
                    throw IOException("provider disappeared")
                }
            )

            assertTrue(orchestrator.submit(command("failure")))

            assertFalse(store.state.value.importInProgress)
            assertEquals(listOf("failure"), orchestrator.completedCommandIds())
            val effect = store.effects.first() as MainMenuEffect.ImportFailed
            assertEquals(StorageErrorCode.IO_FAILURE, effect.error.code)
            assertEquals("provider disappeared", effect.error.detail)
        }

    private fun command(id: String) = MainMenuImportCommand(
        commandId = id,
        contentUri = "content://test/$id"
    )

    private fun success(name: String) = MainMenuImportOutcome.Success(
        MainMenuImportedFile(
            displayName = name,
            kind = MainMenuImportedFileKind.GENOME
        )
    )
}
