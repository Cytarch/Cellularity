/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.files.StoredFileShareOutcome
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuPage
import com.cytarch.cellularity.shared.ui.main_menu.about.AboutScreen
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerAction
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerEffect
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerScreen
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerOpenRequest
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerStore
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankAction
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankEffect
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankScreen
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankStore
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch

/**
 * The four main-menu pages.
 *
 * The screens and their stores already exist in the shared module; what is desktop
 * specific is only where the stores get their repositories from and what an "open this
 * plate" effect turns into here. The stores are kept rather than re-derived because they
 * own the repository reads and the effect channel, and a store rebuilt on recomposition
 * would lose both.
 */
@Composable
fun DesktopMainMenuPages(
    page: MainMenuPage,
    collectionRevision: Long,
    settings: CellularitySettings,
    environment: DesktopEnvironment,
    scope: CoroutineScope,
    notices: DesktopNoticeQueue,
    onOpenPlate: (FreezerOpenRequest) -> Unit
) {
    when (page) {
        MainMenuPage.EXPERIMENTS -> DesktopFreezerPage(
            collectionRevision = collectionRevision,
            settings = settings,
            environment = environment,
            scope = scope,
            notices = notices,
            onOpenPlate = onOpenPlate
        )

        MainMenuPage.GENE_BANK -> DesktopGeneBankPage(
            collectionRevision = collectionRevision,
            settings = settings,
            environment = environment,
            scope = scope,
            notices = notices
        )

        MainMenuPage.SETTINGS -> DesktopSettingsPage(
            environment = environment,
            scope = scope,
            notices = notices
        )

        MainMenuPage.ABOUT -> Box(modifier = Modifier.fillMaxSize()) {
            AboutScreen(versionName = DESKTOP_VERSION_NAME)
        }
    }
}

@Composable
private fun DesktopFreezerPage(
    collectionRevision: Long,
    settings: CellularitySettings,
    environment: DesktopEnvironment,
    scope: CoroutineScope,
    notices: DesktopNoticeQueue,
    onOpenPlate: (FreezerOpenRequest) -> Unit
) {
    val store = remember(environment) { FreezerStore(environment.fileRepository) }
    val state by store.state.collectAsState()

    LaunchedEffect(store) {
        store.effects.collect { effect ->
            when (effect) {
                // "Open this plate" is where the two halves meet: the freezer knows
                // which substrate and with what allocation, and the launcher knows
                // where the simulation is shown.
                is FreezerEffect.OpenPlate -> onOpenPlate(effect.request)

                // Sharing is the platform share sheet, which a desktop process has no
                // equivalent of. The request is completed as "nothing can handle it"
                // rather than left pending, because a pending share disables the item's
                // buttons until it finishes.
                is FreezerEffect.ShareSubstrate -> store.dispatch(
                    FreezerAction.ShareFinished(StoredFileShareOutcome.NoCompatibleTarget)
                )

                // Shown rather than dropped. A rename, a delete or a refused import that
                // says nothing is indistinguishable from a button that does not work.
                is FreezerEffect.ShowNotice -> notices.post(DesktopFileNotice(effect.notice))
            }
        }
    }

    // The collection is re-read when something is imported and when the page is first
    // shown; without this the list would be whatever it was at launch.
    LaunchedEffect(store, collectionRevision) {
        store.dispatch(FreezerAction.Refresh)
    }

    FreezerScreen(
        items = state.items,
        onAction = { action -> scope.launch { store.dispatch(action) } },
        // The same native policy the phone asks, reached through the desktop bridge. It
        // needs no session -- it is a question about a shape, not about a running world --
        // which is what lets the freezer decide whether a plate can be started before
        // anything has been started. A null answer disables the start button rather than
        // guessing, and that is the right refusal: a world the runtime would reject should
        // not look startable.
        estimateMemoryBytes = { cells, food, radius ->
            DesktopNativeRenderer.estimateWorldAllocationBytes(cells, food, radius)
        },
        collectionPreferences = settings.experimentCollection,
        onCollectionPreferencesChange = { update ->
            scope.launch {
                environment.settingsRepository.writeSettings(
                    settings.copy(
                        experimentCollection = update(settings.experimentCollection)
                    )
                )
            }
        }
    )
}

@Composable
private fun DesktopGeneBankPage(
    collectionRevision: Long,
    settings: CellularitySettings,
    environment: DesktopEnvironment,
    scope: CoroutineScope,
    notices: DesktopNoticeQueue
) {
    val store = remember(environment) { GeneBankStore(environment.fileRepository) }
    val state by store.state.collectAsState()

    LaunchedEffect(store) {
        store.effects.collect { effect ->
            when (effect) {
                is GeneBankEffect.ShareGenome -> store.dispatch(
                    GeneBankAction.ShareFinished(StoredFileShareOutcome.NoCompatibleTarget)
                )

                is GeneBankEffect.ShowNotice -> notices.post(DesktopFileNotice(effect.notice))
            }
        }
    }

    LaunchedEffect(store, collectionRevision) {
        store.dispatch(GeneBankAction.Refresh)
    }

    GeneBankScreen(
        items = state.items,
        onAction = { action -> scope.launch { store.dispatch(action) } },
        collectionPreferences = settings.geneBankCollection,
        onCollectionPreferencesChange = { update ->
            scope.launch {
                environment.settingsRepository.writeSettings(
                    settings.copy(geneBankCollection = update(settings.geneBankCollection))
                )
            }
        }
    )
}

@Composable
private fun DesktopSettingsPage(
    environment: DesktopEnvironment,
    scope: CoroutineScope,
    notices: DesktopNoticeQueue
) {
    // The shared store already owns the read-modify-write of the document, so the
    // launcher does not restate those rules; it only says where the document lives.
    val store = remember(environment) {
        SettingsStore(environment.settingsRepository)
    }
    val settings by store.state.collectAsState()

    // The platform's own window is the parent, so the picker belongs to the application
    // rather than floating free of it.
    val window = java.awt.KeyboardFocusManager.getCurrentKeyboardFocusManager()
        .activeWindow as? java.awt.Frame

    SettingsScreen(
        state = settings,
        onAction = { action -> scope.launch { store.dispatch(action) } },
        // Crash reports are a phone feature: this build has no crash reporter to list
        // from, and the screen treats an empty list as "say nothing" rather than as "show
        // a button that does nothing".
        crashReports = emptyList(),
        onExportDiagnostics = { },
        onExportTheme = {
            val message = DesktopThemeFiles.export(
                parent = window,
                colors = settings.theme,
                components = settings.themeComponents
            )
            if (message != null) {
                notices.post(DesktopThemeNotice(message))
            }
        },
        onImportTheme = {
            val imported = DesktopThemeFiles.import(window)
            val colors = imported.colors
            val components = imported.components
            if (colors != null && components != null) {
                // Applied through the store, so an imported theme is written to settings by
                // the same code that writes an edited one. There is no second path to the
                // file, and therefore no way for the two to disagree.
                scope.launch {
                    store.dispatch(SettingsAction.SetTheme(colors, components))
                }
            }
            notices.post(DesktopThemeNotice(imported.message))
        }
    )
}

private const val DESKTOP_VERSION_NAME = "0.1.0-alpha.1"
