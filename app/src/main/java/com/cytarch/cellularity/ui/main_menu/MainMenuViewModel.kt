package com.cytarch.cellularity.ui.main_menu

import android.content.Context
import android.content.Intent
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuAction
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuEffect
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportOutcome
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuState
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuStore
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

internal class MainMenuImportOrchestrator(
    private val store: MainMenuStore,
    private val executor: MainMenuImportExecutor,
    restoredCompletedCommandIds: Collection<String> = emptyList()
) {
    private val importMutex = Mutex()
    private val trackerLock = Any()
    private val activeCommandIds = mutableSetOf<String>()
    private val completedCommandIds = linkedSetOf<String>()

    init {
        restoreCompletedCommandIds(restoredCompletedCommandIds)
    }

    suspend fun submit(command: MainMenuImportCommand): Boolean {
        if (!accept(command.commandId)) return false

        try {
            importMutex.withLock {
                store.dispatch(MainMenuAction.ImportStarted(command.commandId))
                val outcome = try {
                    executor.execute(command)
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (failure: Exception) {
                    MainMenuImportOutcome.Failure(
                        StorageError(
                            code = StorageErrorCode.IO_FAILURE,
                            detail = failure.message ?: failure.javaClass.simpleName
                        )
                    )
                }
                store.dispatch(
                    MainMenuAction.ImportFinished(
                        commandId = command.commandId,
                        outcome = outcome
                    )
                )
                complete(command.commandId)
            }
        } catch (cancelled: CancellationException) {
            withContext(NonCancellable) {
                store.dispatch(MainMenuAction.ImportCancelled(command.commandId))
            }
            release(command.commandId)
            throw cancelled
        }
        return true
    }

    fun restoreCompletedCommandIds(commandIds: Collection<String>) {
        synchronized(trackerLock) {
            commandIds
                .filter(String::isNotBlank)
                .takeLast(MAX_SAVED_COMMAND_IDS)
                .forEach { commandId ->
                    completedCommandIds += commandId
                }
            trimCompletedIds()
        }
    }

    fun completedCommandIds(): List<String> = synchronized(trackerLock) {
        completedCommandIds.toList()
    }

    private fun accept(commandId: String): Boolean = synchronized(trackerLock) {
        if (commandId in activeCommandIds || commandId in completedCommandIds) {
            false
        } else {
            activeCommandIds.add(commandId)
        }
    }

    private fun complete(commandId: String) {
        synchronized(trackerLock) {
            activeCommandIds -= commandId
            completedCommandIds += commandId
            trimCompletedIds()
        }
    }

    private fun release(commandId: String) {
        synchronized(trackerLock) {
            activeCommandIds -= commandId
        }
    }

    private fun trimCompletedIds() {
        while (completedCommandIds.size > MAX_SAVED_COMMAND_IDS) {
            completedCommandIds.remove(completedCommandIds.first())
        }
    }

    private companion object {
        const val MAX_SAVED_COMMAND_IDS = 32
    }
}

internal class MainMenuViewModel(
    private val importCoordinator: AndroidMainMenuImportCoordinator,
    private val store: MainMenuStore = MainMenuStore()
) : ViewModel() {
    private val importOrchestrator = MainMenuImportOrchestrator(
        store = store,
        executor = importCoordinator
    )

    val state: StateFlow<MainMenuState> = store.state
    val effects: Flow<MainMenuEffect> = store.effects

    fun restoreCompletedCommandIds(commandIds: Collection<String>) {
        importOrchestrator.restoreCompletedCommandIds(commandIds)
    }

    fun acceptExternalIntent(intent: Intent) {
        val command = importCoordinator.commandFrom(intent) ?: return
        viewModelScope.launch {
            importOrchestrator.submit(command)
        }
    }

    fun dispatch(action: MainMenuAction) {
        viewModelScope.launch {
            store.dispatch(action)
        }
    }

    fun completedCommandIds(): List<String> = importOrchestrator.completedCommandIds()

    class Factory(context: Context) : ViewModelProvider.Factory {
        private val applicationContext = context.applicationContext

        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            require(modelClass.isAssignableFrom(MainMenuViewModel::class.java)) {
                "Unsupported ViewModel type: ${modelClass.name}"
            }
            @Suppress("UNCHECKED_CAST")
            return MainMenuViewModel(
                AndroidMainMenuImportCoordinator(applicationContext)
            ) as T
        }
    }
}
