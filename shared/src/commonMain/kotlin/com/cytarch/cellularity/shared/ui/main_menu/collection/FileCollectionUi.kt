package com.cytarch.cellularity.shared.ui.main_menu.collection

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Sort
import androidx.compose.material.icons.automirrored.filled.ViewList
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.ErrorOutline
import androidx.compose.material.icons.filled.GridView
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.SearchOff
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_cancel_button_label
import com.cytarch.cellularity.shared.generated.resources.common_ok_action
import com.cytarch.cellularity.shared.generated.resources.file_collection_clear_search_action
import com.cytarch.cellularity.shared.generated.resources.file_collection_clear_search_content_description
import com.cytarch.cellularity.shared.generated.resources.file_collection_delete_action
import com.cytarch.cellularity.shared.generated.resources.file_collection_more_actions_content_description
import com.cytarch.cellularity.shared.generated.resources.file_collection_rename_action
import com.cytarch.cellularity.shared.generated.resources.file_collection_share_action
import com.cytarch.cellularity.shared.generated.resources.file_collection_sort_ascending
import com.cytarch.cellularity.shared.generated.resources.file_collection_sort_descending
import com.cytarch.cellularity.shared.generated.resources.file_collection_switch_to_grid_view_content_description
import com.cytarch.cellularity.shared.generated.resources.file_collection_switch_to_list_view_content_description
import com.cytarch.cellularity.shared.settings.CollectionLayoutMode
import com.cytarch.cellularity.shared.settings.CollectionSortDirection
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import com.cytarch.cellularity.shared.ui.theme.cellularityOutlinedTextFieldColors
import org.jetbrains.compose.resources.stringResource

@Composable
internal fun <SortField> FileCollectionToolbar(
    searchQuery: String,
    searchPlaceholder: String,
    sortFields: List<SortField>,
    sortField: SortField,
    sortFieldLabel: @Composable (SortField) -> String,
    sortDirection: CollectionSortDirection,
    layoutMode: CollectionLayoutMode,
    onSearchQueryChange: (String) -> Unit,
    onSortFieldChange: (SortField) -> Unit,
    onSortDirectionChange: (CollectionSortDirection) -> Unit,
    onLayoutModeChange: (CollectionLayoutMode) -> Unit
) {
    val dimensions = CellularityDimensions.current
    var sortMenuExpanded by remember { mutableStateOf(false) }
    val controlShape = RoundedCornerShape(dimensions.cardCorner)
    val focusManager = LocalFocusManager.current
    val keyboardController = LocalSoftwareKeyboardController.current
    val fieldLabel = sortFieldLabel(sortField)
    val directionLabel = collectionSortDirectionLabel(sortDirection)
    val selectedSortDescription = "$fieldLabel, $directionLabel"

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = dimensions.large,
                top = dimensions.large,
                end = dimensions.large,
                bottom = dimensions.xSmall
            ),
        horizontalArrangement = Arrangement.spacedBy(dimensions.medium),
        verticalAlignment = Alignment.CenterVertically
    ) {
        FileCollectionSearchControl(
            searchQuery = searchQuery,
            searchPlaceholder = searchPlaceholder,
            onSearchQueryChange = onSearchQueryChange,
            shape = controlShape,
            modifier = Modifier.weight(1f)
        )

        Box {
            Box(
                modifier = Modifier
                    .height(MINIMUM_TOUCH_TARGET_SIZE)
                    .semantics { contentDescription = selectedSortDescription }
                    .clickable(role = Role.Button) {
                        keyboardController?.hide()
                        focusManager.clearFocus()
                        sortMenuExpanded = true
                    },
                contentAlignment = Alignment.Center
            ) {
                Row(
                    modifier = Modifier
                        .height(dimensions.compactControlHeight)
                        .clip(controlShape)
                        .background(MaterialTheme.colorScheme.surfaceVariant)
                        .padding(start = dimensions.medium, end = dimensions.xSmall),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.AutoMirrored.Filled.Sort,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(dimensions.swatchSize)
                    )
                    Spacer(Modifier.width(dimensions.small))
                    Text(
                        text = "$fieldLabel ${sortDirection.arrow}",
                        modifier = Modifier.widthIn(max = 112.dp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Icon(
                        Icons.Default.ArrowDropDown,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(dimensions.swatchSize)
                    )
                }
            }
            DropdownMenu(
                expanded = sortMenuExpanded,
                onDismissRequest = { sortMenuExpanded = false },
                modifier = Modifier.widthIn(min = 220.dp, max = 320.dp),
                shape = controlShape,
                containerColor = MaterialTheme.colorScheme.surfaceVariant,
                tonalElevation = dimensions.none,
                shadowElevation = dimensions.cardElevation
            ) {
                sortFields.forEach { option ->
                    val selected = option == sortField
                    DropdownMenuItem(
                        text = {
                            Text(
                                text = sortFieldLabel(option),
                                color = if (selected) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                },
                                style = MaterialTheme.typography.bodyMedium
                            )
                        },
                        trailingIcon = if (selected) {
                            {
                                Icon(
                                    Icons.Default.Check,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(dimensions.swatchSize)
                                )
                            }
                        } else {
                            null
                        },
                        contentPadding = PaddingValues(horizontal = dimensions.medium),
                        onClick = {
                            sortMenuExpanded = false
                            onSortFieldChange(option)
                        }
                    )
                }
                HorizontalDivider(
                    modifier = Modifier.padding(horizontal = dimensions.medium),
                    color = MaterialTheme.colorScheme.outlineVariant
                )
                CollectionSortDirection.entries.forEach { option ->
                    val selected = option == sortDirection
                    DropdownMenuItem(
                        text = {
                            Text(
                                text = collectionSortDirectionLabel(option),
                                color = if (selected) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                },
                                style = MaterialTheme.typography.bodyMedium
                            )
                        },
                        trailingIcon = if (selected) {
                            {
                                Icon(
                                    Icons.Default.Check,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(dimensions.swatchSize)
                                )
                            }
                        } else {
                            null
                        },
                        contentPadding = PaddingValues(horizontal = dimensions.medium),
                        onClick = {
                            sortMenuExpanded = false
                            onSortDirectionChange(option)
                        }
                    )
                }
            }
        }

        FileCollectionLayoutModeControl(
            layoutMode = layoutMode,
            shape = controlShape,
            onLayoutModeChange = { mode ->
                keyboardController?.hide()
                focusManager.clearFocus()
                onLayoutModeChange(mode)
            }
        )
    }
}

@Composable
internal fun <T> FileCollectionGrid(
    items: List<T>,
    layoutMode: CollectionLayoutMode,
    key: (T) -> Any,
    modifier: Modifier = Modifier,
    fullWidthContent: (@Composable () -> Unit)? = null,
    itemContent: @Composable (T, Modifier) -> Unit
) {
    val dimensions = CellularityDimensions.current
    LazyVerticalGrid(
        columns = when (layoutMode) {
            CollectionLayoutMode.GRID -> GridCells.Adaptive(minSize = 176.dp)
            CollectionLayoutMode.LIST -> GridCells.Fixed(1)
        },
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(dimensions.medium),
        verticalArrangement = Arrangement.spacedBy(dimensions.medium),
        contentPadding = PaddingValues(
            start = dimensions.large,
            top = dimensions.medium,
            end = dimensions.large,
            bottom = dimensions.large
        )
    ) {
        items(items = items, key = key) { item ->
            itemContent(
                item,
                Modifier.animateItem(fadeInSpec = null, fadeOutSpec = null)
            )
        }
        if (fullWidthContent != null) {
            item(
                key = "file-collection:full-width-content",
                span = { GridItemSpan(maxLineSpan) }
            ) {
                fullWidthContent()
            }
        }
    }
}

@Composable
internal fun FileCollectionCard(
    stateKey: Any,
    title: String,
    layoutMode: CollectionLayoutMode,
    previewContent: @Composable (Modifier, Shape) -> Unit,
    menuContent: @Composable (dismiss: () -> Unit) -> Unit,
    detailsContent: @Composable (Modifier) -> Unit,
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null
) {
    val dimensions = CellularityDimensions.current
    var menuExpanded by remember(stateKey) { mutableStateOf(false) }
    val interactiveModifier = if (onClick == null) {
        Modifier
    } else {
        Modifier.clickable(onClick = onClick)
    }

    Card(
        modifier = modifier
            .fillMaxWidth()
            .then(interactiveModifier),
        shape = RoundedCornerShape(dimensions.cardCorner),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
            contentColor = MaterialTheme.colorScheme.onSurfaceVariant
        ),
        elevation = CardDefaults.cardElevation(defaultElevation = dimensions.cardElevation)
    ) {
        FileCollectionCardLayout(
            layoutMode = layoutMode,
            previewContent = previewContent,
            detailsContent = { detailsModifier ->
                Column(modifier = detailsModifier) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(CARD_TITLE_ROW_HEIGHT)
                            .padding(
                                start = dimensions.xxLarge,
                                end = dimensions.xSmall
                            ),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = title,
                            style = MaterialTheme.typography.bodyLarge,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.weight(1f)
                        )
                        Box {
                            IconButton(onClick = { menuExpanded = true }) {
                                Icon(
                                    Icons.Default.MoreVert,
                                    contentDescription = stringResource(
                                        Res.string.file_collection_more_actions_content_description,
                                        title
                                    ),
                                    tint = MaterialTheme.colorScheme.primary
                                )
                            }
                            DropdownMenu(
                                expanded = menuExpanded,
                                onDismissRequest = { menuExpanded = false },
                                shape = RoundedCornerShape(dimensions.cardCorner),
                                containerColor = MaterialTheme.colorScheme.surfaceVariant,
                                tonalElevation = dimensions.none,
                                shadowElevation = dimensions.cardElevation
                            ) {
                                menuContent { menuExpanded = false }
                            }
                        }
                    }
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .weight(1f)
                            .padding(
                                start = dimensions.xxLarge,
                                end = dimensions.xxLarge,
                                bottom = dimensions.xLarge
                            )
                    ) {
                        detailsContent(Modifier.fillMaxSize())
                    }
                }
            }
        )
    }
}

@Composable
internal fun FileCollectionStoredActions(
    dismiss: () -> Unit,
    onShare: () -> Unit,
    onRename: () -> Unit,
    onDelete: () -> Unit
) {
    val dimensions = CellularityDimensions.current
    DropdownMenuItem(
        text = {
            Text(
                stringResource(Res.string.file_collection_share_action),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        },
        leadingIcon = {
            Icon(
                Icons.Default.Share,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
        },
        onClick = {
            dismiss()
            onShare()
        }
    )
    DropdownMenuItem(
        text = {
            Text(
                stringResource(Res.string.file_collection_rename_action),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        },
        leadingIcon = {
            Icon(
                Icons.Default.Edit,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.secondary
            )
        },
        onClick = {
            dismiss()
            onRename()
        }
    )
    HorizontalDivider(
        modifier = Modifier.padding(horizontal = dimensions.medium),
        color = MaterialTheme.colorScheme.outlineVariant
    )
    DropdownMenuItem(
        text = {
            Text(
                text = stringResource(Res.string.file_collection_delete_action),
                color = MaterialTheme.colorScheme.error
            )
        },
        leadingIcon = {
            Icon(
                Icons.Default.Delete,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.error
            )
        },
        onClick = {
            dismiss()
            onDelete()
        }
    )
}

@Composable
internal fun FileCollectionMetricBlock(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    contentDescription: String? = null
) {
    val semanticsModifier = if (contentDescription == null) {
        Modifier
    } else {
        Modifier.clearAndSetSemantics { this.contentDescription = contentDescription }
    }
    Column(modifier = modifier.then(semanticsModifier)) {
        Text(
            text = label,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.58f),
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
        Text(
            text = value,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.92f),
            style = MaterialTheme.typography.bodySmall,
            fontWeight = FontWeight.SemiBold,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

@Composable
internal fun FileCollectionIconMetric(
    icon: ImageVector,
    value: String,
    contentDescription: String,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    Row(
        modifier = modifier.clearAndSetSemantics {
            this.contentDescription = contentDescription
        },
        horizontalArrangement = Arrangement.spacedBy(dimensions.xSmall),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary.copy(alpha = 0.82f),
            modifier = Modifier.size(CARD_METADATA_ICON_SIZE)
        )
        Text(
            text = value,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.72f),
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

@Composable
internal fun FileCollectionStatus(
    text: String,
    isError: Boolean = false,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    Box(
        modifier = modifier.fillMaxWidth(),
        contentAlignment = Alignment.CenterStart
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (isError) {
                Icon(
                    imageVector = Icons.Default.ErrorOutline,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.error,
                    modifier = Modifier.size(STATUS_ICON_SIZE)
                )
                Spacer(Modifier.size(dimensions.xSmall))
            }
            Text(
                text = text,
                style = MaterialTheme.typography.bodySmall,
                color = if (isError) {
                    MaterialTheme.colorScheme.error
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.72f)
                },
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
internal fun FileCollectionPreviewPlaceholder(
    painter: Painter,
    modifier: Modifier,
    shape: Shape,
    iconSize: Dp = 48.dp,
    iconAlpha: Float = 1f,
    tint: Color = Color.Unspecified
) {
    Surface(
        modifier = modifier,
        shape = shape,
        color = MaterialTheme.colorScheme.primaryContainer
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                painter = painter,
                contentDescription = null,
                tint = tint,
                modifier = Modifier
                    .size(iconSize)
                    .alpha(iconAlpha)
            )
        }
    }
}

@Composable
internal fun FileCollectionNoResults(
    message: String,
    modifier: Modifier = Modifier,
    onClearSearch: (() -> Unit)? = null
) {
    val dimensions = CellularityDimensions.current
    Column(
        modifier = modifier
            .fillMaxWidth()
            .height(COLLECTION_CARD_FOOTER_HEIGHT)
            .padding(dimensions.xxxLarge),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(
            imageVector = Icons.Default.SearchOff,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.64f),
            modifier = Modifier.size(40.dp)
        )
        Spacer(Modifier.height(dimensions.medium))
        Text(
            text = message,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.SemiBold
        )
        if (onClearSearch != null) {
            TextButton(onClick = onClearSearch) {
                Text(stringResource(Res.string.file_collection_clear_search_action))
            }
        }
    }
}

@Composable
internal fun FileCollectionRenameDialog(
    title: String,
    initialName: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit
) {
    var text by remember(initialName) { mutableStateOf(initialName) }
    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
                colors = cellularityOutlinedTextFieldColors()
            )
        },
        confirmButton = {
            TextButton(onClick = { if (text.isNotBlank()) onConfirm(text.trim()) }) {
                Text(stringResource(Res.string.common_ok_action))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.common_cancel_button_label))
            }
        }
    )
}

@Composable
internal fun FileCollectionDeleteDialog(
    title: String,
    message: String,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit
) {
    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(message) },
        confirmButton = {
            TextButton(
                onClick = onConfirm,
                colors = ButtonDefaults.textButtonColors(
                    contentColor = MaterialTheme.colorScheme.error
                )
            ) {
                Text(stringResource(Res.string.file_collection_delete_action))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.common_cancel_button_label))
            }
        }
    )
}

internal fun compareCollectionNames(left: String, right: String): Int {
    val foldedComparison = left.lowercase().compareTo(right.lowercase())
    return if (foldedComparison != 0) foldedComparison else left.compareTo(right)
}

internal fun <T : Comparable<T>> compareOptionalCollectionValues(
    left: T?,
    right: T?,
    direction: CollectionSortDirection
): Int = when {
    left == null && right == null -> 0
    left == null -> 1
    right == null -> -1
    else -> left.compareTo(right).withCollectionSortDirection(direction)
}

internal fun Int.withCollectionSortDirection(direction: CollectionSortDirection): Int =
    if (direction == CollectionSortDirection.ASCENDING) this else -this

@Composable
private fun FileCollectionLayoutModeControl(
    layoutMode: CollectionLayoutMode,
    shape: RoundedCornerShape,
    onLayoutModeChange: (CollectionLayoutMode) -> Unit
) {
    val dimensions = CellularityDimensions.current
    val targetMode = when (layoutMode) {
        CollectionLayoutMode.GRID -> CollectionLayoutMode.LIST
        CollectionLayoutMode.LIST -> CollectionLayoutMode.GRID
    }
    val description = when (targetMode) {
        CollectionLayoutMode.GRID -> stringResource(
            Res.string.file_collection_switch_to_grid_view_content_description
        )
        CollectionLayoutMode.LIST -> stringResource(
            Res.string.file_collection_switch_to_list_view_content_description
        )
    }
    val icon = when (targetMode) {
        CollectionLayoutMode.GRID -> Icons.Default.GridView
        CollectionLayoutMode.LIST -> Icons.AutoMirrored.Filled.ViewList
    }

    Box(
        modifier = Modifier
            .size(MINIMUM_TOUCH_TARGET_SIZE)
            .semantics { contentDescription = description }
            .clickable(role = Role.Button) { onLayoutModeChange(targetMode) },
        contentAlignment = Alignment.Center
    ) {
        Box(
            modifier = Modifier
                .size(dimensions.compactControlHeight)
                .clip(shape)
                .background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(dimensions.swatchSize)
            )
        }
    }
}

@Composable
private fun FileCollectionSearchControl(
    searchQuery: String,
    searchPlaceholder: String,
    onSearchQueryChange: (String) -> Unit,
    shape: RoundedCornerShape,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    val clearDescription = stringResource(
        Res.string.file_collection_clear_search_content_description
    )
    val focusManager = LocalFocusManager.current
    val keyboardController = LocalSoftwareKeyboardController.current

    Box(
        modifier = modifier.height(MINIMUM_TOUCH_TARGET_SIZE),
        contentAlignment = Alignment.Center
    ) {
        BasicTextField(
            value = searchQuery,
            onValueChange = onSearchQueryChange,
            modifier = Modifier
                .fillMaxWidth()
                .height(MINIMUM_TOUCH_TARGET_SIZE),
            textStyle = MaterialTheme.typography.bodyMedium.copy(
                color = MaterialTheme.colorScheme.onSurfaceVariant
            ),
            singleLine = true,
            cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            keyboardActions = KeyboardActions(
                onSearch = {
                    keyboardController?.hide()
                    focusManager.clearFocus()
                }
            ),
            decorationBox = { textField ->
                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(dimensions.compactControlHeight)
                            .clip(shape)
                            .background(MaterialTheme.colorScheme.surfaceVariant)
                            .padding(
                                start = dimensions.medium,
                                end = MINIMUM_TOUCH_TARGET_SIZE
                            ),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            Icons.Default.Search,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(dimensions.swatchSize)
                        )
                        Spacer(Modifier.width(dimensions.small))
                        Box(
                            modifier = Modifier.weight(1f),
                            contentAlignment = Alignment.CenterStart
                        ) {
                            if (searchQuery.isEmpty()) {
                                Text(
                                    text = searchPlaceholder,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(
                                        alpha = 0.64f
                                    ),
                                    style = MaterialTheme.typography.bodyMedium,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            }
                            textField()
                        }
                    }
                }
            }
        )
        if (searchQuery.isNotEmpty()) {
            Box(
                modifier = Modifier
                    .align(Alignment.CenterEnd)
                    .size(MINIMUM_TOUCH_TARGET_SIZE)
                    .semantics { contentDescription = clearDescription }
                    .clickable(role = Role.Button) { onSearchQueryChange("") },
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    Icons.Default.Close,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(dimensions.swatchSize)
                )
            }
        }
    }
}

@Composable
private fun collectionSortDirectionLabel(direction: CollectionSortDirection): String =
    when (direction) {
        CollectionSortDirection.ASCENDING ->
            stringResource(Res.string.file_collection_sort_ascending)
        CollectionSortDirection.DESCENDING ->
            stringResource(Res.string.file_collection_sort_descending)
    }

private val CollectionSortDirection.arrow: String
    get() = when (this) {
        CollectionSortDirection.ASCENDING -> "↑"
        CollectionSortDirection.DESCENDING -> "↓"
    }

@Composable
private fun FileCollectionCardLayout(
    layoutMode: CollectionLayoutMode,
    previewContent: @Composable (Modifier, Shape) -> Unit,
    detailsContent: @Composable (Modifier) -> Unit
) {
    val dimensions = CellularityDimensions.current
    when (layoutMode) {
        CollectionLayoutMode.GRID -> Column(modifier = Modifier.fillMaxWidth()) {
            previewContent(
                Modifier
                    .fillMaxWidth()
                    .aspectRatio(1f),
                RoundedCornerShape(
                    topStart = dimensions.cardCorner,
                    topEnd = dimensions.cardCorner
                )
            )
            detailsContent(
                Modifier
                    .fillMaxWidth()
                    .height(COLLECTION_CARD_FOOTER_HEIGHT)
            )
        }
        CollectionLayoutMode.LIST -> Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(COLLECTION_LIST_CARD_HEIGHT)
        ) {
            previewContent(
                Modifier.size(COLLECTION_LIST_CARD_HEIGHT),
                RoundedCornerShape(
                    topStart = dimensions.cardCorner,
                    bottomStart = dimensions.cardCorner
                )
            )
            detailsContent(
                Modifier
                    .weight(1f)
                    .height(COLLECTION_LIST_CARD_HEIGHT)
            )
        }
    }
}

private val MINIMUM_TOUCH_TARGET_SIZE = 48.dp
private val COLLECTION_CARD_FOOTER_HEIGHT = 160.dp
private val COLLECTION_LIST_CARD_HEIGHT = COLLECTION_CARD_FOOTER_HEIGHT
private val CARD_TITLE_ROW_HEIGHT = 48.dp
private val CARD_METADATA_ICON_SIZE = 14.dp
private val STATUS_ICON_SIZE = 16.dp
