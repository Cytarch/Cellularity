package com.cytarch.cellularity.shared.ui.main_menu.gene_bank

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.dna_icon
import com.cytarch.cellularity.shared.generated.resources.gene_bank_card_cell_types_label
import com.cytarch.cellularity.shared.generated.resources.gene_bank_card_genes_label
import com.cytarch.cellularity.shared.generated.resources.gene_bank_card_initial_gene_label
import com.cytarch.cellularity.shared.generated.resources.gene_bank_card_unique_genes_label
import com.cytarch.cellularity.shared.generated.resources.gene_bank_delete_dialog_message
import com.cytarch.cellularity.shared.generated.resources.gene_bank_delete_dialog_title
import com.cytarch.cellularity.shared.generated.resources.gene_bank_empty_state
import com.cytarch.cellularity.shared.generated.resources.gene_bank_newer_version_short_status
import com.cytarch.cellularity.shared.generated.resources.gene_bank_no_search_results
import com.cytarch.cellularity.shared.generated.resources.gene_bank_read_error_message
import com.cytarch.cellularity.shared.generated.resources.gene_bank_rename_genome_dialog_title
import com.cytarch.cellularity.shared.settings.GeneBankCollectionPreferences
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionCard
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionDeleteDialog
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionGrid
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionMetricBlock
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionNoResults
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionPreviewPlaceholder
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionRenameDialog
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionStatus
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionStoredActions
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import com.cytarch.cellularity.shared.ui.text.formatLocalizedNumber
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.compose.resources.stringResource

@Composable
fun GeneBankScreen(
    items: List<GeneBankItem>,
    onAction: (GeneBankAction) -> Unit,
    collectionPreferences: GeneBankCollectionPreferences,
    onCollectionPreferencesChange: (
        (GeneBankCollectionPreferences) -> GeneBankCollectionPreferences
    ) -> Unit,
    previewContent: @Composable (GeneBankItem, Modifier, Shape) -> Unit =
        { _, previewModifier, previewShape ->
            DefaultGenomePreviewPlaceholder(previewModifier, previewShape)
        },
    modifier: Modifier = Modifier
) {
    var itemToRename by remember { mutableStateOf<GeneBankItem?>(null) }
    var itemToDelete by remember { mutableStateOf<GeneBankItem?>(null) }
    // Deliberately session-only, independently from the Experiments search query.
    var searchQuery by rememberSaveable { mutableStateOf("") }
    val sortField = collectionPreferences.sortField
    val sortDirection = collectionPreferences.sortDirection
    val layoutMode = collectionPreferences.layoutMode
    val visibleItems = remember(items, searchQuery, sortField, sortDirection) {
        filterAndSortGeneBankItems(items, searchQuery, sortField, sortDirection)
    }

    Column(modifier = modifier.fillMaxSize()) {
        GeneBankCollectionToolbar(
            searchQuery = searchQuery,
            sortField = sortField,
            sortDirection = sortDirection,
            layoutMode = layoutMode,
            onSearchQueryChange = { searchQuery = it },
            onSortFieldChange = { field ->
                onCollectionPreferencesChange { preferences ->
                    preferences.copy(
                        sortField = field,
                        sortDirection = defaultGeneBankDirectionFor(field)
                    )
                }
            },
            onSortDirectionChange = { direction ->
                onCollectionPreferencesChange { preferences ->
                    preferences.copy(sortDirection = direction)
                }
            },
            onLayoutModeChange = { mode ->
                onCollectionPreferencesChange { preferences ->
                    preferences.copy(layoutMode = mode)
                }
            }
        )

        FileCollectionGrid(
            items = visibleItems,
            layoutMode = layoutMode,
            key = { item -> "gene-bank:${item.id.fileName}" },
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
            fullWidthContent = if (visibleItems.isEmpty()) {
                {
                    FileCollectionNoResults(
                        message = stringResource(
                            if (searchQuery.isBlank()) {
                                Res.string.gene_bank_empty_state
                            } else {
                                Res.string.gene_bank_no_search_results
                            }
                        ),
                        onClearSearch = if (searchQuery.isBlank()) {
                            null
                        } else ({ searchQuery = "" })
                    )
                }
            } else {
                null
            }
        ) { item, itemModifier ->
            GenomeCard(
                item = item,
                layoutMode = layoutMode,
                modifier = itemModifier,
                onDelete = { itemToDelete = item },
                onRename = { itemToRename = item },
                onShare = { onAction(GeneBankAction.ShareItem(item.id)) },
                previewContent = previewContent
            )
        }
    }

    itemToRename?.let { item ->
        FileCollectionRenameDialog(
            title = stringResource(Res.string.gene_bank_rename_genome_dialog_title),
            initialName = item.displayName,
            onDismiss = { itemToRename = null },
            onConfirm = { newName ->
                onAction(GeneBankAction.RenameItem(item.id, newName))
                itemToRename = null
            }
        )
    }

    itemToDelete?.let { item ->
        FileCollectionDeleteDialog(
            title = stringResource(Res.string.gene_bank_delete_dialog_title),
            message = stringResource(
                Res.string.gene_bank_delete_dialog_message,
                item.displayName
            ),
            onDismiss = { itemToDelete = null },
            onConfirm = {
                onAction(GeneBankAction.DeleteItem(item.id))
                itemToDelete = null
            }
        )
    }
}

@Composable
private fun GenomeCard(
    item: GeneBankItem,
    layoutMode: GeneBankLayoutMode,
    onDelete: () -> Unit,
    onRename: () -> Unit,
    onShare: () -> Unit,
    previewContent: @Composable (GeneBankItem, Modifier, Shape) -> Unit,
    modifier: Modifier = Modifier
) {
    FileCollectionCard(
        stateKey = item.id,
        title = item.displayName,
        layoutMode = layoutMode,
        modifier = modifier,
        previewContent = { previewModifier, previewShape ->
            previewContent(item, previewModifier, previewShape)
        },
        menuContent = { dismiss ->
            FileCollectionStoredActions(
                dismiss = dismiss,
                onShare = onShare,
                onRename = onRename,
                onDelete = onDelete
            )
        },
        detailsContent = { detailsModifier ->
            when (item.details) {
                GeneBankItemDetails.Readable -> ReadableGenomeSummary(
                    item = item,
                    modifier = detailsModifier
                )
                is GeneBankItemDetails.NewerVersion -> FileCollectionStatus(
                    text = stringResource(Res.string.gene_bank_newer_version_short_status),
                    isError = true,
                    modifier = detailsModifier
                )
                is GeneBankItemDetails.Unreadable -> FileCollectionStatus(
                    text = stringResource(Res.string.gene_bank_read_error_message),
                    isError = true,
                    modifier = detailsModifier
                )
            }
        }
    )
}

@Composable
private fun ReadableGenomeSummary(
    item: GeneBankItem,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.SpaceEvenly
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(dimensions.large)
        ) {
            FileCollectionMetricBlock(
                label = stringResource(Res.string.gene_bank_card_genes_label),
                value = formatOptionalCount(item.geneCount),
                modifier = Modifier.weight(1f)
            )
            FileCollectionMetricBlock(
                label = stringResource(Res.string.gene_bank_card_unique_genes_label),
                value = formatOptionalCount(item.uniqueGeneCount),
                modifier = Modifier.weight(1f)
            )
        }
        Spacer(Modifier.height(dimensions.xSmall))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(dimensions.large)
        ) {
            FileCollectionMetricBlock(
                label = stringResource(Res.string.gene_bank_card_initial_gene_label),
                value = formatOptionalCount(item.initialGeneIndex?.plus(1)),
                modifier = Modifier.weight(1f)
            )
            FileCollectionMetricBlock(
                label = stringResource(Res.string.gene_bank_card_cell_types_label),
                value = formatOptionalCount(item.cellTypeCount),
                modifier = Modifier.weight(1f)
            )
        }
    }
}

@Composable
private fun DefaultGenomePreviewPlaceholder(modifier: Modifier, shape: Shape) {
    FileCollectionPreviewPlaceholder(
        painter = painterResource(Res.drawable.dna_icon),
        modifier = modifier,
        shape = shape,
        iconSize = 52.dp,
        iconAlpha = 0.72f,
        tint = MaterialTheme.colorScheme.primary
    )
}

private fun formatOptionalCount(value: Int?): String = value?.let { count ->
    formatLocalizedNumber(
        value = count.toDouble(),
        minimumFractionDigits = 0,
        maximumFractionDigits = 0,
        useGrouping = true
    )
} ?: UNKNOWN_VALUE

private const val UNKNOWN_VALUE = "—"
