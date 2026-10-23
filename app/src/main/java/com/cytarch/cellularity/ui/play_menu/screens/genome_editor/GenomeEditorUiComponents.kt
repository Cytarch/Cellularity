package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.generated.resources.*
import org.jetbrains.compose.resources.stringResource
import org.jetbrains.compose.resources.stringArrayResource

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledRadioButton
import com.cytarch.cellularity.shared.ui.components.SliderCard
import com.cytarch.cellularity.shared.ui.components.SliderCardStrings
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeGeneCatalog
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeGeneOption
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import java.util.Locale
import kotlin.math.PI
import kotlin.math.floor
import kotlin.math.round

private const val DISPLAY_BOUNDARY_CANONICALIZATION_SCALE = 100_000.0
private const val DISPLAY_FRACTION_SCALE = 1_000_000.0

internal const val NANOGRAMS_PER_INTERNAL_MASS_UNIT = 10.0
internal const val NANOGRAMS_PER_INTERNAL_STIFFNESS_UNIT = 10.0
internal const val MICROMETRES_PER_MILLIMETRE = 1_000.0
internal const val PERCENT_PER_FRACTION = 100.0

data class GenomeEditorGestureCallbacks(
    val onStarted: () -> Unit = {},
    val onFinished: () -> Unit = {}
)

internal fun formatGenomeSliderValue(value: Double): String =
    String.format(Locale.getDefault(), "%1$,.6f", value)

internal fun formatGenomeSliderInput(value: Double): String =
    String.format(Locale.US, "%.6f", value)

internal fun scaleGenomeEditorValue(value: Double, displayScale: Double): Double =
    value * displayScale

internal fun unscaleGenomeEditorValue(value: Double, displayScale: Double): Double =
    value / displayScale

internal fun canonicalizeGenomeEditorDisplayBoundary(value: Double): Double =
    round(value * DISPLAY_BOUNDARY_CANONICALIZATION_SCALE) /
        DISPLAY_BOUNDARY_CANONICALIZATION_SCALE

internal fun floorGenomeEditorDisplayBoundary(value: Double): Double =
    floor(value * DISPLAY_FRACTION_SCALE) / DISPLAY_FRACTION_SCALE

internal fun formatGenomeUnitValue(
    value: Double,
    unit: String,
    separateUnit: Boolean
): String = buildString {
    append(formatGenomeSliderValue(value))
    if (separateUnit) append(' ')
    append(unit)
}

internal fun formatGenomeAngularStiffnessUnit(
    nanogramUnit: String,
    hourUnit: String
): String = "$nanogramUnit/$hourUnit²"

internal fun angleRadiansToDegrees(radians: Double): Double = radians * 180.0 / PI

internal fun angleDegreesToRadians(degrees: Double): Double = degrees * PI / 180.0

internal fun angleRadiansToEditorDegrees(radians: Double): Double =
    angleRadiansToDegrees(radians).coerceIn(0.0, 360.0)

internal fun formatGenomeAngleValue(degrees: Double): String =
    String.format(Locale.getDefault(), "%1$,.6f°", degrees)

internal fun formatGenomeAngleInput(degrees: Double): String =
    String.format(Locale.US, "%.6f", degrees)

@Composable
internal fun GenomeEditorSlider(
    label: String,
    value: Double,
    minimum: Double,
    maximum: Double,
    steps: Int,
    enabled: Boolean = true,
    gestures: GenomeEditorGestureCallbacks = GenomeEditorGestureCallbacks(),
    valueFormatter: (Double) -> String = ::formatGenomeSliderValue,
    inputValueFormatter: (Double) -> String = ::formatGenomeSliderInput,
    onValueChange: (Double) -> Unit
) {
    SliderCard(
        label = label,
        currentValue = value,
        min = minimum,
        max = maximum,
        maxSteps = steps.coerceAtLeast(1),
        valueFormatter = valueFormatter,
        inputValueFormatter = inputValueFormatter,
        editIcon = {
            Icon(
                imageVector = Icons.Default.Settings,
                contentDescription = stringResource(
                    Res.string.common_edit_value_precisely_action
                ),
                tint = Color.Gray,
                modifier = Modifier.size(22.dp)
            )
        },
        strings = SliderCardStrings(
            invalidNumber = stringResource(Res.string.common_invalid_number_message),
            confirm = stringResource(Res.string.common_ok_action),
            dismiss = stringResource(Res.string.common_cancel_action)
        ),
        enabled = enabled,
        onValueChangeStarted = gestures.onStarted,
        onValueChangeFinished = gestures.onFinished,
        onValueChange = onValueChange
    )
}

@Composable
internal fun GenomeEditorScaledUnitSlider(
    label: String,
    value: Double,
    minimum: Double,
    maximum: Double,
    displayScale: Double,
    unit: String,
    steps: Int,
    maximumDisplayValue: Double? = null,
    separateUnit: Boolean = true,
    enabled: Boolean = true,
    gestures: GenomeEditorGestureCallbacks = GenomeEditorGestureCallbacks(),
    onValueChange: (Double) -> Unit
) {
    val displayMinimum = canonicalizeGenomeEditorDisplayBoundary(
        scaleGenomeEditorValue(minimum, displayScale)
    )
    val displayMaximum = maximumDisplayValue ?: canonicalizeGenomeEditorDisplayBoundary(
        scaleGenomeEditorValue(maximum, displayScale)
    )
    val displayValue = scaleGenomeEditorValue(value, displayScale)
        .coerceIn(displayMinimum, displayMaximum)

    GenomeEditorSlider(
        label = label,
        value = displayValue,
        minimum = displayMinimum,
        maximum = displayMaximum,
        steps = steps,
        enabled = enabled,
        gestures = gestures,
        valueFormatter = { formatGenomeUnitValue(it, unit, separateUnit) },
        inputValueFormatter = ::formatGenomeSliderInput,
        onValueChange = {
            onValueChange(unscaleGenomeEditorValue(it, displayScale))
        }
    )
}

@Composable
internal fun GenomeEditorAngleSlider(
    label: String,
    radians: Double,
    steps: Int,
    gestures: GenomeEditorGestureCallbacks = GenomeEditorGestureCallbacks(),
    onRadiansChange: (Double) -> Unit
) {
    GenomeEditorSlider(
        label = label,
        value = angleRadiansToEditorDegrees(radians),
        minimum = 0.0,
        maximum = 360.0,
        steps = steps,
        gestures = gestures,
        valueFormatter = ::formatGenomeAngleValue,
        inputValueFormatter = ::formatGenomeAngleInput,
        onValueChange = { degrees ->
            onRadiansChange(angleDegreesToRadians(degrees))
        }
    )
}

@Composable
internal fun GenomeEditorSectionMessage(text: String) {
    DefaultCard {
        Text(
            text = text,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(16.dp)
        )
    }
}

@Composable
internal fun GenomeEditorDropdownMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    val dimensions = CellularityDimensions.current
    DropdownMenu(
        expanded = expanded,
        onDismissRequest = onDismissRequest,
        modifier = modifier,
        shape = RoundedCornerShape(dimensions.cardCorner),
        containerColor = MaterialTheme.colorScheme.surfaceVariant,
        tonalElevation = dimensions.none,
        shadowElevation = dimensions.cardElevation,
        content = content
    )
}

@Composable
internal fun GenomeEditorDropdownMenuItem(
    text: String,
    selected: Boolean,
    onClick: () -> Unit,
    textColor: Color? = null
) {
    val dimensions = CellularityDimensions.current
    val resolvedTextColor = textColor ?: if (selected) {
        MaterialTheme.colorScheme.primary
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant
    }
    DropdownMenuItem(
        text = {
            Text(
                text = text,
                color = resolvedTextColor,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium
            )
        },
        trailingIcon = if (selected) {
            {
                Icon(
                    imageVector = Icons.Default.Check,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(dimensions.swatchSize)
                )
            }
        } else {
            null
        },
        contentPadding = PaddingValues(horizontal = dimensions.medium),
        onClick = onClick
    )
}

@Composable
internal fun GenomeEditorDropdownAnchor(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    textColor: Color = MaterialTheme.colorScheme.onSurfaceVariant,
    fillAvailableWidth: Boolean = false
) {
    val dimensions = CellularityDimensions.current
    val shape = RoundedCornerShape(dimensions.cardCorner)
    Row(
        modifier = modifier
            .height(dimensions.compactControlHeight)
            .clip(shape)
            .background(MaterialTheme.colorScheme.primaryContainer)
            .clickable(onClick = onClick)
            .padding(start = dimensions.medium, end = dimensions.xSmall),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = text,
            color = textColor,
            style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.SemiBold,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = if (fillAvailableWidth) Modifier.weight(1f) else Modifier
        )
        Spacer(modifier = Modifier.width(dimensions.xSmall))
        Icon(
            imageVector = Icons.Default.ArrowDropDown,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(dimensions.swatchSize)
        )
    }
}

@Composable
internal fun GenomeCellTypeDropdown(
    selectedType: CellType,
    onSelected: (CellType) -> Unit,
    modifier: Modifier = Modifier
) {
    var expanded by remember { mutableStateOf(false) }
    val cellTypeNames = stringArrayResource(Res.array.simulation_cell_type_names)
    Box(modifier = modifier) {
        GenomeEditorDropdownAnchor(
            text = cellTypeNames[selectedType.nativeValue],
            onClick = { expanded = true }
        )
        GenomeEditorDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false }
        ) {
            CellType.entries.forEach { cellType ->
                GenomeEditorDropdownMenuItem(
                    text = cellTypeNames[cellType.nativeValue],
                    selected = cellType == selectedType,
                    onClick = {
                        expanded = false
                        onSelected(cellType)
                    }
                )
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun GenomeGeneDropdown(
    genes: GenomeGeneCatalog,
    selected: GeneIndex,
    onSelected: (GeneIndex) -> Unit,
    modifier: Modifier = Modifier,
    onLongClick: ((GeneIndex) -> Unit)? = null,
    isEnabled: (GeneIndex) -> Boolean = { true },
    onAddGene: (() -> Unit)? = null,
    canAddGene: Boolean = true
) {
    val dimensions = CellularityDimensions.current
    var expanded by remember { mutableStateOf(false) }
    val abbreviations = stringArrayResource(Res.array.genome_editor_cell_type_abbreviations)
    val selectedGene = genes.options[selected.value]
    val selectedColor = selectedGene.composeColor()
    val selectedAbbreviation = abbreviations.getOrNull(selectedGene.cellType.nativeValue).orEmpty()
    val controlShape = RoundedCornerShape(dimensions.cardCorner)

    Box(modifier = modifier) {
        Row(
            modifier = Modifier
                .height(dimensions.compactControlHeight)
                .clip(controlShape)
                .background(MaterialTheme.colorScheme.surfaceVariant)
                .clickable { expanded = true }
                .padding(start = dimensions.medium, end = dimensions.xSmall),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(dimensions.swatchSize)
                    .clip(RoundedCornerShape(dimensions.smallCorner))
                    .background(selectedColor)
                    .border(
                        dimensions.hairline,
                        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.35f),
                        RoundedCornerShape(dimensions.smallCorner)
                    )
            )
            Spacer(modifier = Modifier.width(dimensions.small))
            Text(
                text = stringResource(
                    Res.string.genome_editor_gene_with_type_format,
                    stringResource(Res.string.genome_editor_gene_mode_prefix),
                    selected.value + 1,
                    selectedAbbreviation
                ),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.SemiBold
            )
            Icon(
                imageVector = Icons.Default.ArrowDropDown,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(dimensions.swatchSize)
            )
        }
        GenomeEditorDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            modifier = Modifier.width(300.dp).heightIn(max = 400.dp)
        ) {
            val tileCount = genes.options.size + if (onAddGene != null) 1 else 0
            (0 until tileCount).chunked(5).forEach { rowIndices ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(
                            horizontal = dimensions.xSmall,
                            vertical = dimensions.xxSmall
                        ),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    rowIndices.forEach { index ->
                        if (index < genes.options.size) {
                            val id = GeneIndex(index)
                            val gene = genes.options[index]
                            GenomeGeneTile(
                                gene = gene,
                                number = index + 1,
                                abbreviation = abbreviations
                                    .getOrNull(gene.cellType.nativeValue)
                                    .orEmpty(),
                                selected = id == selected,
                                enabled = isEnabled(id),
                                onClick = {
                                    expanded = false
                                    onSelected(id)
                                },
                                onLongClick = onLongClick?.let {
                                    {
                                        expanded = false
                                        it(id)
                                    }
                                },
                                modifier = Modifier.weight(1f)
                            )
                        } else {
                            val addGene = requireNotNull(onAddGene)
                            GenomeAddGeneTile(
                                enabled = canAddGene,
                                onClick = {
                                    expanded = false
                                    addGene()
                                },
                                modifier = Modifier.weight(1f)
                            )
                        }
                    }
                    repeat(5 - rowIndices.size) { Spacer(modifier = Modifier.weight(1f)) }
                }
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun GenomeGeneTile(
    gene: GenomeGeneOption,
    number: Int,
    abbreviation: String,
    selected: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
    onLongClick: (() -> Unit)?,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    val shape = RoundedCornerShape(dimensions.smallCorner)
    val contentAlpha = if (enabled) 1.0f else 0.35f
    val interaction = when {
        enabled && onLongClick != null -> Modifier.combinedClickable(
            onClick = onClick,
            onLongClick = onLongClick
        )
        enabled -> Modifier.clickable(onClick = onClick)
        else -> Modifier
    }
    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier
            .padding(dimensions.xxSmall)
            .height(dimensions.iconButtonSize + dimensions.medium)
            .clip(shape)
            .background(
                if (selected) {
                    MaterialTheme.colorScheme.background
                } else {
                    MaterialTheme.colorScheme.primaryContainer
                }
            )
            .border(
                dimensions.hairline,
                if (selected) MaterialTheme.colorScheme.primary else Color.Transparent,
                shape
            )
            .then(interaction)
            .padding(horizontal = dimensions.xxSmall, vertical = dimensions.xSmall)
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(dimensions.medium)
                        .clip(RoundedCornerShape(dimensions.xxSmall))
                        .background(gene.composeColor().copy(alpha = contentAlpha))
                        .border(
                            dimensions.hairline,
                            MaterialTheme.colorScheme.onSurfaceVariant.copy(
                                alpha = 0.35f * contentAlpha
                            ),
                            RoundedCornerShape(dimensions.xxSmall)
                        )
                )
                Spacer(modifier = Modifier.width(dimensions.xxSmall))
                Text(
                    text = number.toString(),
                    color = if (selected) {
                        MaterialTheme.colorScheme.primary.copy(alpha = contentAlpha)
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = contentAlpha)
                    },
                    style = MaterialTheme.typography.labelMedium,
                    fontWeight = if (selected) FontWeight.Bold else FontWeight.SemiBold
                )
            }
            Text(
                text = abbreviation,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(
                    alpha = 0.72f * contentAlpha
                ),
                style = MaterialTheme.typography.labelSmall,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun GenomeAddGeneTile(
    enabled: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val dimensions = CellularityDimensions.current
    val shape = RoundedCornerShape(dimensions.smallCorner)
    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier
            .padding(dimensions.xxSmall)
            .height(dimensions.iconButtonSize + dimensions.medium)
            .clip(shape)
            .background(MaterialTheme.colorScheme.primaryContainer)
            .then(if (enabled) Modifier.clickable(onClick = onClick) else Modifier)
    ) {
        Icon(
            imageVector = Icons.Default.Add,
            contentDescription = stringResource(Res.string.genome_editor_add_gene_action),
            tint = MaterialTheme.colorScheme.primary.copy(
                alpha = if (enabled) 1.0f else 0.35f
            ),
            modifier = Modifier.size(dimensions.swatchSize)
        )
    }
}

@Composable
internal fun GenomeIntegerDropdown(
    label: String,
    selectedIndex: Int,
    names: List<String>,
    onSelected: (Int) -> Unit,
    colors: List<Color>? = null
) {
    var expanded by remember { mutableStateOf(false) }
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f)
        )
        Box {
            GenomeEditorDropdownAnchor(
                text = names.getOrNull(selectedIndex).orEmpty(),
                textColor = colors?.getOrNull(selectedIndex)
                    ?: MaterialTheme.colorScheme.onSurfaceVariant,
                onClick = { expanded = true }
            )
            GenomeEditorDropdownMenu(
                expanded = expanded,
                onDismissRequest = { expanded = false },
                modifier = Modifier.heightIn(max = 300.dp)
            ) {
                names.forEachIndexed { index, name ->
                    GenomeEditorDropdownMenuItem(
                        text = name,
                        selected = index == selectedIndex,
                        textColor = colors?.getOrNull(index),
                        onClick = {
                            expanded = false
                            onSelected(index)
                        }
                    )
                }
            }
        }
    }
}

@Composable
internal fun GenomeChoiceRow(
    label: String,
    selectedIndex: Int,
    options: List<String>,
    onSelected: (Int) -> Unit
) {
    GenomeIntegerDropdown(label, selectedIndex, options, onSelected)
}

@Composable
internal fun GenomeRadioRow(
    label: String,
    selected: Boolean,
    onClick: () -> Unit
) {
    DefaultLabeledRadioButton(
        label = label,
        selected = selected,
        onClick = onClick
    )
}

private fun GenomeGeneOption.composeColor(): Color = Color(
    color[0].coerceIn(0.0f, 1.0f),
    color[1].coerceIn(0.0f, 1.0f),
    color[2].coerceIn(0.0f, 1.0f)
)
