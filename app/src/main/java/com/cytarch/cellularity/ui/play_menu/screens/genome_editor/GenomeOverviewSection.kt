package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.generated.resources.*
import org.jetbrains.compose.resources.stringResource

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledCheckbox
import com.cytarch.cellularity.shared.ui.components.DefaultSlider
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeBooleanField
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.theme.cellularityOutlinedTextFieldColors
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorConfig
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorMath
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeOverviewSectionState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeScalarField
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeStructureSectionState
import java.util.Locale
import kotlin.math.roundToInt

@Composable
internal fun GenomeGeneralSection(
    overview: GenomeOverviewSectionState,
    structure: GenomeStructureSectionState,
    gestures: GenomeEditorGestureCallbacks,
    onAction: (GenomeEditorAction) -> Unit
) {
    val steps = if (structure.snapValues) {
        GenomeEditorConfig.COARSE_STEPS
    } else {
        GenomeEditorConfig.FINE_STEPS
    }
    var colorBeforeDialog by remember { mutableStateOf<List<Float>?>(null) }
    DefaultCard {
        Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
                ) {
                Text(
                    text = stringResource(Res.string.genome_editor_cell_type_label),
                    modifier = Modifier.weight(1f),
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                GenomeCellTypeDropdown(
                    selectedType = overview.cellType,
                    onSelected = { onAction(GenomeEditorAction.SetCellType(it)) }
                )
            }
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.genome_editor_prioritize_survival_label),
                checked = overview.stayAlive,
                onCheckedChange = {
                    onAction(
                        GenomeEditorAction.SetBoolean(GenomeBooleanField.STAY_ALIVE, it)
                    )
                }
            )
            GenomeRadioRow(
                label = stringResource(Res.string.genome_editor_initial_gene_label),
                selected = overview.initial,
                onClick = { onAction(GenomeEditorAction.MakeInitial(overview.gene)) }
            )
        }
    }

    val priorityPosition = runCatching {
        GenomeEditorMath.inverseExponentialScale(
            CellDataConstants.MIN_MASS_FLOW_PRIORITY.toDouble(),
            CellDataConstants.MAX_MASS_FLOW_PRIORITY.toDouble(),
            structure.massFlowPriority.toDouble()
        )
    }.getOrDefault(0.0)
    GenomeEditorSlider(
        label = stringResource(Res.string.genome_editor_mass_flow_priority_label),
        value = priorityPosition,
        minimum = 0.0,
        maximum = 1.0,
        steps = steps,
        gestures = gestures,
        onValueChange = {
            onAction(
                GenomeEditorAction.SetScalar(
                    GenomeScalarField.MASS_FLOW_PRIORITY,
                    GenomeEditorMath.exponentialScale(
                        CellDataConstants.MIN_MASS_FLOW_PRIORITY.toDouble(),
                        CellDataConstants.MAX_MASS_FLOW_PRIORITY.toDouble(),
                        it
                    ).toFloat()
                )
            )
        }
    )

    GenomeColorCard(
        red = overview.color[0],
        green = overview.color[1],
        blue = overview.color[2],
        onClick = { colorBeforeDialog = overview.color.toList() }
    )

    GenomeEditorSlider(
        label = stringResource(Res.string.genome_editor_cytoskeleton_label),
        value = structure.cytoskeleton.toDouble(),
        minimum = GenomeContract.floatRange(GeneFloatType.CYTOSKELETON).min.toDouble(),
        maximum = GenomeContract.floatRange(GeneFloatType.CYTOSKELETON).max.toDouble(),
        steps = steps,
        gestures = gestures,
        onValueChange = {
            onAction(GenomeEditorAction.SetFloat(GeneFloatType.CYTOSKELETON, it.toFloat()))
        }
    )

    colorBeforeDialog?.let { original ->
        GenomeColorDialog(
            red = original[0],
            green = original[1],
            blue = original[2],
            onColorChange = { red, green, blue ->
                onAction(GenomeEditorAction.SetColor(red, green, blue))
            },
            onCancel = {
                onAction(GenomeEditorAction.SetColor(original[0], original[1], original[2]))
                colorBeforeDialog = null
            },
            onConfirm = { colorBeforeDialog = null }
        )
    }
}

@Composable
private fun GenomeColorCard(
    red: Float,
    green: Float,
    blue: Float,
    onClick: () -> Unit
) {
    val color = Color(red.coerceUnit(), green.coerceUnit(), blue.coerceUnit())
    DefaultCard {
        Row(
            modifier = Modifier.fillMaxWidth().clickable(onClick = onClick).padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(Res.string.genome_editor_color_label),
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = rgbToHex(red, green, blue),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.Bold
                )
            }
            Box(
                modifier = Modifier
                    .width(72.dp)
                    .height(36.dp)
                    .clip(RoundedCornerShape(6.dp))
                    .background(color)
                    .border(1.dp, MaterialTheme.colorScheme.onSurfaceVariant, RoundedCornerShape(6.dp))
            )
        }
    }
}

@Composable
private fun GenomeColorDialog(
    red: Float,
    green: Float,
    blue: Float,
    onColorChange: (Float, Float, Float) -> Unit,
    onCancel: () -> Unit,
    onConfirm: () -> Unit
) {
    var currentRed by remember { mutableFloatStateOf(red.coerceUnit()) }
    var currentGreen by remember { mutableFloatStateOf(green.coerceUnit()) }
    var currentBlue by remember { mutableFloatStateOf(blue.coerceUnit()) }
    var hexDraft by remember { mutableStateOf(rgbToHex(red, green, blue)) }

    fun update(nextRed: Float, nextGreen: Float, nextBlue: Float, syncHex: Boolean = true) {
        currentRed = nextRed.coerceUnit()
        currentGreen = nextGreen.coerceUnit()
        currentBlue = nextBlue.coerceUnit()
        if (syncHex) hexDraft = rgbToHex(currentRed, currentGreen, currentBlue)
        onColorChange(currentRed, currentGreen, currentBlue)
    }

    CellularityAlertDialog(
        onDismissRequest = onCancel,
        title = {
            Text(text = stringResource(Res.string.genome_editor_color_dialog_title))
        },
        text = {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp)
                        .clip(RoundedCornerShape(6.dp))
                        .background(Color(currentRed, currentGreen, currentBlue))
                        .border(1.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(6.dp))
                )
                Spacer(modifier = Modifier.height(10.dp))
                OutlinedTextField(
                    value = hexDraft,
                    onValueChange = { input ->
                        hexDraft = input.trim().uppercase(Locale.US).take(7)
                        parseHexColor(hexDraft)?.let {
                            update(it.first, it.second, it.third, syncHex = false)
                        }
                    },
                    singleLine = true,
                    label = { Text(stringResource(Res.string.genome_editor_hex_label)) },
                    isError = parseHexColor(hexDraft) == null,
                    keyboardOptions = KeyboardOptions(
                        capitalization = KeyboardCapitalization.Characters,
                        keyboardType = KeyboardType.Ascii
                    ),
                    modifier = Modifier.fillMaxWidth(),
                    colors = cellularityOutlinedTextFieldColors()
                )
                Spacer(modifier = Modifier.height(8.dp))
                ColorChannelSlider(
                    stringResource(Res.string.genome_editor_color_red_label),
                    currentRed
                ) { update(it, currentGreen, currentBlue) }
                ColorChannelSlider(
                    stringResource(Res.string.genome_editor_color_green_label),
                    currentGreen
                ) { update(currentRed, it, currentBlue) }
                ColorChannelSlider(
                    stringResource(Res.string.genome_editor_color_blue_label),
                    currentBlue
                ) { update(currentRed, currentGreen, it) }
            }
        },
        confirmButton = {
            TextButton(onClick = onConfirm) {
                Text(stringResource(Res.string.common_ok_action))
            }
        },
        dismissButton = {
            TextButton(onClick = onCancel) {
                Text(stringResource(Res.string.common_cancel_action))
            }
        }
    )
}

@Composable
private fun ColorChannelSlider(label: String, value: Float, onChange: (Float) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(text = label, modifier = Modifier.width(52.dp))
        DefaultSlider(
            value = value,
            onValueChange = onChange,
            valueRange = 0.0f..1.0f,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = formatGenomeSliderValue(value.toDouble()),
            modifier = Modifier.width(80.dp)
        )
    }
}

private fun Float.coerceUnit(): Float = coerceIn(0.0f, 1.0f)

private fun rgbToHex(red: Float, green: Float, blue: Float): String = String.format(
    Locale.US,
    "#%02X%02X%02X",
    (red.coerceUnit() * 255.0f).roundToInt(),
    (green.coerceUnit() * 255.0f).roundToInt(),
    (blue.coerceUnit() * 255.0f).roundToInt()
)

private fun parseHexColor(value: String): Triple<Float, Float, Float>? {
    val normalized = value.trim().removePrefix("#")
    if (normalized.length != 6 || normalized.any {
            !it.isDigit() && it.uppercaseChar() !in 'A'..'F'
        }
    ) return null
    val color = normalized.toIntOrNull(16) ?: return null
    return Triple(
        ((color shr 16) and 0xFF) / 255.0f,
        ((color shr 8) and 0xFF) / 255.0f,
        (color and 0xFF) / 255.0f
    )
}
