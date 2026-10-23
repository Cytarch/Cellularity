package com.cytarch.cellularity.shared.ui.main_menu.settings

import com.cytarch.cellularity.shared.settings.CellularityLanguage
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.settings.CellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.CellularityThemeColors
import com.cytarch.cellularity.shared.settings.SettingsError
import com.cytarch.cellularity.shared.settings.SettingsErrorCode
import com.cytarch.cellularity.shared.settings.SettingsRecoveryState
import com.cytarch.cellularity.shared.settings.SettingsResult
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SettingsStoreTest {
    @Test
    fun reducerIsPureTypedAndNormalizesValues() {
        val preview = SettingsReducer.reduce(
            CellularitySettings.Default,
            SettingsAction.SetGenomePreviewMaxCells(Int.MAX_VALUE)
        )
        val incubate = SettingsReducer.reduce(
            preview,
            SettingsAction.SetIncubateTargetUps(Double.NaN)
        )
        val language = SettingsReducer.reduce(
            incubate,
            SettingsAction.SetLanguage(CellularityLanguage.POLISH)
        )
        val performanceHud = SettingsReducer.reduce(
            language,
            SettingsAction.SetPerformanceHudEnabled(true)
        )
        assertEquals(CellularitySettings.MaxGenomePreviewMaxCells, performanceHud.genomePreviewMaxCells)
        assertEquals(CellularitySettings.DefaultIncubateTargetUps, performanceHud.incubateTargetUps)
        assertEquals(CellularityLanguage.POLISH, performanceHud.language)
        assertTrue(performanceHud.performanceHudEnabled)
        assertEquals(CellularitySettings.Default, CellularitySettings.Default)
    }

    @Test
    fun committedActionIsPublishedByRepositoryState() = runBlocking {
        val repository = MemorySettingsRepository()
        val store = SettingsStore(repository)

        store.dispatch(SettingsAction.SetKeepOn(true))

        assertTrue(store.state.value.keepOn)
        assertEquals(1, repository.writeCount)
        assertFalse(store.state is MutableStateFlow<*>)
    }

    @Test
    fun committedThemeChangeEmitsOneNonReplayedRecreationEffect() = runBlocking {
        val store = SettingsStore(MemorySettingsRepository())

        store.dispatch(
            SettingsAction.SetTheme(
                colors = CellularityThemeColors(name = "New theme"),
                components = CellularitySettings.Default.themeComponents
            )
        )

        assertEquals(SettingsEffect.RecreateUi, store.effects.first())
        assertNull(withTimeoutOrNull(25) { store.effects.first() })
    }

    @Test
    fun failedWriteKeepsCommittedStateAndEmitsOneTypedEffect() = runBlocking {
        val repository = MemorySettingsRepository().apply { failWrites = true }
        val store = SettingsStore(repository)

        store.dispatch(
            SettingsAction.SetTheme(
                colors = CellularityThemeColors(name = "Lost theme"),
                components = CellularitySettings.Default.themeComponents
            )
        )

        val effect = assertIs<SettingsEffect.SaveFailed>(store.effects.first())
        assertEquals(SettingsErrorCode.IO_FAILURE, effect.error.code)
        assertEquals(CellularitySettings.Default, store.state.value)
        assertNull(withTimeoutOrNull(25) { store.effects.first() })
    }

    @Test
    fun queuedActionsReduceFromLatestCommittedSnapshot() = runBlocking {
        val store = SettingsStore(MemorySettingsRepository())

        coroutineScope {
            launch { store.dispatch(SettingsAction.SetKeepOn(true)) }
            launch { store.dispatch(SettingsAction.SetAskSterilize(true)) }
        }

        assertTrue(store.state.value.keepOn)
        assertTrue(store.state.value.askSterilize)
    }

    @Test
    fun noOpActionDoesNotWriteOrEmitEffect() = runBlocking {
        val repository = MemorySettingsRepository()
        val store = SettingsStore(repository)

        store.dispatch(SettingsAction.SetKeepOn(false))

        assertEquals(0, repository.writeCount)
        assertNull(withTimeoutOrNull(25) { store.effects.first() })
    }

    private class MemorySettingsRepository : CellularitySettingsRepository {
        private val mutableSettings = MutableStateFlow(CellularitySettings.Default)
        override val settings: StateFlow<CellularitySettings> = mutableSettings.asStateFlow()
        override val recovery: StateFlow<SettingsRecoveryState?> =
            MutableStateFlow<SettingsRecoveryState?>(null).asStateFlow()

        var failWrites = false
        var writeCount = 0

        override suspend fun initialize(): SettingsResult<CellularitySettings> =
            SettingsResult.Success(settings.value)

        override suspend fun writeSettings(
            settings: CellularitySettings
        ): SettingsResult<CellularitySettings> {
            writeCount++
            if (failWrites) {
                return SettingsResult.Failure(
                    SettingsError(SettingsErrorCode.IO_FAILURE, "test failure")
                )
            }
            val normalized = settings.normalized()
            mutableSettings.value = normalized
            return SettingsResult.Success(normalized)
        }

        override suspend fun resetSettings(): SettingsResult<CellularitySettings> {
            mutableSettings.value = CellularitySettings.Default
            return SettingsResult.Success(mutableSettings.value)
        }
    }
}
