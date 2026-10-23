package com.cytarch.cellularity.shared.ui.main_menu.freezer

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.DateRange
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.freezer_dish_shape_label
import com.cytarch.cellularity.shared.generated.resources.freezer_dish_shape_circle
import com.cytarch.cellularity.shared.generated.resources.freezer_dish_shape_ring
import com.cytarch.cellularity.shared.generated.resources.freezer_inner_diameter_label
import com.cytarch.cellularity.shared.generated.resources.freezer_ring_dimensions_hint
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledRadioButton
import com.cytarch.cellularity.shared.generated.resources.freezer_advanced_settings_maximum_cell_count_label
import com.cytarch.cellularity.shared.generated.resources.freezer_advanced_settings_warning
import com.cytarch.cellularity.shared.generated.resources.freezer_advanced_settings_petri_dish_diameter_label
import com.cytarch.cellularity.shared.generated.resources.freezer_advanced_settings_maximum_nutrient_count_label
import com.cytarch.cellularity.shared.generated.resources.common_cancel_button_label
import com.cytarch.cellularity.shared.generated.resources.common_start_action
import com.cytarch.cellularity.shared.generated.resources.freezer_advanced_settings_title
import com.cytarch.cellularity.shared.generated.resources.freezer_card_age_content_description
import com.cytarch.cellularity.shared.generated.resources.freezer_card_cells_label
import com.cytarch.cellularity.shared.generated.resources.freezer_card_created_content_description
import com.cytarch.cellularity.shared.generated.resources.freezer_card_dish_content_description
import com.cytarch.cellularity.shared.generated.resources.freezer_card_dish_diameter_format
import com.cytarch.cellularity.shared.generated.resources.freezer_card_dish_label
import com.cytarch.cellularity.shared.generated.resources.freezer_card_food_label
import com.cytarch.cellularity.shared.generated.resources.freezer_card_mass_label
import com.cytarch.cellularity.shared.generated.resources.freezer_delete_dialog_message
import com.cytarch.cellularity.shared.generated.resources.freezer_delete_dialog_title
import com.cytarch.cellularity.shared.generated.resources.freezer_memory_estimate_format
import com.cytarch.cellularity.shared.generated.resources.freezer_new_plate_action
import com.cytarch.cellularity.shared.generated.resources.freezer_new_plate_summary
import com.cytarch.cellularity.shared.generated.resources.freezer_newer_version_short_status
import com.cytarch.cellularity.shared.generated.resources.freezer_no_search_results
import com.cytarch.cellularity.shared.generated.resources.freezer_read_error_message
import com.cytarch.cellularity.shared.generated.resources.freezer_relabel_sample_action
import com.cytarch.cellularity.shared.generated.resources.plate
import com.cytarch.cellularity.shared.generated.resources.simulation_hour_abbreviation
import com.cytarch.cellularity.shared.settings.ExperimentCollectionPreferences
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionCard
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionDeleteDialog
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionGrid
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionIconMetric
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionMetricBlock
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionNoResults
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionPreviewPlaceholder
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionRenameDialog
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionStatus
import com.cytarch.cellularity.shared.ui.main_menu.collection.FileCollectionStoredActions
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import com.cytarch.cellularity.shared.ui.theme.cellularityOutlinedTextFieldColors
import com.cytarch.cellularity.shared.ui.text.formatLocalizedDate
import com.cytarch.cellularity.shared.ui.text.formatLocalizedNumber
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.compose.resources.stringResource

@Composable
fun FreezerScreen(
    items: List<FreezerItem>,
    onAction: (FreezerAction) -> Unit,
    estimateMemoryBytes: (Int, Int, Double) -> Long?,
    collectionPreferences: ExperimentCollectionPreferences,
    onCollectionPreferencesChange: (
        (ExperimentCollectionPreferences) -> ExperimentCollectionPreferences
    ) -> Unit,
    previewContent: @Composable (FreezerItem.Stored, Modifier, Shape) -> Unit =
        { _, previewModifier, previewShape ->
            DefaultSubstratePreviewPlaceholder(previewModifier, previewShape)
        },
    modifier: Modifier = Modifier
) {
    var itemToRename by remember { mutableStateOf<FreezerItem.Stored?>(null) }
    var itemToDelete by remember { mutableStateOf<FreezerItem.Stored?>(null) }
    var showAdvancedDialog by remember { mutableStateOf(false) }
    // Search is screen-session state only; unlike collection preferences, it is never persisted.
    var searchQuery by rememberSaveable { mutableStateOf("") }
    val sortField = collectionPreferences.sortField
    val sortDirection = collectionPreferences.sortDirection
    val layoutMode = collectionPreferences.layoutMode
    val visibleItems = remember(items, searchQuery, sortField, sortDirection) {
        filterAndSortFreezerItems(items, searchQuery, sortField, sortDirection)
    }
    val noSearchResults = searchQuery.isNotBlank() &&
        visibleItems.none { it is FreezerItem.Stored }

    Column(modifier = modifier.fillMaxSize()) {
        FreezerCollectionToolbar(
            searchQuery = searchQuery,
            sortField = sortField,
            sortDirection = sortDirection,
            layoutMode = layoutMode,
            onSearchQueryChange = { searchQuery = it },
            onSortFieldChange = { field ->
                onCollectionPreferencesChange { preferences ->
                    preferences.copy(
                        sortField = field,
                        sortDirection = defaultDirectionFor(field)
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
            key = { item ->
                when (item) {
                    FreezerItem.NewPlate -> "freezer:new"
                    is FreezerItem.Stored -> "freezer:stored:${item.id.value}"
                }
            },
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
            fullWidthContent = if (noSearchResults) {
                {
                    FileCollectionNoResults(
                        message = stringResource(Res.string.freezer_no_search_results),
                        onClearSearch = { searchQuery = "" }
                    )
                }
            } else {
                null
            }
        ) { item, itemModifier ->
            SubstrateCard(
                item = item,
                modifier = itemModifier,
                onClick = {
                    onAction(
                        when (item) {
                            FreezerItem.NewPlate -> FreezerAction.OpenNewPlate
                            is FreezerItem.Stored -> FreezerAction.OpenStored(item.id)
                        }
                    )
                },
                onDelete = {
                    if (item is FreezerItem.Stored) itemToDelete = item
                },
                onRename = {
                    if (item is FreezerItem.Stored) itemToRename = item
                },
                onShare = {
                    if (item is FreezerItem.Stored) {
                        onAction(FreezerAction.ShareItem(item.id))
                    }
                },
                onAdvanced = { showAdvancedDialog = true },
                layoutMode = layoutMode,
                previewContent = previewContent
            )
        }
    }

    val renameItem = itemToRename
    if (renameItem != null) {
        FileCollectionRenameDialog(
            title = stringResource(Res.string.freezer_relabel_sample_action),
            initialName = renameItem.displayName,
            onDismiss = { itemToRename = null },
            onConfirm = { newName ->
                onAction(FreezerAction.RenameItem(renameItem.id, newName))
                itemToRename = null
            }
        )
    }

    val deleteItem = itemToDelete
    if (deleteItem != null) {
        FileCollectionDeleteDialog(
            title = stringResource(Res.string.freezer_delete_dialog_title),
            message = stringResource(
                Res.string.freezer_delete_dialog_message,
                deleteItem.displayName
            ),
            onDismiss = { itemToDelete = null },
            onConfirm = {
                onAction(FreezerAction.DeleteItem(deleteItem.id))
                itemToDelete = null
            }
        )
    }

    if (showAdvancedDialog) {
        AdvancedSettingsDialog(
            estimateMemoryBytes = estimateMemoryBytes,
            onDismiss = { showAdvancedDialog = false },
            onStart = { configuration ->
                onAction(FreezerAction.StartAdvanced(
                    configuration.maxCellCount,
                    configuration.maxFoodCount,
                    configuration.radius,
                    configuration.innerRadius
                ))
                showAdvancedDialog = false
            }
        )
    }
}

@Composable
private fun SubstrateCard(
    item: FreezerItem,
    onClick: () -> Unit,
    onDelete: () -> Unit,
    onRename: () -> Unit,
    onShare: () -> Unit,
    onAdvanced: () -> Unit,
    layoutMode: FreezerLayoutMode,
    previewContent: @Composable (FreezerItem.Stored, Modifier, Shape) -> Unit,
    modifier: Modifier = Modifier
) {
    val title = when (item) {
        FreezerItem.NewPlate -> stringResource(Res.string.freezer_new_plate_action)
        is FreezerItem.Stored -> item.displayName
    }
    FileCollectionCard(
        stateKey = item,
        title = title,
        layoutMode = layoutMode,
        modifier = modifier,
        onClick = onClick,
        previewContent = { previewModifier, previewShape ->
            when (item) {
                FreezerItem.NewPlate -> NewPlatePreview(
                    modifier = previewModifier,
                    shape = previewShape
                )
                is FreezerItem.Stored -> previewContent(
                    item,
                    previewModifier,
                    previewShape
                )
            }
        },
        menuContent = { dismiss ->
            when (item) {
                FreezerItem.NewPlate -> DropdownMenuItem(
                    text = {
                        Text(
                            stringResource(Res.string.freezer_advanced_settings_title),
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    },
                    leadingIcon = {
                        Icon(
                            Icons.Default.Settings,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                    },
                    onClick = {
                        dismiss()
                        onAdvanced()
                    }
                )
                is FreezerItem.Stored -> FileCollectionStoredActions(
                    dismiss = dismiss,
                    onShare = onShare,
                    onRename = onRename,
                    onDelete = onDelete
                )
            }
        },
        detailsContent = { detailsModifier ->
            when (item) {
                FreezerItem.NewPlate -> FileCollectionStatus(
                    text = stringResource(Res.string.freezer_new_plate_summary),
                    modifier = detailsModifier
                )
                is FreezerItem.Stored -> when (val details = item.details) {
                    is FreezerItemDetails.Readable -> ReadableSubstrateSummary(
                        details = details,
                        modifier = detailsModifier
                    )
                    is FreezerItemDetails.NewerVersion -> FileCollectionStatus(
                        text = stringResource(Res.string.freezer_newer_version_short_status),
                        isError = true,
                        modifier = detailsModifier
                    )
                    is FreezerItemDetails.Unreadable -> FileCollectionStatus(
                        text = stringResource(Res.string.freezer_read_error_message),
                        isError = true,
                        modifier = detailsModifier
                    )
                }
            }
        }
    )
}

@Composable
private fun ReadableSubstrateSummary(
    details: FreezerItemDetails.Readable,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    val date = details.createdAtUnixMillis?.let(::formatLocalizedDate) ?: UNKNOWN_VALUE
    val age = "${formatOneDecimal(details.unfrozenAgeHours)} " +
        stringResource(Res.string.simulation_hour_abbreviation)
    val dishDiameter = details.dishDiameterMillimeters?.let { diameter ->
        stringResource(
            Res.string.freezer_card_dish_diameter_format,
            formatMeasurement(diameter)
        )
    } ?: UNKNOWN_VALUE
    val createdDescription = stringResource(
        Res.string.freezer_card_created_content_description,
        date
    )
    val ageDescription = stringResource(
        Res.string.freezer_card_age_content_description,
        age
    )
    val dishDescription = stringResource(
        Res.string.freezer_card_dish_content_description,
        dishDiameter
    )

    Column(modifier = modifier) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(dimensions.large)
        ) {
            FileCollectionMetricBlock(
                label = stringResource(Res.string.freezer_card_cells_label),
                value = formatCapacity(details.cellCount, details.maximumCellCount),
                modifier = Modifier.weight(1f)
            )
            FileCollectionMetricBlock(
                label = stringResource(Res.string.freezer_card_food_label),
                value = formatCapacity(details.foodCount, details.maximumFoodCount),
                modifier = Modifier.weight(1f)
            )
        }
        Spacer(Modifier.height(dimensions.xSmall))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(dimensions.large)
        ) {
            FileCollectionMetricBlock(
                label = stringResource(Res.string.freezer_card_mass_label),
                value = details.foodMass?.let(::formatMeasurement) ?: UNKNOWN_VALUE,
                modifier = Modifier.weight(1f)
            )
            FileCollectionMetricBlock(
                label = stringResource(Res.string.freezer_card_dish_label),
                value = dishDiameter,
                contentDescription = dishDescription,
                modifier = Modifier.weight(1f)
            )
        }
        Spacer(Modifier.weight(1f))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(dimensions.large),
            verticalAlignment = Alignment.CenterVertically
        ) {
            FileCollectionIconMetric(
                icon = Icons.Default.DateRange,
                value = date,
                contentDescription = createdDescription,
                modifier = Modifier.weight(1f)
            )
            FileCollectionIconMetric(
                icon = Icons.Default.Schedule,
                value = age,
                contentDescription = ageDescription,
                modifier = Modifier.weight(1f)
            )
        }
    }
}

@Composable
private fun NewPlatePreview(modifier: Modifier, shape: Shape) {
    Surface(
        modifier = modifier,
        shape = shape,
        color = MaterialTheme.colorScheme.primaryContainer
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                imageVector = Icons.Default.Add,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(56.dp)
            )
        }
    }
}

@Composable
private fun DefaultSubstratePreviewPlaceholder(modifier: Modifier, shape: Shape) {
    FileCollectionPreviewPlaceholder(
        painter = painterResource(Res.drawable.plate),
        modifier = modifier,
        shape = shape,
        tint = Color.Unspecified
    )
}

@Composable
private fun AdvancedSettingsDialog(
    estimateMemoryBytes: (Int, Int, Double) -> Long?,
    onDismiss: () -> Unit,
    onStart: (SimulationConfiguration) -> Unit
) {
    var cells by remember { mutableStateOf("1000") }
    var food by remember { mutableStateOf("3500") }
    var diameter by remember { mutableStateOf("2.0") }
    var ring by remember { mutableStateOf(false) }
    var innerDiameter by remember { mutableStateOf("1.0") }

    val configuration = remember(cells, food, diameter, ring, innerDiameter) {
        val c = cells.toIntOrNull()
        val f = food.toIntOrNull()
        val d = diameter.toDoubleOrNull()
        val inner = if (ring) innerDiameter.toDoubleOrNull() else 0.0
        if (c != null && f != null && d != null && inner != null) {
            SimulationConfiguration(c, f, d * 0.5, inner * 0.5)
                .takeIf { (!ring || it.innerRadius > 0.0) && SimulationContract.isConfigurationValid(it) }
        } else {
            null
        }
    }

    val estimatedMegabytes = remember(configuration, estimateMemoryBytes) {
        configuration?.let {
            estimateMemoryBytes(it.maxCellCount, it.maxFoodCount, it.radius)
                ?.div(BYTES_PER_MEBIBYTE)
        }
    }
    val usageText = estimatedMegabytes?.let { megabytes ->
        stringResource(Res.string.freezer_memory_estimate_format, megabytes)
    }.orEmpty()

    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(Res.string.freezer_advanced_settings_title)) },
        text = {
            Column(
                modifier = Modifier
                    .verticalScroll(rememberScrollState())
                    .fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = stringResource(Res.string.freezer_advanced_settings_warning),
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(bottom = 8.dp)
                )

                Text(
                    text = usageText,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(bottom = 12.dp)
                )

                Text(
                    text = stringResource(Res.string.freezer_advanced_settings_maximum_cell_count_label),
                    style = MaterialTheme.typography.titleMedium
                )
                OutlinedTextField(
                    value = cells,
                    onValueChange = { cells = it },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth(),
                    colors = cellularityOutlinedTextFieldColors()
                )

                Spacer(modifier = Modifier.height(12.dp))

                Text(
                    text = stringResource(Res.string.freezer_advanced_settings_maximum_nutrient_count_label),
                    style = MaterialTheme.typography.titleMedium
                )
                OutlinedTextField(
                    value = food,
                    onValueChange = { food = it },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth(),
                    colors = cellularityOutlinedTextFieldColors()
                )

                Spacer(modifier = Modifier.height(12.dp))

                Text(
                    text = stringResource(Res.string.freezer_advanced_settings_petri_dish_diameter_label),
                    style = MaterialTheme.typography.titleMedium
                )
                OutlinedTextField(
                    value = diameter,
                    onValueChange = { diameter = it },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                    modifier = Modifier.fillMaxWidth(),
                    colors = cellularityOutlinedTextFieldColors()
                )

                Spacer(modifier = Modifier.height(12.dp))
                Text(
                    text = stringResource(Res.string.freezer_dish_shape_label),
                    style = MaterialTheme.typography.titleMedium
                )
                Row(modifier = Modifier.fillMaxWidth()) {
                    DefaultLabeledRadioButton(
                        label = stringResource(Res.string.freezer_dish_shape_circle),
                        selected = !ring,
                        modifier = Modifier.weight(1f),
                        onClick = { ring = false }
                    )
                    DefaultLabeledRadioButton(
                        label = stringResource(Res.string.freezer_dish_shape_ring),
                        selected = ring,
                        modifier = Modifier.weight(1f),
                        onClick = { ring = true }
                    )
                }
                if (ring) {
                    Text(
                        text = stringResource(Res.string.freezer_inner_diameter_label),
                        style = MaterialTheme.typography.titleMedium
                    )
                    OutlinedTextField(
                        value = innerDiameter,
                        onValueChange = { innerDiameter = it },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                        modifier = Modifier.fillMaxWidth(),
                        isError = configuration == null,
                        colors = cellularityOutlinedTextFieldColors()
                    )
                    Text(
                        text = stringResource(
                            Res.string.freezer_ring_dimensions_hint,
                            formatMeasurement(2.0 * CellDataConstants.MAX_CELL_RADIUS)
                        ),
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        },
        confirmButton = {
            TextButton(
                enabled = estimatedMegabytes != null,
                onClick = {
                    configuration?.let(onStart)
                }
            ) {
                Text(stringResource(Res.string.common_start_action))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.common_cancel_button_label))
            }
        }
    )
}

private fun formatOneDecimal(value: Double): String =
    formatLocalizedNumber(value, minimumFractionDigits = 1, maximumFractionDigits = 1)

private fun formatCellCount(value: Int): String = formatLocalizedNumber(
    value = value.toDouble(),
    minimumFractionDigits = 0,
    maximumFractionDigits = 0,
    useGrouping = true
)

private fun formatCapacity(current: Int?, maximum: Int?): String =
    "${current?.let(::formatCellCount) ?: UNKNOWN_VALUE} / " +
        (maximum?.let(::formatCellCount) ?: UNKNOWN_VALUE)

private fun formatMeasurement(value: Double): String = formatLocalizedNumber(
    value = value,
    minimumFractionDigits = 0,
    maximumFractionDigits = 2,
    useGrouping = true
)

private const val BYTES_PER_MEBIBYTE = 1024L * 1024L
private const val UNKNOWN_VALUE = "—"
