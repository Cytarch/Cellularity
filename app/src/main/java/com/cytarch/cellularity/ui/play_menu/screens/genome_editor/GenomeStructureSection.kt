package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.generated.resources.*
import org.jetbrains.compose.resources.stringResource

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.LinkConstants
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledCheckbox
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIntegerId
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeBooleanField
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeChild
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeChildRulesSectionState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorConfig
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeScalarField
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeStructureSectionState

@Composable
internal fun GenomeSplittingSection(
    state: GenomeStructureSectionState,
    childRules: GenomeChildRulesSectionState,
    gestures: GenomeEditorGestureCallbacks,
    onAddChildGene: (GenomeChild) -> Unit,
    onAction: (GenomeEditorAction) -> Unit
) {
    val steps = if (state.snapValues) {
        GenomeEditorConfig.COARSE_STEPS
    } else {
        GenomeEditorConfig.FINE_STEPS
    }
    val nanogramUnit = stringResource(Res.string.microscope_unit_nanogram)
    val angularStiffnessUnit = formatGenomeAngularStiffnessUnit(
        nanogramUnit = nanogramUnit,
        hourUnit = stringResource(Res.string.simulation_hour_abbreviation)
    )
    val maximumSplitMassNanograms = floorGenomeEditorDisplayBoundary(
        scaleGenomeEditorValue(
            CellDataConstants.MAX_SPLITTABLE_MASS.toDouble(),
            NANOGRAMS_PER_INTERNAL_MASS_UNIT
        )
    )

    if (state.supportsDivision) {
        DefaultCard {
            Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
                DefaultLabeledCheckbox(
                    label = stringResource(Res.string.genome_editor_divide_label),
                    checked = state.divisionEnabled,
                    onCheckedChange = {
                        onAction(GenomeEditorAction.SetDivisionEnabled(it))
                    }
                )
                if (state.divisionEnabled) {
                    DefaultLabeledCheckbox(
                        label = stringResource(Res.string.genome_editor_produce_adhesin_label),
                        checked = state.makeLink,
                        onCheckedChange = {
                            onAction(GenomeEditorAction.SetBoolean(GenomeBooleanField.MAKE_LINK, it))
                        }
                    )
                }
            }
        }
    }

    if (state.divisionEnabled) {
        GenomeEditorScaledUnitSlider(
            label = stringResource(Res.string.genome_editor_split_mass_label),
            value = state.splitMass.toDouble(),
            minimum = CellDataConstants.MIN_SPLIT_MASS.toDouble(),
            maximum = CellDataConstants.MAX_SPLITTABLE_MASS.toDouble(),
            displayScale = NANOGRAMS_PER_INTERNAL_MASS_UNIT,
            unit = nanogramUnit,
            steps = steps,
            maximumDisplayValue = maximumSplitMassNanograms,
            gestures = gestures,
            onValueChange = {
                onAction(GenomeEditorAction.SetScalar(GenomeScalarField.SPLIT_MASS, it.toFloat()))
            }
        )
        GenomeEditorScaledUnitSlider(
            label = stringResource(Res.string.genome_editor_split_ratio_label),
            value = 1.0 - state.splitRatio,
            minimum = CellDataConstants.MIN_RATIO.toDouble(),
            maximum = (1.0f - CellDataConstants.MIN_RATIO).toDouble(),
            displayScale = PERCENT_PER_FRACTION,
            unit = "%",
            separateUnit = false,
            steps = steps,
            gestures = gestures,
            onValueChange = {
                onAction(
                    GenomeEditorAction.SetScalar(
                        GenomeScalarField.SPLIT_RATIO,
                        1.0f - it.toFloat()
                    )
                )
            }
        )
        GenomeEditorAngleSlider(
            label = stringResource(Res.string.genome_editor_split_angle_label),
            radians = state.splitAngle.toDouble(),
            steps = steps,
            gestures = gestures,
            onRadiansChange = {
                onAction(GenomeEditorAction.SetScalar(GenomeScalarField.SPLIT_ANGLE, it.toFloat()))
            }
        )

        GenomeChildRulesSection(
            state = childRules,
            snapValues = state.snapValues,
            gestures = gestures,
            onAddGene = onAddChildGene,
            onAction = onAction
        )
    }
    if (state.divisionEnabled && state.makeLink) {
        GenomeEditorScaledUnitSlider(
            label = stringResource(Res.string.genome_editor_adhesin_stiffness_label),
            value = state.linkStrength.toDouble(),
            minimum = 0.0,
            maximum = LinkConstants.MAX_STIFFNESS.toDouble(),
            displayScale = NANOGRAMS_PER_INTERNAL_STIFFNESS_UNIT,
            unit = angularStiffnessUnit,
            steps = steps,
            gestures = gestures,
            onValueChange = {
                onAction(GenomeEditorAction.SetScalar(GenomeScalarField.LINK_STRENGTH, it.toFloat()))
            }
        )
        GenomeEditorScaledUnitSlider(
            label = stringResource(Res.string.genome_editor_adhesin_length_label),
            value = state.adhesinLength.toDouble(),
            minimum = GenomeContract.floatRange(GeneFloatType.ADHESIN_LENGTH).min.toDouble(),
            maximum = GenomeContract.floatRange(GeneFloatType.ADHESIN_LENGTH).max.toDouble(),
            displayScale = MICROMETRES_PER_MILLIMETRE,
            unit = stringResource(Res.string.microscope_unit_micrometre),
            steps = steps,
            gestures = gestures,
            onValueChange = {
                onAction(GenomeEditorAction.SetFloat(GeneFloatType.ADHESIN_LENGTH, it.toFloat()))
            }
        )
    }

    if (state.divisionEnabled) {
        DefaultCard {
            Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
                GenomeIntegerDropdown(
                    label = stringResource(Res.string.genome_editor_maximum_adhesin_connections_label),
                    selectedIndex = state.maxConnections,
                    names = List(
                        GenomeContract.integerUpperBounds[GenomeContract.MAX_CONNECTION_COUNT_INDEX]
                    ) { (it + 1).toString() },
                    onSelected = {
                        onAction(
                            GenomeEditorAction.SetInteger(
                                GeneIntegerId(GenomeContract.MAX_CONNECTION_COUNT_INDEX),
                                it
                            )
                        )
                    }
                )
            }
        }
    }
}
