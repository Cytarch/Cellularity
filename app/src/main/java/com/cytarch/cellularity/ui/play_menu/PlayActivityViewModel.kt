package com.cytarch.cellularity.ui.play_menu

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.simulation.AndroidSimulationFactory
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.simulation.SimulationGateway
import com.cytarch.cellularity.shared.simulation.SimulationSessionState
import com.cytarch.cellularity.shared.ui.play.PlayAction
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayPersistenceState
import com.cytarch.cellularity.shared.ui.play.PlayRestoredUiState
import com.cytarch.cellularity.shared.ui.play.PlayStore
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeStore
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch

/** Retained composition owner for one Play store and one native session controller. */
class PlayActivityViewModel : ViewModel() {
    private val microscopeStore = MicroscopeStore()
    private var playStore: PlayStore? = null
    private var sessionController: PlaySessionController? = null
    private var contentObserver: Job? = null
    private var sessionObserver: Job? = null

    @Synchronized
    fun initialize(
        context: Context,
        fileRepository: AndroidCellularityFileRepository,
        source: PlayLaunchSource,
        restoredUi: PlayRestoredUiState
    ): PlayStore {
        playStore?.let { return it }

        val controller = PlaySessionController(
            ownerScope = viewModelScope,
            context = context,
            fileRepository = fileRepository,
            connectionFactory = { factoryContext, files, configuration, target ->
                AndroidSimulationFactory.create(
                    context = factoryContext,
                    fileRepository = files,
                    initialConfiguration = configuration,
                    initialTarget = target
                )
            }
        )
        val store = PlayStore(
            source = source,
            operations = controller,
            restoredUi = restoredUi
        )
        sessionController = controller
        playStore = store

        contentObserver = viewModelScope.launch {
            controller.contentStates.collect { content ->
                store.dispatch(PlayAction.ContentStateObserved(content))
            }
        }
        sessionObserver = viewModelScope.launch {
            controller.sessionStates.collect { state ->
                if (state is SimulationSessionState.Failed) {
                    store.dispatch(PlayAction.SessionFailed(state.error))
                }
            }
        }
        viewModelScope.launch { store.dispatch(PlayAction.Start) }
        return store
    }

    fun dispatch(action: PlayAction) {
        val store = requireNotNull(playStore) { "Play store has not been initialized" }
        viewModelScope.launch { store.dispatch(action) }
    }

    val microscopeState = microscopeStore.state

    fun dispatchMicroscope(action: MicroscopeAction) {
        microscopeStore.dispatch(action)
    }

    fun persistenceState(): PlayPersistenceState? = playStore?.state?.value?.persistenceState()

    fun requireSimulationGateway(): SimulationGateway =
        requireNotNull(sessionController) { "Play session has not been initialized" }
            .requireGateway()

    fun requireMicroscopeSurfaceFactory(
        backendKind: RenderBackendKind,
        onRuntimeFailure: (String) -> Unit
    ) =
        requireNotNull(sessionController) { "Play session has not been initialized" }
            .requireMicroscopeSurfaceFactory()
            .withBackend(backendKind, onRuntimeFailure)

    fun selectedGenome(): GenomeDraft? = sessionController?.selectedGenome()

    override fun onCleared() {
        contentObserver?.cancel()
        sessionObserver?.cancel()
        sessionController?.close()
        super.onCleared()
    }
}
