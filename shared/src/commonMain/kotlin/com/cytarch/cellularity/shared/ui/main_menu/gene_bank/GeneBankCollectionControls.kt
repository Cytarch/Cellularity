package com.cytarch.cellularity.shared.ui.main_menu.gene_bank

import androidx.compose.runtime.Composable
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.gene_bank_search_placeholder
import com.cytarch.cellularity.shared.generated.resources.gene_bank_sort_cell_type_count
import com.cytarch.cellularity.shared.generated.resources.gene_bank_sort_gene_count
import com.cytarch.cellularity.shared.generated.resources.gene_bank_sort_initial_gene
import com.cytarch.cellularity.shared.generated.resources.gene_bank_sort_name
import com.cytarch.cellularity.shared.generated.resources.gene_bank_sort_unique_gene_count
import com.cytarch.cellularity.shared.settings.CollectionLayoutMode
import com.cytarch.cellularity.shared.settings.CollectionSortDirection
import com.cytarch.cellularity.shared.settings.GeneBankSortField
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionToolbar
import com.cytarch.cellularity.shared.ui.main_menu.collection.compareCollectionNames
import com.cytarch.cellularity.shared.ui.main_menu.collection.compareOptionalCollectionValues
import com.cytarch.cellularity.shared.ui.main_menu.collection.withCollectionSortDirection
import org.jetbrains.compose.resources.stringResource

internal typealias GeneBankSortDirection = CollectionSortDirection
internal typealias GeneBankLayoutMode = CollectionLayoutMode

@Composable
internal fun GeneBankCollectionToolbar(
    searchQuery: String,
    sortField: GeneBankSortField,
    sortDirection: GeneBankSortDirection,
    layoutMode: GeneBankLayoutMode,
    onSearchQueryChange: (String) -> Unit,
    onSortFieldChange: (GeneBankSortField) -> Unit,
    onSortDirectionChange: (GeneBankSortDirection) -> Unit,
    onLayoutModeChange: (GeneBankLayoutMode) -> Unit
) {
    FileCollectionToolbar(
        searchQuery = searchQuery,
        searchPlaceholder = stringResource(Res.string.gene_bank_search_placeholder),
        sortFields = GeneBankSortField.entries,
        sortField = sortField,
        sortFieldLabel = ::geneBankSortFieldLabel,
        sortDirection = sortDirection,
        layoutMode = layoutMode,
        onSearchQueryChange = onSearchQueryChange,
        onSortFieldChange = onSortFieldChange,
        onSortDirectionChange = onSortDirectionChange,
        onLayoutModeChange = onLayoutModeChange
    )
}

@Composable
private fun geneBankSortFieldLabel(sortField: GeneBankSortField): String = when (sortField) {
    GeneBankSortField.NAME -> stringResource(Res.string.gene_bank_sort_name)
    GeneBankSortField.GENE_COUNT -> stringResource(Res.string.gene_bank_sort_gene_count)
    GeneBankSortField.UNIQUE_GENE_COUNT ->
        stringResource(Res.string.gene_bank_sort_unique_gene_count)
    GeneBankSortField.INITIAL_GENE -> stringResource(Res.string.gene_bank_sort_initial_gene)
    GeneBankSortField.CELL_TYPE_COUNT ->
        stringResource(Res.string.gene_bank_sort_cell_type_count)
}

internal fun defaultGeneBankDirectionFor(
    field: GeneBankSortField
): GeneBankSortDirection = if (field == GeneBankSortField.NAME) {
    GeneBankSortDirection.ASCENDING
} else {
    GeneBankSortDirection.DESCENDING
}

internal fun filterAndSortGeneBankItems(
    items: List<GeneBankItem>,
    searchQuery: String,
    sortField: GeneBankSortField,
    sortDirection: GeneBankSortDirection
): List<GeneBankItem> {
    val query = searchQuery.trim()
    return items
        .filter { query.isEmpty() || it.displayName.contains(query, ignoreCase = true) }
        .sortedWith { left, right ->
            val comparison = when (sortField) {
                GeneBankSortField.NAME -> compareCollectionNames(
                    left.displayName,
                    right.displayName
                ).withCollectionSortDirection(sortDirection)
                GeneBankSortField.GENE_COUNT -> compareOptionalCollectionValues(
                    left.geneCount,
                    right.geneCount,
                    sortDirection
                )
                GeneBankSortField.UNIQUE_GENE_COUNT -> compareOptionalCollectionValues(
                    left.uniqueGeneCount,
                    right.uniqueGeneCount,
                    sortDirection
                )
                GeneBankSortField.INITIAL_GENE -> compareOptionalCollectionValues(
                    left.initialGeneIndex,
                    right.initialGeneIndex,
                    sortDirection
                )
                GeneBankSortField.CELL_TYPE_COUNT -> compareOptionalCollectionValues(
                    left.cellTypeCount,
                    right.cellTypeCount,
                    sortDirection
                )
            }
            if (comparison != 0) {
                comparison
            } else {
                compareCollectionNames(left.displayName, right.displayName)
            }
        }
}
