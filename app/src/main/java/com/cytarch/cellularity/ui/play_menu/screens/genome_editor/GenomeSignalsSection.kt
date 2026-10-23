package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.generated.resources.*
import org.jetbrains.compose.resources.stringResource
import org.jetbrains.compose.resources.pluralStringResource

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
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
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorConfig
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeIntegerField
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeOscillatorPreset
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeOscillatorPresetConfiguration
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeOscillatorPresetSource
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeParametricValueState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeSignalsSectionState
import java.util.Locale

@Composable
internal fun GenomeSignalsSection(
    state: GenomeSignalsSectionState,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onAction: (GenomeEditorAction) -> Unit
) {
    when {
        state.showsSensing -> SensingControls(state, snapValues, gestures, onAction)
        state.showsSecretion -> SecretionControls(state, onAction)
        state.showsNeuralOutputs -> NeuralOutputControls(state, snapValues, gestures, onAction)
        else -> GenomeEditorSectionMessage(
            stringResource(Res.string.genome_editor_no_signal_settings_for_cell_type_message)
        )
    }
}

@Composable
private fun SensingControls(
    state: GenomeSignalsSectionState,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onAction: (GenomeEditorAction) -> Unit
) {
    val smellNames = MutableList(CellDataConstants.SMELL_TYPE_COUNT) { "" }
    smellNames[CellDataConstants.SMELL_TYPE_CELL] = stringResource(Res.string.cell_property_sense_cell)
    smellNames[CellDataConstants.SMELL_TYPE_FOOD] = stringResource(Res.string.cell_property_sense_nutrient)
    smellNames[CellDataConstants.SMELL_TYPE_FAT] =
        stringResource(Res.string.cell_property_sense_coated_nutrient)
    smellNames[CellDataConstants.SMELL_TYPE_LIGHT] = stringResource(Res.string.cell_property_sense_light)
    smellNames[CellDataConstants.SMELL_TYPE_VELOCITY] =
        stringResource(Res.string.cell_property_sense_velocity)
    smellNames[CellDataConstants.SMELL_TYPE_WALL] = stringResource(Res.string.cell_property_sense_wall)
    val signalNames = signalNames()
    DefaultCard {
        Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
            GenomeIntegerDropdown(
                label = stringResource(Res.string.genome_editor_sense_type_label),
                selectedIndex = state.smellType,
                names = smellNames,
                onSelected = {
                    onAction(
                        GenomeEditorAction.SetInteger(GenomeIntegerField.SMELL_TYPE.id, it)
                    )
                }
            )
            GenomeIntegerDropdown(
                label = stringResource(Res.string.genome_editor_output_channel_label),
                selectedIndex = state.smellOutputChannel,
                names = signalNames,
                onSelected = {
                    onAction(
                        GenomeEditorAction.SetInteger(GenomeIntegerField.SMELL_OUTPUT.id, it)
                    )
                }
            )
        }
    }
    SignalFloatSlider(
        label = stringResource(Res.string.genome_editor_output_amount_label),
        type = GeneFloatType.SMELL_AMPLITUDE,
        value = state.smellAmplitude,
        snapValues = snapValues,
        gestures = gestures,
        onAction = onAction
    )
    if (state.showsCellColorFilter) {
        SignalFloatSlider(
            label = stringResource(Res.string.genome_editor_sense_red_label),
            type = GeneFloatType.SMELL_RED,
            value = state.smellRed,
            snapValues = snapValues,
            gestures = gestures,
            onAction = onAction
        )
        SignalFloatSlider(
            label = stringResource(Res.string.genome_editor_sense_green_label),
            type = GeneFloatType.SMELL_GREEN,
            value = state.smellGreen,
            snapValues = snapValues,
            gestures = gestures,
            onAction = onAction
        )
        SignalFloatSlider(
            label = stringResource(Res.string.genome_editor_sense_blue_label),
            type = GeneFloatType.SMELL_BLUE,
            value = state.smellBlue,
            snapValues = snapValues,
            gestures = gestures,
            onAction = onAction
        )
        SignalFloatSlider(
            label = stringResource(Res.string.genome_editor_sense_color_threshold_label),
            type = GeneFloatType.SMELL_THRESHOLD,
            value = state.smellThreshold,
            snapValues = snapValues,
            gestures = gestures,
            onAction = onAction
        )
    }
}

@Composable
private fun SecretionControls(
    state: GenomeSignalsSectionState,
    onAction: (GenomeEditorAction) -> Unit
) {
    DefaultCard {
        Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
            GenomeIntegerDropdown(
                label = stringResource(Res.string.genome_editor_secretion_label),
                selectedIndex = state.secretionType,
                names = secretionNames(),
                onSelected = {
                    onAction(
                        GenomeEditorAction.SetInteger(GenomeIntegerField.SECRETION_TYPE.id, it)
                    )
                }
            )
        }
    }
}

@Composable
private fun NeuralOutputControls(
    state: GenomeSignalsSectionState,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onAction: (GenomeEditorAction) -> Unit
) {
    var showOscillatorPreset by remember { mutableStateOf(false) }
    val signalNames = signalNames()
    state.oscillatorPresetSource?.let { source ->
        Button(
            onClick = { showOscillatorPreset = true },
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(stringResource(Res.string.genome_editor_oscillator_preset_action))
        }
        if (showOscillatorPreset) {
            OscillatorPresetDialog(
                source = source,
                onConfirm = {
                    onAction(GenomeEditorAction.ApplyOscillatorPreset(it))
                    showOscillatorPreset = false
                },
                onDismiss = { showOscillatorPreset = false }
            )
        }
    }
    state.neuralOutputs.forEach { output ->
        DefaultCard {
            Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
                Text(
                    text = stringResource(
                        Res.string.genome_editor_numbered_label_format,
                        stringResource(Res.string.genome_editor_output_channel_label),
                        output.outputIndex + 1
                    ),
                    color = MaterialTheme.colorScheme.tertiary,
                    fontWeight = FontWeight.Bold
                )
                GenomeIntegerDropdown(
                    label = stringResource(Res.string.genome_editor_output_channel_label),
                    selectedIndex = output.channel,
                    names = signalNames,
                    onSelected = {
                        onAction(GenomeEditorAction.SetInteger(output.channelField.id, it))
                    }
                )
            }
        }
        GenomeParametricValueEditor(
            state = GenomeParametricValueState(
                id = output.valueId,
                value = output.value,
                minimum = GenomeContract.parametricMinimums[output.valueId.value],
                maximum = GenomeContract.parametricMaximums[output.valueId.value]
            ),
            label = stringResource(
                Res.string.genome_editor_numbered_label_format,
                stringResource(Res.string.genome_editor_output_amount_label),
                output.outputIndex + 1
            ),
            snapValues = snapValues,
            gestures = gestures,
            onValueChange = {
                onAction(GenomeEditorAction.SetParametricValue(output.valueId, it))
            }
        )
    }
}

@Composable
private fun OscillatorPresetDialog(
    source: GenomeOscillatorPresetSource,
    onConfirm: (GenomeOscillatorPresetConfiguration) -> Unit,
    onDismiss: () -> Unit
) {
    val initial = remember(source) { GenomeOscillatorPreset.configurationFor(source) }
    val channelPairs = remember {
        buildList {
            repeat(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) { first ->
                repeat(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) { second ->
                    if (first != second) add(first to second)
                }
            }
        }
    }
    var connections by remember(initial) { mutableIntStateOf(initial.adhesinConnections) }
    var selectedPair by remember(initial, channelPairs) {
        mutableIntStateOf(
            channelPairs.indexOf(initial.firstChannel to initial.secondChannel).coerceAtLeast(0)
        )
    }
    var period by remember(initial) { mutableFloatStateOf(initial.periodHours) }
    val pairNames = channelPairs.map { (first, second) ->
        stringResource(
            Res.string.genome_editor_oscillator_channel_pair_format,
            first + 1,
            second + 1
        )
    }

    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(text = stringResource(Res.string.genome_editor_oscillator_preset_title))
        },
        text = {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
            ) {
                Text(text = stringResource(Res.string.genome_editor_oscillator_preset_description))
                Spacer(modifier = Modifier.height(12.dp))
                GenomeIntegerDropdown(
                    label = stringResource(Res.string.genome_editor_oscillator_preset_adhesin_connections_label),
                    selectedIndex = connections,
                    names = GenomeOscillatorPreset.CONNECTION_RANGE.map { value ->
                        pluralStringResource(
                            Res.plurals.genome_editor_adhesin_connection_count,
                            value,
                            value
                        )
                    },
                    onSelected = { connections = it }
                )
                GenomeIntegerDropdown(
                    label = stringResource(Res.string.genome_editor_oscillator_preset_channel_pair_label),
                    selectedIndex = selectedPair,
                    names = pairNames,
                    onSelected = { selectedPair = it }
                )
                Spacer(modifier = Modifier.height(8.dp))
                GenomeEditorSlider(
                    label = stringResource(
                        Res.string.genome_editor_label_with_unit_format,
                        stringResource(Res.string.genome_editor_oscillator_preset_period_label),
                        stringResource(Res.string.simulation_hour_abbreviation)
                    ),
                    value = period.toDouble(),
                    minimum = GenomeOscillatorPreset.MIN_PERIOD_HOURS.toDouble(),
                    maximum = GenomeOscillatorPreset.MAX_PERIOD_HOURS.toDouble(),
                    steps = GenomeEditorConfig.OSCILLATOR_PRESET_STEPS,
                    onValueChange = { period = it.toFloat() }
                )
                Text(
                    text = stringResource(
                        Res.string.genome_editor_value_with_unit_format,
                        String.format(Locale.getDefault(), "%.2f", period),
                        stringResource(Res.string.simulation_hour_abbreviation)
                    ),
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.SemiBold
                )
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    val pair = channelPairs[selectedPair]
                    onConfirm(
                        GenomeOscillatorPresetConfiguration(
                            adhesinConnections = connections,
                            firstChannel = pair.first,
                            secondChannel = pair.second,
                            periodHours = period
                        )
                    )
                }
            ) {
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
private fun SignalFloatSlider(
    label: String,
    type: GeneFloatType,
    value: Float,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onAction: (GenomeEditorAction) -> Unit
) {
    val range = GenomeContract.floatRange(type)
    GenomeEditorSlider(
        label = label,
        value = value.toDouble(),
        minimum = range.min.toDouble(),
        maximum = range.max.toDouble(),
        steps = if (snapValues) {
            GenomeEditorConfig.COARSE_STEPS
        } else {
            GenomeEditorConfig.FINE_STEPS
        },
        gestures = gestures,
        onValueChange = { onAction(GenomeEditorAction.SetFloat(type, it.toFloat())) }
    )
}

@Composable
private fun signalNames(): List<String> = List(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) {
    stringResource(
        Res.string.genome_editor_signal_name_format,
        stringResource(Res.string.cell_property_signal_substance_prefix),
        it + 1
    )
}

@Composable
private fun secretionNames(): List<String> = buildList {
    add(stringResource(Res.string.cell_property_secretion_nutrient_smell))
    add(stringResource(Res.string.cell_property_secretion_hcn))
    add(stringResource(Res.string.cell_property_secretion_lipase))
    add(stringResource(Res.string.cell_property_secretion_protease))
    add(stringResource(Res.string.cell_property_secretion_coated_nutrient_smell))
    add(stringResource(Res.string.cell_property_secretion_light_sense_activator))
    add(stringResource(Res.string.cell_property_secretion_wall_sense_activator))
    repeat(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) {
        add(
            stringResource(
                Res.string.genome_editor_signed_signal_name_format,
                stringResource(Res.string.genome_editor_decrease_symbol),
                stringResource(Res.string.cell_property_signal_substance_prefix),
                it + 1
            )
        )
    }
    repeat(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) {
        add(
            stringResource(
                Res.string.genome_editor_signed_signal_name_format,
                stringResource(Res.string.genome_editor_increase_symbol),
                stringResource(Res.string.cell_property_signal_substance_prefix),
                it + 1
            )
        )
    }
}
