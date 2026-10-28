/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeCellSpecificValue
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorSection
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorSections
import com.cytarch.cellularity.shared.ui.play.genome_editor.sections.GenomeCellSpecificSection
import com.cytarch.cellularity.shared.ui.play.genome_editor.sections.GenomeEditorGestureCallbacks
import com.cytarch.cellularity.shared.ui.play.genome_editor.sections.GenomeGeneralSection
import com.cytarch.cellularity.shared.ui.play.genome_editor.sections.GenomeParametricValueEditor
import com.cytarch.cellularity.shared.ui.play.genome_editor.sections.GenomeSignalsSection
import com.cytarch.cellularity.shared.ui.play.genome_editor.sections.GenomeSplittingSection

/**
 * The genome editor, on the shared reducer and the shared sections.
 *
 * The editor is the same one the phone shows rather than a desktop reading of it: the
 * sections are the phone's, and every edit is dispatched to the shared reducer, so what
 * makes a genome valid and what an edit means are decided in one place for both.
 *
 * It is also the only part of the phone's editor that could be reused at all, because
 * everything else about it is Android: the preview renders through a native worker that
 * wants a platform surface, and the file operations go through a repository interface
 * that only Android implements. Those are called out where they would appear rather than
 * papered over, so the gap is visible in the interface instead of in a comment.
 */
@Composable
fun DesktopGenomeEditor(
    store: DesktopGenomeEditorStore,
    modifier: Modifier = Modifier
) {
    val state = store.state
    val gestures = remember { GenomeEditorGestureCallbacks() }
    var geneListWidth by remember { mutableStateOf(220.dp) }

    Row(modifier = modifier.fillMaxSize()) {
        GeneList(
            store = store,
            width = geneListWidth
        )

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            GeneToolbar(store)
            SectionBar(store)

            when (state.selectedSection) {
                GenomeEditorSection.GENERAL -> GenomeGeneralSection(
                    overview = GenomeEditorSections.overview(state),
                    structure = GenomeEditorSections.structure(state),
                    gestures = gestures,
                    onAction = store::dispatch
                )

                GenomeEditorSection.SPLITTING -> {
                    GenomeSplittingSection(
                        state = GenomeEditorSections.structure(state),
                        childRules = GenomeEditorSections.childRules(state),
                        gestures = gestures,
                        onAddChildGene = { child ->
                            store.dispatch(
                                GenomeEditorAction.AddChildGene(
                                    child = child,
                                    value = DesktopGenomeEditorStore.createRandomizedEditorGene(
                                        state.genome.genes.size
                                    )
                                )
                            )
                        },
                        onAction = store::dispatch
                    )
                    GenomeSignalsSection(
                        state = GenomeEditorSections.signals(state),
                        snapValues = state.snapValues,
                        gestures = gestures,
                        onAction = store::dispatch
                    )
                }

                GenomeEditorSection.SPECIFIC -> {
                    GenomeCellSpecificSection(
                        state = GenomeEditorSections.cellSpecific(state),
                        snapValues = state.snapValues,
                        gestures = gestures,
                        onAction = store::dispatch
                    )
                    ParametricEditors(store)
                }
            }

            PendingWorkNotice(store)
        }
    }
}

/** The genes, with the initial one marked, because it is the one a plate starts from. */
@Composable
private fun GeneList(store: DesktopGenomeEditorStore, width: androidx.compose.ui.unit.Dp) {
    val state = store.state
    val catalog = remember(state.genome) { GenomeEditorSections.geneCatalog(state) }

    Column(
        modifier = Modifier
            .width(width)
            .fillMaxHeight()
            .background(MaterialTheme.colorScheme.surfaceVariant)
    ) {
        Text(
            text = "Genes (${state.genome.genes.size})",
            modifier = Modifier.padding(12.dp),
            style = MaterialTheme.typography.titleSmall
        )
        LazyColumn(modifier = Modifier.fillMaxSize()) {
            items(catalog.options.size) { index ->
                val option = catalog.options[index]
                val selected = index == state.selectedGene.value
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { store.dispatch(GenomeEditorAction.SelectGene(GeneIndex(index))) }
                        .background(
                            if (selected) MaterialTheme.colorScheme.secondaryContainer
                            else Color.Transparent
                        )
                        .padding(horizontal = 12.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .width(12.dp)
                            .height(12.dp)
                            .background(swatchColor(option.color))
                    )
                    Text(
                        text = buildString {
                            append(index + 1)
                            append(". ")
                            append(option.cellType.name.lowercase())
                            if (state.genome.genes[index].initial) append("  (initial)")
                        },
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        }
    }
}

/**
 * The operations on the gene list itself.
 *
 * The sections below edit the selected gene; without these there is no way to reach a
 * second gene, so a one-gene genome could be tuned but never grown. They mirror what the
 * phone offers per gene, including that adding is refused at the ceiling rather than
 * attempted and rejected by the reducer.
 *
 * Deleting is deliberately not a plain removal. A genome is an ordered list whose genes
 * refer to each other by position, so removing one would shift every later gene and
 * silently repoint the references that named them. The reducer instead needs to be told
 * which surviving gene inherits the references, and the honest answer for a delete here is
 * "none of them": the slot is filled with a fresh gene and that gene takes the references.
 * That keeps the order stable, which is what makes the gene list still mean what it said.
 */
@Composable
private fun GeneToolbar(store: DesktopGenomeEditorStore) {
    val state = store.state
    val selected = state.selectedGene
    val canAdd = state.genome.genes.size < GenomeContract.MAX_GENE_COUNT
    val canDelete = state.genome.genes.size > GenomeContract.MIN_GENE_COUNT
    val canMoveLeft = selected.value > 0
    val canMoveRight = selected.value < state.genome.genes.size - 1

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        ToolbarAction("add gene", enabled = canAdd) {
            store.dispatch(
                GenomeEditorAction.AddGene(
                    after = selected,
                    value = DesktopGenomeEditorStore.createRandomizedEditorGene(
                        selected.value + 1
                    )
                )
            )
        }
        ToolbarAction("duplicate", enabled = canAdd) {
            store.dispatch(GenomeEditorAction.DuplicateGene(selected))
        }
        ToolbarAction("replace", enabled = true) {
            store.dispatch(
                GenomeEditorAction.ResetGene(
                    gene = selected,
                    value = DesktopGenomeEditorStore.createRandomizedEditorGene(selected.value)
                )
            )
        }
        ToolbarAction("delete", enabled = canDelete) {
            store.deleteGene(selected)
        }
        ToolbarAction("make initial", enabled = !state.genome.genes[selected.value].initial) {
            store.dispatch(GenomeEditorAction.MakeInitial(selected))
        }
        ToolbarAction("move left", enabled = canMoveLeft) {
            store.dispatch(GenomeEditorAction.MoveGene(selected, GeneIndex(selected.value - 1)))
        }
        ToolbarAction("move right", enabled = canMoveRight) {
            store.dispatch(
                GenomeEditorAction.MoveGene(selected, GeneIndex(selected.value + 1))
            )
        }
    }
}

@Composable
private fun ToolbarAction(label: String, enabled: Boolean, onClick: () -> Unit) {
    Text(
        text = label,
        modifier = Modifier
            .clickable(enabled = enabled, onClick = onClick)
            .background(
                // An enabled action and a refused one have to look different, or the
                // toolbar reads as "these all work" and a click that does nothing looks
                // like a broken button rather than a rule.
                if (enabled) MaterialTheme.colorScheme.secondaryContainer
                else MaterialTheme.colorScheme.surface
            )
            .padding(horizontal = 10.dp, vertical = 6.dp),
        style = MaterialTheme.typography.labelMedium,
        color = if (enabled) MaterialTheme.colorScheme.onSecondaryContainer
        else MaterialTheme.colorScheme.outline
    )
}

@Composable
private fun SectionBar(store: DesktopGenomeEditorStore) {
    val state = store.state
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        GenomeEditorSection.entries.forEach { section ->
            val selected = section == state.selectedSection
            Text(
                text = section.name.lowercase(),
                modifier = Modifier
                    .clickable { store.dispatch(GenomeEditorAction.SelectSection(section)) }
                    .background(
                        if (selected) MaterialTheme.colorScheme.secondaryContainer
                        else MaterialTheme.colorScheme.surfaceVariant
                    )
                    .padding(horizontal = 12.dp, vertical = 6.dp),
                style = MaterialTheme.typography.labelLarge
            )
        }
    }
}

/** The parametric values, which the cell-specific section only names. */
@Composable
private fun ParametricEditors(store: DesktopGenomeEditorStore) {
    val state = store.state
    val values = GenomeEditorSections.cellSpecific(state).values
    val gestures = remember { GenomeEditorGestureCallbacks() }
    values.forEach { value ->
        if (value is GenomeCellSpecificValue.Parametric) {
            GenomeParametricValueEditor(
                state = value.value,
                label = value.parameter.name.lowercase(),
                snapValues = state.snapValues,
                gestures = gestures,
                onValueChange = { draft ->
                    store.dispatch(
                        GenomeEditorAction.SetParametricValue(value.value.id, draft)
                    )
                }
            )
        }
    }
}

/**
 * Says what the editor cannot do here yet.
 *
 * Saving and loading a genome, and previewing it, are the two things the phone's editor
 * does that this one does not. Both are reported rather than silently missing: an editor
 * that looks complete and quietly discards a genome is worse than one that says it cannot
 * keep it.
 */
@Composable
private fun PendingWorkNotice(store: DesktopGenomeEditorStore) {
    DefaultCard {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(
                text = if (store.state.dirty) {
                    "This genome has edits that have not been saved."
                } else {
                    "This genome is unchanged since it was created."
                },
                style = MaterialTheme.typography.bodySmall
            )
            Text(
                text = "Saving and loading genomes, and the live preview, are not available " +
                    "in the desktop build yet.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

/** A gene's colour as a strip, so the list says which gene is which at a glance. */
private fun swatchColor(color: List<Float>): Color = Color(
    red = (color.getOrElse(0) { 1.0f }).coerceIn(0.0f, 1.0f),
    green = (color.getOrElse(1) { 0.0f }).coerceIn(0.0f, 1.0f),
    blue = (color.getOrElse(2) { 0.0f }).coerceIn(0.0f, 1.0f)
)


