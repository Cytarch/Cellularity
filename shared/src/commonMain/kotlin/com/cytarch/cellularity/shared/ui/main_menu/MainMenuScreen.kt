package com.cytarch.cellularity.shared.ui.main_menu

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.material3.Scaffold
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.sp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.main_menu_tab_about
import com.cytarch.cellularity.shared.generated.resources.main_menu_tab_experiments
import com.cytarch.cellularity.shared.generated.resources.main_menu_tab_gene_bank
import com.cytarch.cellularity.shared.generated.resources.main_menu_tab_settings
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.stringResource

@Composable
fun MainMenuScreen(
    state: MainMenuState,
    onAction: (MainMenuAction) -> Unit,
    avoidCameraCutout: Boolean = false,
    modifier: Modifier = Modifier,
    pageContent: @Composable (page: MainMenuPage, collectionRevision: Long) -> Unit
) {
    val tabTitles = listOf(
        stringResource(Res.string.main_menu_tab_experiments),
        stringResource(Res.string.main_menu_tab_gene_bank),
        stringResource(Res.string.main_menu_tab_settings),
        stringResource(Res.string.main_menu_tab_about)
    )
    val pagerState = rememberPagerState(
        initialPage = state.page.index,
        pageCount = { tabTitles.size }
    )
    val coroutineScope = rememberCoroutineScope()

    LaunchedEffect(state.page) {
        val requestedPage = state.page.index.coerceIn(0, tabTitles.lastIndex)
        if (pagerState.currentPage != requestedPage) {
            pagerState.scrollToPage(requestedPage)
        }
    }

    LaunchedEffect(pagerState) {
        snapshotFlow { pagerState.currentPage }
            .distinctUntilChanged()
            .collect { pageIndex ->
                onAction(MainMenuAction.PageSelected(MainMenuPage.fromIndex(pageIndex)))
            }
    }

    Box(modifier = modifier.fillMaxSize()) {
        Scaffold(
            modifier = Modifier
                .fillMaxSize()
                .cameraCutoutPadding(avoidCameraCutout),
            bottomBar = {
                MainMenuBottomBar(
                    tabTitles = tabTitles,
                    selectedTab = pagerState.currentPage,
                    onTabSelected = { index ->
                        coroutineScope.launch {
                            pagerState.scrollToPage(index)
                        }
                    }
                )
            }
        ) { paddingValues ->
            HorizontalPager(
                state = pagerState,
                modifier = Modifier
                    .fillMaxSize()
                    .background(MaterialTheme.colorScheme.background),
                beyondViewportPageCount = (tabTitles.size - 1).coerceAtLeast(0)
            ) { page ->
                val menuPage = MainMenuPage.fromIndex(page)
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(bottom = paddingValues.calculateBottomPadding())
                ) {
                    pageContent(menuPage, state.collectionRevision(menuPage))
                }
            }
        }

        if (state.importInProgress) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.35f)),
                contentAlignment = Alignment.Center
            ) {
                CircularProgressIndicator()
            }
        }
    }
}

private fun Modifier.cameraCutoutPadding(enabled: Boolean): Modifier =
    if (enabled) padding(top = CellularityDimensions.cutoutTopPadding) else this

@Composable
private fun MainMenuBottomBar(
    tabTitles: List<String>,
    selectedTab: Int,
    onTabSelected: (Int) -> Unit
) {
    Surface(
        color = MaterialTheme.colorScheme.primaryContainer,
        contentColor = MaterialTheme.colorScheme.primary
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(CellularityDimensions.toolbarHeight)
                .padding(horizontal = CellularityDimensions.medium, vertical = CellularityDimensions.small + CellularityDimensions.hairline),
            horizontalArrangement = Arrangement.spacedBy(CellularityDimensions.small),
            verticalAlignment = Alignment.CenterVertically
        ) {
            tabTitles.forEachIndexed { index, title ->
                val isSelected = selectedTab == index
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .clip(RoundedCornerShape(CellularityDimensions.large))
                        .background(
                            if (isSelected) {
                                MaterialTheme.colorScheme.background
                            } else {
                                Color.Transparent
                            }
                        )
                        .clickable { onTabSelected(index) }
                        .padding(horizontal = CellularityDimensions.small),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = title,
                        color = if (isSelected) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.background
                        },
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = if (isSelected) FontWeight.Bold else FontWeight.SemiBold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        autoSize = TextAutoSize.StepBased(
                            minFontSize = 10.sp,
                            maxFontSize = MaterialTheme.typography.labelLarge.fontSize,
                            stepSize = 0.5.sp
                        )
                    )
                }
            }
        }
    }
}
