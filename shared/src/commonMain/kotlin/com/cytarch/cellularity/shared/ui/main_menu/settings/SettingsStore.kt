package com.cytarch.cellularity.shared.ui.main_menu.settings

import com.cytarch.cellularity.shared.settings.CellularityLanguage
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.CellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.CellularityThemeColors
import com.cytarch.cellularity.shared.settings.CellularityThemeComponents
import com.cytarch.cellularity.shared.settings.SettingsError
import com.cytarch.cellularity.shared.settings.SettingsResult
import com.cytarch.cellularity.shared.ui.state.Store
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

sealed interface SettingsAction {
    data class SetGenomePreviewMaxCells(val value: Int) : SettingsAction
    data class SetIncubateTargetUps(val value: Double) : SettingsAction
    data class SetPerformanceHudEnabled(val value: Boolean) : SettingsAction
    data class SetLanguage(val value: CellularityLanguage) : SettingsAction
    data class SetKeepOn(val value: Boolean) : SettingsAction
    data class SetAskSterilize(val value: Boolean) : SettingsAction
    data class SetTheme(
        val colors: CellularityThemeColors,
        val components: CellularityThemeComponents
    ) : SettingsAction
}

sealed interface SettingsEffect {
    data class SaveFailed(val error: SettingsError) : SettingsEffect
    data object RecreateUi : SettingsEffect
}

/** Pure settings transformations; persistence and effect delivery belong to [SettingsStore]. */
internal object SettingsReducer {
    fun reduce(current: CellularitySettings, action: SettingsAction): CellularitySettings {
        return when (action) {
            is SettingsAction.SetGenomePreviewMaxCells -> current.copy(
                genomePreviewMaxCells = action.value
            )
            is SettingsAction.SetIncubateTargetUps -> current.copy(
                incubateTargetUps = action.value
            )
            is SettingsAction.SetPerformanceHudEnabled -> current.copy(
                performanceHudEnabled = action.value
            )
            is SettingsAction.SetLanguage -> current.copy(language = action.value)
            is SettingsAction.SetKeepOn -> current.copy(keepOn = action.value)
            is SettingsAction.SetAskSterilize -> current.copy(askSterilize = action.value)
            is SettingsAction.SetTheme -> current.copy(
                theme = action.colors,
                themeComponents = action.components
            )
        }.normalized()
    }

    fun requiresUiRecreation(
        previous: CellularitySettings,
        committed: CellularitySettings
    ): Boolean {
        return committed.theme != previous.theme ||
            committed.themeComponents != previous.themeComponents
    }
}

class SettingsStore(
    private val repository: CellularitySettingsRepository
) : Store<CellularitySettings, SettingsAction, SettingsEffect> {
    private val dispatchMutex = Mutex()
    private val effectChannel = Channel<SettingsEffect>(Channel.UNLIMITED)

    override val state: StateFlow<CellularitySettings> = repository.settings
    override val effects: Flow<SettingsEffect> = effectChannel.receiveAsFlow()

    override suspend fun dispatch(action: SettingsAction) {
        dispatchMutex.withLock {
            val previous = state.value
            val next = SettingsReducer.reduce(previous, action)
            if (next == previous) return@withLock

            when (val result = repository.writeSettings(next)) {
                is SettingsResult.Success -> if (
                    SettingsReducer.requiresUiRecreation(previous, result.value)
                ) {
                    effectChannel.send(SettingsEffect.RecreateUi)
                }
                is SettingsResult.Failure -> {
                    effectChannel.send(SettingsEffect.SaveFailed(result.error))
                }
            }
        }
    }
}
