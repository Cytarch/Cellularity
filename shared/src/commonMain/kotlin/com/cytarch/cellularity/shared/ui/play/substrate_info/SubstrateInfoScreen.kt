package com.cytarch.cellularity.shared.ui.play.substrate_info

import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.simulation_hour_abbreviation
import com.cytarch.cellularity.shared.generated.resources.plate
import com.cytarch.cellularity.shared.generated.resources.substrate_edit_empty_gene_pool_message
import com.cytarch.cellularity.shared.generated.resources.substrate_edit_failed_message
import com.cytarch.cellularity.shared.generated.resources.substrate_edit_invalid_value_message
import com.cytarch.cellularity.shared.generated.resources.substrate_edit_pending_message
import com.cytarch.cellularity.shared.generated.resources.substrate_edit_rejected_message
import com.cytarch.cellularity.shared.generated.resources.substrate_edit_revision_conflict_message
import com.cytarch.cellularity.shared.generated.resources.substrate_environment_loading_message
import com.cytarch.cellularity.shared.generated.resources.play_plate_options_content_description
import com.cytarch.cellularity.shared.generated.resources.substrate_stats_summary_format
import com.cytarch.cellularity.shared.model.EnvironmentFieldType
import com.cytarch.cellularity.shared.ui.play.microscope.TemperatureSelectionDialog
import com.cytarch.cellularity.shared.ui.play.microscope.temperatureIcon
import com.cytarch.cellularity.shared.ui.play.microscope.temperatureTitle
import com.cytarch.cellularity.shared.ui.text.formatLocalizedNumber
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.compose.resources.stringResource

@Composable
fun SubstrateInfoScreen(
    state: SubstrateInfoState,
    environment: SubstrateEnvironmentUiState?,
    stats: SubstrateStatsSectionState,
    editStatus: SubstrateEditStatus,
    showExperimentalFeatures: Boolean,
    onPlateActionClick: () -> Unit,
    selectedTemperature: Int,
    onTemperatureSelected: (Int) -> Unit,
    onCheckboxChanged: (EnvironmentCheckboxType, Boolean) -> Unit,
    onSliderChanged: (SubstrateSliderType, Double) -> Unit,
    onEnvironmentFieldTypeChanged: (EnvironmentFieldTarget, EnvironmentFieldType) -> Unit,
    onEnvironmentFieldValueChanged: (EnvironmentFieldTarget, DensityFieldValueType, Double) -> Unit,
    onMutationWeightChanged: (Int, Double) -> Unit,
    onMutationWeightsReset: () -> Unit,
    onGenePoolChanged: (Int, Boolean) -> Unit,
    sliderValueFormatter: (Double) -> String,
    sliderInputValueFormatter: (Double) -> String,
    densityPreviewContent: @Composable (
        EnvironmentFieldTarget,
        SubstrateFieldUiState,
        (DensityFieldValueType, Double) -> Unit
    ) -> Unit = { _, _, _ -> },
    modifier: Modifier = Modifier
) {
    var showTemperatureSelectionDialog by remember { mutableStateOf(false) }

    if (showTemperatureSelectionDialog) {
        TemperatureSelectionDialog(
            onDismiss = { showTemperatureSelectionDialog = false },
            onSelected = { temperature ->
                onTemperatureSelected(temperature)
                showTemperatureSelectionDialog = false
            }
        )
    }

    val mechanicsScrollState = rememberSectionScrollState(SubstrateInfoSection.MECHANICS, state)
    val sourcesScrollState = rememberSectionScrollState(SubstrateInfoSection.SOURCES, state)
    val evolutionScrollState = rememberSectionScrollState(SubstrateInfoSection.EVOLUTION, state)
    val statsScrollState = rememberSectionScrollState(SubstrateInfoSection.STATS, state)
    val selectedSection = state.selectedSection
    val selectedScrollState = when (selectedSection) {
        SubstrateInfoSection.MECHANICS -> mechanicsScrollState
        SubstrateInfoSection.SOURCES -> sourcesScrollState
        SubstrateInfoSection.EVOLUTION -> evolutionScrollState
        SubstrateInfoSection.STATS -> statsScrollState
    }

    Column(modifier = modifier.fillMaxSize()) {
        SubstrateSummaryHeader(
            stats = stats,
            selectedTemperature = selectedTemperature,
            onPlateActionClick = onPlateActionClick,
            onTemperatureClick = { showTemperatureSelectionDialog = true }
        )

        SubstrateSectionBar(
            selectedSection = selectedSection,
            onSectionSelected = state::updateSelectedSection
        )

        EnvironmentEditFeedback(status = editStatus)

        val selectedField = state.editingFieldTarget?.let { target ->
            environment?.fields?.field(target)
        }
        when {
            selectedField != null -> {
                SubstrateFieldEditor(
                    state = selectedField,
                    onBack = state::closeFieldEditor,
                    onTypeChanged = { type ->
                        onEnvironmentFieldTypeChanged(selectedField.target, type)
                    },
                    onValueChanged = { component, value ->
                        onEnvironmentFieldValueChanged(selectedField.target, component, value)
                    },
                    sliderValueFormatter = sliderValueFormatter,
                    sliderInputValueFormatter = sliderInputValueFormatter,
                    previewContent = densityPreviewContent,
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(10.dp)
                )
            }

            selectedSection == SubstrateInfoSection.STATS -> {
                ScrollableSection(selectedScrollState) {
                    SubstrateStatsSection(stats)
                }
            }

            environment == null -> {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text(
                        text = stringResource(Res.string.substrate_environment_loading_message),
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            else -> {
                ScrollableSection(selectedScrollState) {
                    if (selectedSection == SubstrateInfoSection.EVOLUTION &&
                        state.mutationWeightsVisible
                    ) {
                        MutationWeightsEditor(
                            weights = environment.evolution.mutationWeights,
                            onBack = state::hideMutationWeights,
                            onWeightChanged = onMutationWeightChanged,
                            onReset = onMutationWeightsReset
                        )
                    } else {
                        when (selectedSection) {
                            SubstrateInfoSection.MECHANICS -> SubstrateMechanicsSection(
                                state = environment.mechanics,
                                fields = environment.fields,
                                onCheckboxChanged = onCheckboxChanged,
                                onFieldSelected = state::openFieldEditor,
                                onSliderChanged = onSliderChanged,
                                sliderValueFormatter = sliderValueFormatter,
                                sliderInputValueFormatter = sliderInputValueFormatter
                            )
                            SubstrateInfoSection.SOURCES -> SubstrateSourcesSection(
                                state = environment.nutrition,
                                fields = environment.fields,
                                experimentalState = environment.experimental,
                                showExperimentalFeatures = showExperimentalFeatures,
                                onCheckboxChanged = onCheckboxChanged,
                                onFieldSelected = state::openFieldEditor,
                                onSliderChanged = onSliderChanged,
                                sliderValueFormatter = sliderValueFormatter,
                                sliderInputValueFormatter = sliderInputValueFormatter
                            )
                            SubstrateInfoSection.EVOLUTION -> SubstrateEvolutionSection(
                                state = environment.evolution,
                                experimentalState = environment.experimental,
                                showExperimentalFeatures = showExperimentalFeatures,
                                onCheckboxChanged = onCheckboxChanged,
                                onSliderChanged = onSliderChanged,
                                onMutationWeightsClick = state::showMutationWeights,
                                onGenePoolChanged = onGenePoolChanged,
                                sliderValueFormatter = sliderValueFormatter,
                                sliderInputValueFormatter = sliderInputValueFormatter
                            )
                            SubstrateInfoSection.STATS -> Unit // Rendered before environment loading.
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun SubstrateSummaryHeader(
    stats: SubstrateStatsSectionState,
    selectedTemperature: Int,
    onPlateActionClick: () -> Unit,
    onTemperatureClick: () -> Unit
) {
    val hour = stringResource(Res.string.simulation_hour_abbreviation)
    val sampleInfoText = stringResource(
        Res.string.substrate_stats_summary_format,
        formatOneDecimal(stats.time),
        hour,
        stats.cellCount,
        formatOneDecimal(stats.foodMass)
    )

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surfaceVariant)
            .padding(10.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = sampleInfoText,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = onPlateActionClick) {
                    Icon(
                        painter = painterResource(Res.drawable.plate),
                        contentDescription = stringResource(
                            Res.string.play_plate_options_content_description
                        ),
                        tint = Color.Unspecified
                    )
                }
                IconButton(onClick = onTemperatureClick) {
                    Icon(
                        painter = painterResource(temperatureIcon(selectedTemperature)),
                        contentDescription = stringResource(temperatureTitle(selectedTemperature)),
                        tint = Color.Unspecified
                    )
                }
            }
        }
    }
}

@Composable
private fun EnvironmentEditFeedback(status: SubstrateEditStatus) {
    val kind = status.visibleFeedbackKind() ?: return
    val text = when (kind) {
        SubstrateEditFeedbackKind.PENDING -> stringResource(Res.string.substrate_edit_pending_message)
        SubstrateEditFeedbackKind.APPLIED -> return
        SubstrateEditFeedbackKind.REVISION_CONFLICT ->
            stringResource(Res.string.substrate_edit_revision_conflict_message)
        SubstrateEditFeedbackKind.INVALID_VALUE ->
            stringResource(Res.string.substrate_edit_invalid_value_message)
        SubstrateEditFeedbackKind.EMPTY_GENE_POOL ->
            stringResource(Res.string.substrate_edit_empty_gene_pool_message)
        SubstrateEditFeedbackKind.REJECTED -> stringResource(Res.string.substrate_edit_rejected_message)
        SubstrateEditFeedbackKind.FAILED -> stringResource(Res.string.substrate_edit_failed_message)
    }
    val error = kind in setOf(
        SubstrateEditFeedbackKind.REVISION_CONFLICT,
        SubstrateEditFeedbackKind.INVALID_VALUE,
        SubstrateEditFeedbackKind.EMPTY_GENE_POOL,
        SubstrateEditFeedbackKind.REJECTED,
        SubstrateEditFeedbackKind.FAILED
    )
    Surface(
        color = if (error) {
            MaterialTheme.colorScheme.errorContainer
        } else {
            MaterialTheme.colorScheme.secondaryContainer
        },
        contentColor = if (error) {
            MaterialTheme.colorScheme.onErrorContainer
        } else {
            MaterialTheme.colorScheme.onSecondaryContainer
        },
        modifier = Modifier.fillMaxWidth()
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp)
        )
    }
}

@Composable
private fun SubstrateSectionBar(
    selectedSection: SubstrateInfoSection,
    onSectionSelected: (SubstrateInfoSection) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.primaryContainer)
            .padding(horizontal = 8.dp, vertical = 7.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        SubstrateInfoSection.entries.forEach { section ->
            val selected = selectedSection == section
            Box(
                modifier = Modifier
                    .weight(1f)
                    .height(38.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(
                        if (selected) MaterialTheme.colorScheme.background else Color.Transparent
                    )
                    .clickable { onSectionSelected(section) }
                    .padding(horizontal = 4.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = stringResource(section.presentationDescriptor().titleResource),
                    color = if (selected) {
                        MaterialTheme.colorScheme.primary
                    } else {
                        MaterialTheme.colorScheme.background
                    },
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = if (selected) FontWeight.Bold else FontWeight.SemiBold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

@Composable
private fun ScrollableSection(
    scrollState: ScrollState,
    content: @Composable () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(10.dp)
    ) {
        content()
    }
}

@Composable
private fun rememberSectionScrollState(
    section: SubstrateInfoSection,
    state: SubstrateInfoState
): ScrollState {
    val scrollState = rememberScrollState(state.getScrollOffset(section))
    LaunchedEffect(section, state, scrollState) {
        snapshotFlow { scrollState.value }.collect { offset ->
            state.updateScrollOffset(section, offset)
        }
    }
    return scrollState
}

private fun formatOneDecimal(value: Double): String =
    formatLocalizedNumber(value, minimumFractionDigits = 1, maximumFractionDigits = 1)
