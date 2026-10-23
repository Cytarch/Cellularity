package com.cytarch.cellularity.shared.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.cytarch.cellularity.shared.settings.CellularityThemeComponents
import com.cytarch.cellularity.shared.settings.CellularityThemeColors
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuAction
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuPage
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuScreen
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuState
import com.cytarch.cellularity.shared.ui.theme.CellularityTheme

@Composable
fun CellularityApp(
    mainMenuState: MainMenuState,
    onMainMenuAction: (MainMenuAction) -> Unit,
    avoidCameraCutout: Boolean = false,
    themeColors: CellularityThemeColors = CellularityThemeColors.Default,
    themeComponents: CellularityThemeComponents = CellularityThemeComponents.Default,
    modifier: Modifier = Modifier,
    mainMenuPageContent: @Composable (
        page: MainMenuPage,
        collectionRevision: Long
    ) -> Unit
) {
    CellularityTheme(themeColors = themeColors, themeComponents = themeComponents) {
        MainMenuScreen(
            state = mainMenuState,
            onAction = onMainMenuAction,
            avoidCameraCutout = avoidCameraCutout,
            modifier = modifier,
            pageContent = mainMenuPageContent
        )
    }
}
