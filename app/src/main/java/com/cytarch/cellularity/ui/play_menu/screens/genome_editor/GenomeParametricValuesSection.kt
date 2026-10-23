package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.generated.resources.*
import org.jetbrains.compose.resources.stringResource

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.ParametricValueDraft
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorConfig
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeParametricEditor
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeParametricEditorValues
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeParametricValueState
import org.jetbrains.compose.resources.painterResource

@Composable
internal fun GenomeParametricValueEditor(
    state: GenomeParametricValueState,
    label: String,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onValueChange: (ParametricValueDraft) -> Unit
) {
    var showDialog by remember { mutableStateOf(false) }
    val fixed = state.value.formulaType.toInt() == 0
    val physicalValue = GenomeParametricEditor.decode(
        state.value,
        state.minimum,
        state.maximum
    ).fixedValue

    DefaultCard {
        Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = label,
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = if (fixed) {
                            formatGenomeSliderValue(physicalValue.toDouble())
                        } else {
                            formulaLabel(state.value.formulaType.toInt())
                        },
                        color = if (fixed) MaterialTheme.colorScheme.primary
                        else MaterialTheme.colorScheme.secondary,
                        fontWeight = FontWeight.Bold
                    )
                }
                IconButton(onClick = { showDialog = true }) {
                    Icon(
                        painter = painterResource(Res.drawable.settings_icon),
                        contentDescription = stringResource(
                            Res.string.common_edit_value_precisely_action
                        ),
                        tint = Color.Gray
                    )
                }
            }
            if (fixed) {
                GenomeEditorSlider(
                    label = stringResource(Res.string.genome_editor_value_dialog_value_label),
                    value = physicalValue.toDouble(),
                    minimum = state.minimum.toDouble(),
                    maximum = state.maximum.toDouble(),
                    steps = if (snapValues) {
                        GenomeEditorConfig.COARSE_STEPS
                    } else {
                        GenomeEditorConfig.FINE_STEPS
                    },
                    gestures = gestures,
                    onValueChange = { newValue ->
                        val normalized = if (state.maximum == state.minimum) {
                            0.0f
                        } else {
                            (2.0 * (newValue - state.minimum) /
                                (state.maximum - state.minimum) - 1.0).toFloat()
                        }
                        onValueChange(
                            state.value.copy(
                                formulaType = 0,
                                variableA = 0.0f,
                                variableB = normalized,
                                variableC = 0.0f
                            )
                        )
                    }
                )
            }
        }
    }

    if (showDialog) {
        ParametricValueDialog(
            state = state,
            snapValues = snapValues,
            gestures = gestures,
            onConfirm = {
                onValueChange(it)
                showDialog = false
            },
            onDismiss = { showDialog = false }
        )
    }
}

@Composable
private fun ParametricValueDialog(
    state: GenomeParametricValueState,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onConfirm: (ParametricValueDraft) -> Unit,
    onDismiss: () -> Unit
) {
    var formulaType by remember { mutableIntStateOf(state.value.formulaType.toInt().coerceIn(0, 3)) }
    var signalChannel by remember {
        mutableIntStateOf(
            state.value.signalSubstanceChannelIndex.toInt()
                .coerceIn(0, GenomeContract.SIGNAL_INPUT_COUNT - 1)
        )
    }
    val decoded = remember(state.value, state.minimum, state.maximum) {
        GenomeParametricEditor.decode(state.value, state.minimum, state.maximum)
    }
    var fixedValue by remember(decoded) { mutableFloatStateOf(decoded.fixedValue) }
    var variableA by remember(decoded) { mutableFloatStateOf(decoded.variableA) }
    var variableB by remember(decoded) { mutableFloatStateOf(decoded.variableB) }
    var variableC by remember(decoded) { mutableFloatStateOf(decoded.variableC) }
    val steps = if (snapValues) GenomeEditorConfig.COARSE_STEPS else GenomeEditorConfig.FINE_STEPS
    val signalNames = signalInputNames()
    val bounds = GenomeParametricEditor.bounds(formulaType, state.minimum, state.maximum)
    val editorValues = GenomeParametricEditorValues(
        fixedValue = fixedValue.coerceIn(state.minimum, state.maximum),
        variableA = variableA.coerceIn(bounds.variableA.min, bounds.variableA.max),
        variableB = variableB.coerceIn(bounds.variableB.min, bounds.variableB.max),
        variableC = variableC.coerceIn(bounds.variableC.min, bounds.variableC.max)
    )

    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(text = stringResource(Res.string.genome_editor_cell_state_dependence_dialog_title))
        },
        text = {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
            ) {
                ChoiceButton(
                    label = if (formulaType == 0) {
                        stringResource(Res.string.genome_editor_value_dialog_fixed_value_label)
                    } else {
                        stringResource(Res.string.genome_editor_value_dialog_use_cell_state_label)
                    },
                    choices = listOf(
                        stringResource(Res.string.genome_editor_value_dialog_fixed_value_label),
                        stringResource(Res.string.genome_editor_value_dialog_use_cell_state_label)
                    ),
                    selected = if (formulaType == 0) 0 else 1,
                    onSelected = { selected ->
                        formulaType = if (selected == 0) 0 else formulaType.coerceAtLeast(1)
                    }
                )
                if (formulaType == 0) {
                    Spacer(modifier = Modifier.height(8.dp))
                    GenomeEditorSlider(
                        label = stringResource(Res.string.genome_editor_value_dialog_value_label),
                        value = editorValues.fixedValue.toDouble(),
                        minimum = state.minimum.toDouble(),
                        maximum = state.maximum.toDouble(),
                        steps = steps,
                        gestures = gestures,
                        onValueChange = { fixedValue = it.toFloat() }
                    )
                } else {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = stringResource(Res.string.genome_editor_value_dialog_input_label),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    ChoiceButton(
                        label = signalNames[signalChannel],
                        choices = signalNames,
                        selected = signalChannel,
                        onSelected = { signalChannel = it }
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = stringResource(Res.string.genome_editor_value_dialog_value_equals_label),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    ChoiceButton(
                        label = formulaLabel(formulaType),
                        choices = (1 until GenomeContract.FORMULA_TYPE_COUNT).map {
                            formulaLabel(it)
                        },
                        selected = formulaType - 1,
                        onSelected = { formulaType = it + 1 }
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    ParametricCurvePreview(
                        formulaType = formulaType,
                        values = editorValues,
                        minimum = state.minimum,
                        maximum = state.maximum
                    )
                    GenomeEditorSlider(
                        label = stringResource(Res.string.genome_editor_value_dialog_variable_a_label),
                        value = editorValues.variableA.toDouble(),
                        minimum = bounds.variableA.min.toDouble(),
                        maximum = bounds.variableA.max.toDouble(),
                        steps = steps,
                        gestures = gestures,
                        onValueChange = { variableA = it.toFloat() }
                    )
                    GenomeEditorSlider(
                        label = stringResource(Res.string.genome_editor_value_dialog_variable_b_label),
                        value = editorValues.variableB.toDouble(),
                        minimum = bounds.variableB.min.toDouble(),
                        maximum = bounds.variableB.max.toDouble(),
                        steps = steps,
                        gestures = gestures,
                        onValueChange = { variableB = it.toFloat() }
                    )
                    if (formulaType == 2 || formulaType == 3) {
                        GenomeEditorSlider(
                            label = if (formulaType == 3) {
                                stringResource(Res.string.genome_editor_variable_x_symbol)
                            } else {
                                stringResource(Res.string.genome_editor_value_dialog_variable_c_label)
                            },
                            value = editorValues.variableC.toDouble(),
                            minimum = bounds.variableC.min.toDouble(),
                            maximum = bounds.variableC.max.toDouble(),
                            steps = steps,
                            gestures = gestures,
                            onValueChange = { variableC = it.toFloat() }
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    onConfirm(
                        GenomeParametricEditor.encode(
                            formulaType = formulaType,
                            signalChannel = signalChannel,
                            values = editorValues,
                            minimum = state.minimum,
                            maximum = state.maximum
                        )
                    )
                }
            ) {
                Text(stringResource(Res.string.common_ok_action))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.common_cancel_action))
            }
        }
    )
}

@Composable
private fun ParametricCurvePreview(
    formulaType: Int,
    values: GenomeParametricEditorValues,
    minimum: Float,
    maximum: Float
) {
    val curveColor = MaterialTheme.colorScheme.primary
    val fillColor = curveColor.copy(alpha = 0.16f)
    val gridColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.45f)
    val backgroundColor = MaterialTheme.colorScheme.background
    Canvas(
        modifier = Modifier
            .fillMaxWidth()
            .height(112.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(backgroundColor)
            .border(1.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(8.dp))
            .padding(10.dp)
    ) {
        val left = 4.dp.toPx()
        val right = size.width - 4.dp.toPx()
        val top = 4.dp.toPx()
        val bottom = size.height - 4.dp.toPx()
        val width = (right - left).coerceAtLeast(1.0f)
        val height = (bottom - top).coerceAtLeast(1.0f)
        fun point(sample: Int, samples: Int): Offset {
            val input = sample.toFloat() / samples
            val output = GenomeParametricEditor.evaluatePhysical(
                formulaType,
                input,
                values,
                minimum,
                maximum
            )
            val normalized = ((output - minimum) / (maximum - minimum)).coerceIn(0.0f, 1.0f)
            return Offset(left + width * input, bottom - height * normalized)
        }

        drawLine(gridColor, Offset(left, top), Offset(right, top))
        drawLine(gridColor, Offset(left, bottom), Offset(right, bottom))
        drawLine(gridColor, Offset(left, top), Offset(left, bottom))
        drawLine(gridColor, Offset(right, top), Offset(right, bottom))
        drawLine(
            gridColor.copy(alpha = 0.5f),
            Offset(left, top + height * 0.5f),
            Offset(right, top + height * 0.5f)
        )
        val samples = 64
        val points = List(samples + 1) { point(it, samples) }
        val fillPath = Path().apply {
            moveTo(points.first().x, bottom)
            points.forEach { lineTo(it.x, it.y) }
            lineTo(points.last().x, bottom)
            close()
        }
        drawPath(fillPath, fillColor)
        points.zipWithNext().forEach { (first, second) ->
            drawLine(curveColor, first, second, strokeWidth = 2.5.dp.toPx())
        }
    }
}

@Composable
private fun ChoiceButton(
    label: String,
    choices: List<String>,
    selected: Int,
    onSelected: (Int) -> Unit
) {
    var expanded by remember { mutableStateOf(false) }
    Box(modifier = Modifier.fillMaxWidth()) {
        GenomeEditorDropdownAnchor(
            text = label,
            onClick = { expanded = true },
            modifier = Modifier.fillMaxWidth(),
            fillAvailableWidth = true
        )
        GenomeEditorDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false }
        ) {
            choices.forEachIndexed { index, choice ->
                GenomeEditorDropdownMenuItem(
                    text = choice,
                    selected = index == selected,
                    onClick = {
                        expanded = false
                        onSelected(index)
                    }
                )
            }
        }
    }
}

@Composable
private fun formulaLabel(type: Int): String = when (type) {
    0 -> stringResource(Res.string.genome_editor_value_dialog_fixed_value_label)
    1 -> stringResource(Res.string.genome_editor_value_dialog_linear_formula)
    2 -> stringResource(Res.string.genome_editor_value_dialog_heaviside_formula)
    3 -> stringResource(Res.string.genome_editor_value_dialog_sine_formula)
    else -> stringResource(Res.string.genome_editor_unknown_formula_message)
}

@Composable
private fun signalInputNames(): List<String> = buildList {
    repeat(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) { index ->
        add(
            stringResource(
                Res.string.genome_editor_signal_name_format,
                stringResource(Res.string.cell_property_signal_substance_prefix),
                index + 1
            )
        )
    }
    add(stringResource(Res.string.genome_editor_value_dependency_cell_mass))
    add(stringResource(Res.string.genome_editor_value_dependency_cell_age))
    add(stringResource(Res.string.genome_editor_value_dependency_nitrogen_reserve))
    add(stringResource(Res.string.genome_editor_value_dependency_adhesin_connection_number))
}
