package com.cytarch.cellularity.ui.main_menu.screens.experiments

import android.content.Context
import android.content.Intent
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.NativeCellularityControllerBridge
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.files.AndroidStoredFileSharer
import com.cytarch.cellularity.platform.files.showFileCollectionNotice
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.plate
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.settings.ExperimentCollectionPreferences
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerAction
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerEffect
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerItem
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerOpenRequest
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerStore
import com.cytarch.cellularity.shared.ui.main_menu.freezer.FreezerScreen as SharedFreezerScreen
import com.cytarch.cellularity.ui.main_menu.screens.StoredFilePreview
import com.cytarch.cellularity.ui.play_menu.PlayActivity
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.painterResource

@Composable
fun FreezerScreen(
    externalRefreshRevision: Long = 0L,
    collectionPreferences: ExperimentCollectionPreferences,
    onCollectionPreferencesChange: (
        (ExperimentCollectionPreferences) -> ExperimentCollectionPreferences
    ) -> Unit,
    onStartPlayActivity: (Intent) -> Unit
) {
    val context = LocalContext.current
    val latestStartPlayActivity by rememberUpdatedState(onStartPlayActivity)
    val fileRepository = remember(context.applicationContext) {
        AndroidCellularityFileRepository(context.applicationContext)
    }
    val store = remember(fileRepository) { FreezerStore(fileRepository) }
    val fileSharer = remember(context, fileRepository) {
        AndroidStoredFileSharer(context, fileRepository)
    }
    val state by store.state.collectAsStateWithLifecycle()
    val coroutineScope = rememberCoroutineScope()

    LaunchedEffect(store, fileSharer) {
        store.effects.collect { effect ->
            when (effect) {
                is FreezerEffect.OpenPlate -> {
                    latestStartPlayActivity(effect.request.toPlayIntent(context))
                }
                is FreezerEffect.ShareSubstrate -> store.dispatch(
                    FreezerAction.ShareFinished(fileSharer.shareSubstrate(effect.id))
                )
                is FreezerEffect.ShowNotice -> showFileCollectionNotice(
                    context,
                    effect.notice
                )
            }
        }
    }

    LifecycleResumeEffect(store) {
        val refreshJob = coroutineScope.launch {
            store.dispatch(FreezerAction.ReturnedToForeground)
            store.dispatch(FreezerAction.Refresh)
        }
        onPauseOrDispose { refreshJob.cancel() }
    }

    LaunchedEffect(store, externalRefreshRevision) {
        if (externalRefreshRevision > 0L) {
            store.dispatch(FreezerAction.Refresh)
        }
    }

    SharedFreezerScreen(
        items = state.items,
        collectionPreferences = collectionPreferences,
        onCollectionPreferencesChange = onCollectionPreferencesChange,
        previewContent = { item, modifier, shape ->
            StoredSubstratePreview(item, fileRepository, modifier, shape)
        },
        estimateMemoryBytes = { cells, food, radius ->
            NativeCellularityControllerBridge.estimateWorldAllocationBytes(
                SimulationConfiguration(cells, food, radius)
            ).takeIf { it >= 0L }
        },
        onAction = { action ->
            coroutineScope.launch { store.dispatch(action) }
        }
    )
}

@Composable
private fun StoredSubstratePreview(
    item: FreezerItem.Stored,
    repository: AndroidCellularityFileRepository,
    modifier: Modifier,
    shape: Shape
) {
    StoredFilePreview(
        previewKey = item.id,
        previewAvailable = item.previewAvailable,
        modifier = modifier,
        clipShape = shape,
        placeholderContent = {
            Icon(
                painter = painterResource(Res.drawable.plate),
                contentDescription = null,
                tint = Color.Unspecified,
                modifier = Modifier
                    .align(Alignment.Center)
                    .size(48.dp)
                    .alpha(0.55f)
            )
        }
    ) {
        repository.readSubstratePreview(item.id)
    }
}

private fun FreezerOpenRequest.toPlayIntent(context: Context): Intent {
    return Intent(context, PlayActivity::class.java).apply {
        putExtra("old", storedSubstrateId != null)
        putExtra("name", storedSubstrateId?.value.orEmpty())
        advancedConfiguration?.let { configuration ->
            putExtra("maxCells", configuration.maximumCells)
            putExtra("maxFoods", configuration.maximumFood)
            putExtra("radius", configuration.radius)
            putExtra("innerRadius", configuration.innerRadius)
        }
    }
}
