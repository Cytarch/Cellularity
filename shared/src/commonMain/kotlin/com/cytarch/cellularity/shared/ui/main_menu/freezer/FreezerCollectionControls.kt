package com.cytarch.cellularity.shared.ui.main_menu.freezer

import androidx.compose.runtime.Composable
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.freezer_search_placeholder
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_age
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_cell_count
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_created
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_dish_size
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_food_count
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_food_mass
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_maximum_cell_count
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_maximum_food_count
import com.cytarch.cellularity.shared.generated.resources.freezer_sort_name
import com.cytarch.cellularity.shared.settings.CollectionLayoutMode
import com.cytarch.cellularity.shared.settings.CollectionSortDirection
import com.cytarch.cellularity.shared.settings.ExperimentSortField
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionToolbar
import com.cytarch.cellularity.shared.ui.main_menu.collection.compareCollectionNames
import com.cytarch.cellularity.shared.ui.main_menu.collection.compareOptionalCollectionValues
import com.cytarch.cellularity.shared.ui.main_menu.collection.withCollectionSortDirection
import org.jetbrains.compose.resources.stringResource

internal typealias FreezerSortField = ExperimentSortField
internal typealias FreezerSortDirection = CollectionSortDirection
internal typealias FreezerLayoutMode = CollectionLayoutMode

@Composable
internal fun FreezerCollectionToolbar(
    searchQuery: String,
    sortField: FreezerSortField,
    sortDirection: FreezerSortDirection,
    layoutMode: FreezerLayoutMode,
    onSearchQueryChange: (String) -> Unit,
    onSortFieldChange: (FreezerSortField) -> Unit,
    onSortDirectionChange: (FreezerSortDirection) -> Unit,
    onLayoutModeChange: (FreezerLayoutMode) -> Unit
) {
    FileCollectionToolbar(
        searchQuery = searchQuery,
        searchPlaceholder = stringResource(Res.string.freezer_search_placeholder),
        sortFields = FreezerSortField.entries,
        sortField = sortField,
        sortFieldLabel = ::freezerSortFieldLabel,
        sortDirection = sortDirection,
        layoutMode = layoutMode,
        onSearchQueryChange = onSearchQueryChange,
        onSortFieldChange = onSortFieldChange,
        onSortDirectionChange = onSortDirectionChange,
        onLayoutModeChange = onLayoutModeChange
    )
}

@Composable
private fun freezerSortFieldLabel(sortField: FreezerSortField): String = when (sortField) {
    FreezerSortField.NAME -> stringResource(Res.string.freezer_sort_name)
    FreezerSortField.CREATED -> stringResource(Res.string.freezer_sort_created)
    FreezerSortField.AGE -> stringResource(Res.string.freezer_sort_age)
    FreezerSortField.CELL_COUNT -> stringResource(Res.string.freezer_sort_cell_count)
    FreezerSortField.MAXIMUM_CELL_COUNT ->
        stringResource(Res.string.freezer_sort_maximum_cell_count)
    FreezerSortField.FOOD_COUNT -> stringResource(Res.string.freezer_sort_food_count)
    FreezerSortField.MAXIMUM_FOOD_COUNT ->
        stringResource(Res.string.freezer_sort_maximum_food_count)
    FreezerSortField.FOOD_MASS -> stringResource(Res.string.freezer_sort_food_mass)
    FreezerSortField.DISH_SIZE -> stringResource(Res.string.freezer_sort_dish_size)
}

internal fun defaultDirectionFor(field: FreezerSortField): FreezerSortDirection =
    if (field == FreezerSortField.NAME) {
        FreezerSortDirection.ASCENDING
    } else {
        FreezerSortDirection.DESCENDING
    }

internal fun filterAndSortFreezerItems(
    items: List<FreezerItem>,
    searchQuery: String,
    sortField: FreezerSortField,
    sortDirection: FreezerSortDirection
): List<FreezerItem> {
    val query = searchQuery.trim()
    val storedItems = items
        .filterIsInstance<FreezerItem.Stored>()
        .filter { query.isEmpty() || it.displayName.contains(query, ignoreCase = true) }
        .sortedWith { left, right ->
            compareStoredItems(left, right, sortField, sortDirection)
        }

    return buildList {
        if (FreezerItem.NewPlate in items) add(FreezerItem.NewPlate)
        addAll(storedItems)
    }
}

private fun compareStoredItems(
    left: FreezerItem.Stored,
    right: FreezerItem.Stored,
    sortField: FreezerSortField,
    sortDirection: FreezerSortDirection
): Int {
    val leftDetails = left.readableDetails()
    val rightDetails = right.readableDetails()
    val comparison = when (sortField) {
        FreezerSortField.NAME -> compareCollectionNames(
            left.displayName,
            right.displayName
        ).withCollectionSortDirection(sortDirection)
        FreezerSortField.CREATED -> compareOptionalCollectionValues(
            leftDetails?.createdAtUnixMillis,
            rightDetails?.createdAtUnixMillis,
            sortDirection
        )
        FreezerSortField.AGE -> compareOptionalCollectionValues(
            leftDetails?.unfrozenAgeHours,
            rightDetails?.unfrozenAgeHours,
            sortDirection
        )
        FreezerSortField.CELL_COUNT -> compareOptionalCollectionValues(
            leftDetails?.cellCount,
            rightDetails?.cellCount,
            sortDirection
        )
        FreezerSortField.MAXIMUM_CELL_COUNT -> compareOptionalCollectionValues(
            leftDetails?.maximumCellCount,
            rightDetails?.maximumCellCount,
            sortDirection
        )
        FreezerSortField.FOOD_COUNT -> compareOptionalCollectionValues(
            leftDetails?.foodCount,
            rightDetails?.foodCount,
            sortDirection
        )
        FreezerSortField.MAXIMUM_FOOD_COUNT -> compareOptionalCollectionValues(
            leftDetails?.maximumFoodCount,
            rightDetails?.maximumFoodCount,
            sortDirection
        )
        FreezerSortField.FOOD_MASS -> compareOptionalCollectionValues(
            leftDetails?.foodMass,
            rightDetails?.foodMass,
            sortDirection
        )
        FreezerSortField.DISH_SIZE -> compareOptionalCollectionValues(
            leftDetails?.dishDiameterMillimeters,
            rightDetails?.dishDiameterMillimeters,
            sortDirection
        )
    }
    return if (comparison != 0) {
        comparison
    } else {
        compareCollectionNames(left.displayName, right.displayName)
    }
}

private fun FreezerItem.Stored.readableDetails(): FreezerItemDetails.Readable? =
    details as? FreezerItemDetails.Readable
