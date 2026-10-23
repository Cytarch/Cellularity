package com.cytarch.cellularity.shared.ui.play.substrate_info

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_back_action
import com.cytarch.cellularity.shared.generated.resources.common_edit_action
import com.cytarch.cellularity.shared.generated.resources.common_reset_action
import com.cytarch.cellularity.shared.generated.resources.common_search_action
import com.cytarch.cellularity.shared.generated.resources.simulation_cell_type_names
import com.cytarch.cellularity.shared.generated.resources.play_sample_gene_pool_label
import com.cytarch.cellularity.shared.generated.resources.substrate_cell_aging
import com.cytarch.cellularity.shared.generated.resources.substrate_contaminate_with_random_cells_action
import com.cytarch.cellularity.shared.generated.resources.substrate_gene_pool_heading_format
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_details_format
import com.cytarch.cellularity.shared.generated.resources.substrate_mutation_only_after_split
import com.cytarch.cellularity.shared.generated.resources.mutation_weights_title
import com.cytarch.cellularity.shared.generated.resources.mutation_weights_editor_summary_format
import com.cytarch.cellularity.shared.generated.resources.mutation_weights_summary_format
import com.cytarch.cellularity.shared.generated.resources.substrate_point_mutations_only
import com.cytarch.cellularity.shared.model.EnvironmentConstants
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledCheckbox
import com.cytarch.cellularity.shared.ui.components.DefaultSlider
import com.cytarch.cellularity.shared.ui.theme.cellularityOutlinedTextFieldColors
import org.jetbrains.compose.resources.stringArrayResource
import org.jetbrains.compose.resources.stringResource

@Composable
internal fun SubstrateEvolutionSection(
    state: SubstrateEvolutionSectionState,
    experimentalState: SubstrateExperimentalSectionState,
    showExperimentalFeatures: Boolean,
    onCheckboxChanged: (EnvironmentCheckboxType, Boolean) -> Unit,
    onSliderChanged: (SubstrateSliderType, Double) -> Unit,
    onMutationWeightsClick: () -> Unit,
    onGenePoolChanged: (Int, Boolean) -> Unit,
    sliderValueFormatter: (Double) -> String,
    sliderInputValueFormatter: (Double) -> String
) {
    DefaultCard {
        Column(modifier = Modifier.padding(8.dp)) {
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.substrate_contaminate_with_random_cells_action),
                checked = state.addRandom,
                onCheckedChange = { onCheckboxChanged(EnvironmentCheckboxType.ADD_RANDOM, it) }
            )
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.substrate_point_mutations_only),
                checked = state.onlyTweakMutations,
                onCheckedChange = { onCheckboxChanged(EnvironmentCheckboxType.TWEAK, it) }
            )
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.substrate_mutation_only_after_split),
                checked = state.mutationOnlyAfterSplit,
                onCheckedChange = {
                    onCheckboxChanged(EnvironmentCheckboxType.MUTATION_ONLY_AFTER_SPLIT, it)
                }
            )
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.substrate_cell_aging),
                checked = state.aging,
                onCheckedChange = { onCheckboxChanged(EnvironmentCheckboxType.AGING, it) }
            )
            MutationWeightsEntry(
                weights = state.mutationWeights,
                onClick = onMutationWeightsClick
            )
        }
    }

    EnvironmentSliderCards(
        sliders = listOf(state.radiation),
        onSliderChanged = onSliderChanged,
        sliderValueFormatter = sliderValueFormatter,
        sliderInputValueFormatter = sliderInputValueFormatter
    )

    if (showExperimentalFeatures) {
        EnvironmentSliderCards(
            sliders = experimentalState.sliders.filter {
                it.type != SubstrateSliderType.TEST_FOOD_DECAY
            },
            onSliderChanged = onSliderChanged,
            sliderValueFormatter = sliderValueFormatter,
            sliderInputValueFormatter = sliderInputValueFormatter
        )
    }

    DefaultCard {
        GenePoolSection(
            genePool = state.genePool,
            genePoolEnabled = state.genePoolEnabled,
            onGenePoolChanged = onGenePoolChanged
        )
    }
}

@Composable
private fun MutationWeightsEntry(
    weights: List<Double>,
    onClick: () -> Unit
) {
    val activeCount = weights.count { it > 0.0 }
    val totalWeight = weights.filter { it > 0.0 }.sum()
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = stringResource(Res.string.mutation_weights_title),
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = stringResource(
                    Res.string.mutation_weights_summary_format,
                    activeCount,
                    formatTwoDecimals(totalWeight)
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Text(
            text = stringResource(Res.string.common_edit_action),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.primary,
            fontWeight = FontWeight.Bold
        )
    }
}

@Composable
internal fun MutationWeightsEditor(
    weights: List<Double>,
    onBack: () -> Unit,
    onWeightChanged: (Int, Double) -> Unit,
    onReset: () -> Unit
) {
    var filterText by remember { mutableStateOf("") }
    val totalWeight = weights.filter { it > 0.0 }.sum()
    val activeCount = weights.count { it > 0.0 }

    DefaultCard {
        Column(modifier = Modifier.padding(10.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                TextButton(onClick = onBack) {
                    Text(stringResource(Res.string.common_back_action))
                }
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(
                        text = stringResource(Res.string.mutation_weights_title),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                    Text(
                        text = stringResource(
                            Res.string.mutation_weights_editor_summary_format,
                            activeCount,
                            formatTwoDecimals(totalWeight)
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                TextButton(onClick = onReset) {
                    Text(stringResource(Res.string.common_reset_action))
                }
            }

            OutlinedTextField(
                value = filterText,
                onValueChange = { filterText = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 8.dp),
                singleLine = true,
                label = { Text(stringResource(Res.string.common_search_action)) },
                colors = cellularityOutlinedTextFieldColors()
            )

            for (descriptor in mutationWeightDescriptors) {
                val localizedLabel = mutationWeightLabel(descriptor)
                if (descriptor.matchesFilter(localizedLabel, filterText)) {
                    MutationWeightRow(
                        descriptor = descriptor,
                        localizedLabel = localizedLabel,
                        weight = weights.getOrNull(descriptor.index) ?: 0.0,
                        totalWeight = totalWeight,
                        onWeightChanged = onWeightChanged
                    )
                }
            }
        }
    }
}

@Composable
private fun MutationWeightRow(
    descriptor: MutationWeightDescriptor,
    localizedLabel: String,
    weight: Double,
    totalWeight: Double,
    onWeightChanged: (Int, Double) -> Unit
) {
    val clampedWeight = weight.coerceIn(0.0, EnvironmentConstants.MAX_MUTATION_WEIGHT)
    val effectiveChance = if (totalWeight > 0.0 && weight > 0.0) {
        weight / totalWeight * 100.0
    } else {
        0.0
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Checkbox(
            checked = weight > 0.0,
            onCheckedChange = { checked ->
                onWeightChanged(descriptor.index, if (checked) 1.0 else 0.0)
            }
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = localizedLabel,
                style = MaterialTheme.typography.bodyMedium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Text(
                text = stringResource(
                    Res.string.mutation_weight_details_format,
                    formatTwoDecimals(weight),
                    formatTwoDecimals(effectiveChance)
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            DefaultSlider(
                value = clampedWeight.toFloat(),
                onValueChange = { onWeightChanged(descriptor.index, it.toDouble()) },
                valueRange = 0f..EnvironmentConstants.MAX_MUTATION_WEIGHT.toFloat(),
                modifier = Modifier.fillMaxWidth(),
                enabled = weight > 0.0 || clampedWeight > 0.0,
                commitOnValueChangeFinished = true
            )
        }
        Spacer(modifier = Modifier.width(8.dp))
        OutlinedTextField(
            value = formatTwoDecimals(weight),
            onValueChange = { text ->
                text.toDoubleOrNull()?.let { value ->
                    onWeightChanged(
                        descriptor.index,
                        value.coerceAtLeast(0.0)
                    )
                }
            },
            modifier = Modifier.width(86.dp),
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
            colors = cellularityOutlinedTextFieldColors()
        )
    }
}

@Composable
private fun GenePoolSection(
    genePool: List<Boolean>,
    genePoolEnabled: List<Boolean>,
    onGenePoolChanged: (Int, Boolean) -> Unit
) {
    val cellTypeNames = stringArrayResource(Res.array.simulation_cell_type_names)

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(12.dp)
    ) {
        Text(
            text = stringResource(
                Res.string.substrate_gene_pool_heading_format,
                stringResource(Res.string.play_sample_gene_pool_label)
            ),
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(bottom = 8.dp)
        )

        cellTypeNames.forEachIndexed { index, name ->
            val checked = genePool.getOrNull(index) ?: false
            val enabled = genePoolEnabled.getOrNull(index) ?: true
            DefaultLabeledCheckbox(
                label = name,
                checked = checked,
                enabled = enabled,
                onCheckedChange = { onGenePoolChanged(index, it) }
            )
        }
    }
}
