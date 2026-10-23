package com.cytarch.cellularity.ui.main_menu.screens.gene_bank

import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.files.AndroidStoredFileSharer
import com.cytarch.cellularity.platform.files.showFileCollectionNotice
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.dna_icon
import com.cytarch.cellularity.shared.settings.GeneBankCollectionPreferences
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankAction
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankEffect
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankItem
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankStore
import com.cytarch.cellularity.ui.main_menu.screens.StoredFilePreview
import com.cytarch.cellularity.shared.ui.main_menu.gene_bank.GeneBankScreen as SharedGeneBankScreen
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.painterResource

@Composable
fun GeneBankScreen(
    externalRefreshRevision: Long = 0L,
    collectionPreferences: GeneBankCollectionPreferences,
    onCollectionPreferencesChange: (
        (GeneBankCollectionPreferences) -> GeneBankCollectionPreferences
    ) -> Unit
) {
    val context = LocalContext.current
    val fileRepository = remember(context.applicationContext) {
        AndroidCellularityFileRepository(context.applicationContext)
    }
    val store = remember(fileRepository) { GeneBankStore(fileRepository) }
    val fileSharer = remember(context, fileRepository) {
        AndroidStoredFileSharer(context, fileRepository)
    }
    val state by store.state.collectAsStateWithLifecycle()
    val coroutineScope = rememberCoroutineScope()

    LaunchedEffect(store, fileSharer) {
        store.effects.collect { effect ->
            when (effect) {
                is GeneBankEffect.ShareGenome -> store.dispatch(
                    GeneBankAction.ShareFinished(fileSharer.shareGenome(effect.id))
                )
                is GeneBankEffect.ShowNotice -> showFileCollectionNotice(
                    context,
                    effect.notice
                )
            }
        }
    }

    LifecycleResumeEffect(store) {
        val refreshJob = coroutineScope.launch {
            store.dispatch(GeneBankAction.Refresh)
        }
        onPauseOrDispose { refreshJob.cancel() }
    }

    LaunchedEffect(store, externalRefreshRevision) {
        if (externalRefreshRevision > 0L) {
            store.dispatch(GeneBankAction.Refresh)
        }
    }

    SharedGeneBankScreen(
        items = state.items,
        collectionPreferences = collectionPreferences,
        onCollectionPreferencesChange = onCollectionPreferencesChange,
        previewContent = { item, modifier, shape ->
            StoredGenomePreview(item, fileRepository, modifier, shape)
        },
        onAction = { action ->
            coroutineScope.launch { store.dispatch(action) }
        }
    )
}

@Composable
private fun StoredGenomePreview(
    item: GeneBankItem,
    repository: AndroidCellularityFileRepository,
    modifier: Modifier,
    shape: Shape
) {
    StoredFilePreview(
        previewKey = item.id,
        previewAvailable = item.previewAvailable,
        modifier = modifier,
        clipShape = shape,
        ignoreEmbeddedAlpha = true,
        placeholderContent = {
            Icon(
                painter = painterResource(Res.drawable.dna_icon),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier
                    .align(Alignment.Center)
                    .size(52.dp)
                    .alpha(0.72f)
            )
        }
    ) {
        repository.readGenomePreview(item.id)
    }
}
