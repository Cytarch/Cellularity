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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledCheckbox
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeBooleanField
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeChild
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeChildRuleState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeChildRulesSectionState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorConfig
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeScalarField

@Composable
internal fun GenomeChildRulesSection(
    state: GenomeChildRulesSectionState,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onAddGene: (GenomeChild) -> Unit,
    onAction: (GenomeEditorAction) -> Unit
) {
    ChildRuleCard(state.first, state.genes, snapValues, gestures, onAddGene, onAction)
    ChildRuleCard(state.second, state.genes, snapValues, gestures, onAddGene, onAction)
}

@Composable
private fun ChildRuleCard(
    state: GenomeChildRuleState,
    genes: com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeGeneCatalog,
    snapValues: Boolean,
    gestures: GenomeEditorGestureCallbacks,
    onAddGene: (GenomeChild) -> Unit,
    onAction: (GenomeEditorAction) -> Unit
) {
    val isFirst = state.child == GenomeChild.FIRST
    val title = if (isFirst) {
        stringResource(Res.string.genome_editor_child_1_gene_label)
    } else {
        stringResource(Res.string.genome_editor_child_2_gene_label)
    }
    val keepField = if (isFirst) {
        GenomeBooleanField.CHILD_1_KEEP_LINKS
    } else {
        GenomeBooleanField.CHILD_2_KEEP_LINKS
    }
    val mirrorField = if (isFirst) {
        GenomeBooleanField.CHILD_1_MIRROR
    } else {
        GenomeBooleanField.CHILD_2_MIRROR
    }
    val angleField = if (isFirst) {
        GenomeScalarField.CHILD_1_ANGLE
    } else {
        GenomeScalarField.CHILD_2_ANGLE
    }

    DefaultCard {
        Column(modifier = Modifier.fillMaxWidth().padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = title,
                    color = MaterialTheme.colorScheme.tertiary,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f)
                )
                GenomeGeneDropdown(
                    genes = genes,
                    selected = state.targetGene,
                    onSelected = {
                        onAction(GenomeEditorAction.SetChildGene(state.child, it))
                    },
                    onAddGene = { onAddGene(state.child) },
                    canAddGene = genes.options.size < GenomeContract.MAX_GENE_COUNT
                )
            }
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.genome_editor_keep_adhesin_connections_label),
                checked = state.keepLinks,
                onCheckedChange = {
                    onAction(GenomeEditorAction.SetBoolean(keepField, it))
                }
            )
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.genome_editor_mirror_label),
                checked = state.mirror,
                onCheckedChange = {
                    onAction(GenomeEditorAction.SetBoolean(mirrorField, it))
                }
            )
            GenomeEditorAngleSlider(
                label = if (isFirst) {
                    stringResource(Res.string.genome_editor_child_1_angle_label)
                } else {
                    stringResource(Res.string.genome_editor_child_2_angle_label)
                },
                radians = state.angle.toDouble(),
                steps = if (snapValues) {
                    GenomeEditorConfig.COARSE_STEPS
                } else {
                    GenomeEditorConfig.FINE_STEPS
                },
                gestures = gestures,
                onRadiansChange = {
                    onAction(GenomeEditorAction.SetScalar(angleField, it.toFloat()))
                }
            )
        }
    }
}
