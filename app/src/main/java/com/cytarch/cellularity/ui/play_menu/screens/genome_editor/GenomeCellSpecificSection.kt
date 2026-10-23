package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.generated.resources.*
import org.jetbrains.compose.resources.stringResource

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeCellSpecificParameter
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeCellSpecificSectionState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeCellSpecificValue
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction

@Composable
internal fun GenomeCellSpecificSection(
    state: GenomeCellSpecificSectionState,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onAction: (GenomeEditorAction) -> Unit
) {
    val infinity = stringResource(Res.string.genome_editor_infinity_symbol)
    if (state.values.isEmpty()) {
        GenomeEditorSectionMessage(
            stringResource(Res.string.genome_editor_no_specific_settings_for_cell_type_message)
        )
        return
    }
    state.values.forEach { value ->
        when (value) {
            is GenomeCellSpecificValue.GeneReference -> {
                DefaultCard {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = cellSpecificLabel(value.parameter),
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.weight(1f)
                        )
                        GenomeGeneDropdown(
                            genes = state.genes,
                            selected = value.selectedGene,
                            onSelected = {
                                onAction(GenomeEditorAction.SetInteger(value.field.id, it.value))
                            }
                        )
                    }
                }
            }
            is GenomeCellSpecificValue.Parametric -> GenomeParametricValueEditor(
                state = value.value,
                label = cellSpecificLabel(value.parameter),
                snapValues = snapValues,
                gestures = gestures,
                onValueChange = {
                    onAction(GenomeEditorAction.SetParametricValue(value.value.id, it))
                }
            )
            is GenomeCellSpecificValue.IntegerChoice -> {
                DefaultCard {
                    Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
                        GenomeIntegerDropdown(
                            label = cellSpecificLabel(value.parameter),
                            selectedIndex = value.selectedIndex,
                            names = GenomeContract.telomeraseLengths.map {
                                if (it < 0) infinity else it.toString()
                            },
                            onSelected = {
                                onAction(GenomeEditorAction.SetInteger(value.field.id, it))
                            }
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun cellSpecificLabel(parameter: GenomeCellSpecificParameter): String = when (parameter) {
    GenomeCellSpecificParameter.VIRUS_COPY_FROM ->
        stringResource(Res.string.genome_editor_virus_source_gene_label)
    GenomeCellSpecificParameter.VIRUS_COPY_TO ->
        stringResource(Res.string.genome_editor_virus_target_gene_label)
    GenomeCellSpecificParameter.SWIM_FORCE -> stringResource(Res.string.genome_editor_swim_force_label)
    GenomeCellSpecificParameter.DENSITY -> stringResource(Res.string.genome_editor_density_label)
    GenomeCellSpecificParameter.MYOCYTE_CONTRACTION ->
        stringResource(Res.string.cell_property_muscle_contraction)
    GenomeCellSpecificParameter.MYOCYTE_BENDING ->
        stringResource(Res.string.cell_property_muscle_bending)
    GenomeCellSpecificParameter.MYOCYTE_LIFTING ->
        stringResource(Res.string.cell_property_muscle_lifting)
    GenomeCellSpecificParameter.STEM_MODE_1 -> stringResource(Res.string.genome_editor_stem_path_1_mode_label)
    GenomeCellSpecificParameter.STEM_THRESHOLD_1 -> stringResource(Res.string.genome_editor_stem_path_1_signal_label)
    GenomeCellSpecificParameter.STEM_MODE_2 -> stringResource(Res.string.genome_editor_stem_path_2_mode_label)
    GenomeCellSpecificParameter.STEM_THRESHOLD_2 -> stringResource(Res.string.genome_editor_stem_path_2_signal_label)
    GenomeCellSpecificParameter.GAMETE_COMPATIBLE_MODE ->
        stringResource(Res.string.genome_editor_compatible_mode_label)
    GenomeCellSpecificParameter.GAMETE_MODE_AFTER_FERTILIZATION ->
        stringResource(Res.string.genome_editor_mode_after_fertilization_label)
    GenomeCellSpecificParameter.GAMETE_TELOMERASE_LENGTH ->
        stringResource(Res.string.genome_editor_telomeres_after_fertilization_label)
    GenomeCellSpecificParameter.CILIOCYTE_LEFT_SPEED ->
        stringResource(Res.string.genome_editor_ciliocyte_left_movement_speed_label)
    GenomeCellSpecificParameter.CILIOCYTE_RIGHT_SPEED ->
        stringResource(Res.string.genome_editor_ciliocyte_right_movement_speed_label)
}
