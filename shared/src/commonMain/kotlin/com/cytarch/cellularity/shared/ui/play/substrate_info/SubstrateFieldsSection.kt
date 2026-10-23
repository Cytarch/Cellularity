package com.cytarch.cellularity.shared.ui.play.substrate_info

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_back_action
import com.cytarch.cellularity.shared.generated.resources.common_edit_action
import com.cytarch.cellularity.shared.generated.resources.substrate_field_summary_format
import com.cytarch.cellularity.shared.model.EnvironmentFieldType
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.components.SliderCard
import kotlin.math.PI
import org.jetbrains.compose.resources.stringResource

internal fun environmentAngleRadiansToDegrees(radians: Double): Double =
    radians * 180.0 / PI

internal fun environmentAngleDegreesToRadians(degrees: Double): Double =
    degrees * PI / 180.0

internal fun environmentAngleRadiansToEditorDegrees(radians: Double): Double =
    environmentAngleRadiansToDegrees(radians).coerceIn(-180.0, 180.0)

@Composable
internal fun SubstrateFieldsSection(
    state: SubstrateFieldsSectionState,
    onFieldSelected: (EnvironmentFieldTarget) -> Unit
) {
    DefaultCard {
        Column(modifier = Modifier.padding(8.dp)) {
            FieldEntry(state.density) { onFieldSelected(EnvironmentFieldTarget.DENSITY) }
            FieldEntry(state.gravity) { onFieldSelected(EnvironmentFieldTarget.GRAVITY) }
            FieldEntry(state.salinity) { onFieldSelected(EnvironmentFieldTarget.SALINITY) }
            FieldEntry(state.foodLipidLayer) {
                onFieldSelected(EnvironmentFieldTarget.FOOD_LIPID_LAYER)
            }
        }
    }
}

@Composable
internal fun FieldEntry(
    state: SubstrateFieldUiState,
    onClick: () -> Unit
) {
    val typeName = stringResource(state.type.presentationDescriptor().labelResource)
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
                text = environmentFieldTitle(state.target),
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = stringResource(
                    Res.string.substrate_field_summary_format,
                    typeName,
                    formatTwoDecimals(state.strength)
                ),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
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
internal fun SubstrateFieldEditor(
    state: SubstrateFieldUiState,
    onBack: () -> Unit,
    onTypeChanged: (EnvironmentFieldType) -> Unit,
    onValueChanged: (DensityFieldValueType, Double) -> Unit,
    sliderValueFormatter: (Double) -> String,
    sliderInputValueFormatter: (Double) -> String,
    previewContent: @Composable (
        EnvironmentFieldTarget,
        SubstrateFieldUiState,
        (DensityFieldValueType, Double) -> Unit
    ) -> Unit,
    modifier: Modifier = Modifier
) {
    val controlsScrollState = rememberScrollState()
    DefaultCard(modifier = modifier) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(10.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                TextButton(onClick = onBack) {
                    Text(stringResource(Res.string.common_back_action))
                }
                Text(
                    text = environmentFieldTitle(state.target),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                Spacer(modifier = Modifier.width(64.dp))
            }

            previewContent(state.target, state, onValueChanged)
            Spacer(modifier = Modifier.height(10.dp))
            if (state.target != EnvironmentFieldTarget.GRAVITY) {
                FieldLegend(ranges = state.ranges)
                Spacer(modifier = Modifier.height(10.dp))
            }

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .verticalScroll(controlsScrollState)
            ) {
                if (state.target == EnvironmentFieldTarget.GRAVITY) {
                    VectorFieldModeRow(
                        selectedType = state.type as VectorFieldType,
                        onTypeChanged = onTypeChanged
                    )
                } else {
                    ScalarFieldModeRow(
                        selectedType = state.type as ScalarFieldType,
                        onTypeChanged = onTypeChanged
                    )
                    FieldValueSlider(
                        label = fieldComponentName(DensityFieldValueType.BASE),
                        value = state.base,
                        min = state.ranges.baseMin,
                        max = state.ranges.baseMax,
                        valueFormatter = sliderValueFormatter,
                        inputValueFormatter = sliderInputValueFormatter,
                        onValueChanged = { onValueChanged(DensityFieldValueType.BASE, it) }
                    )
                }
                FieldValueSlider(
                    label = fieldComponentName(DensityFieldValueType.STRENGTH),
                    value = state.strength,
                    min = state.ranges.strengthMin,
                    max = state.ranges.strengthMax,
                    valueFormatter = sliderValueFormatter,
                    inputValueFormatter = sliderInputValueFormatter,
                    onValueChanged = { onValueChanged(DensityFieldValueType.STRENGTH, it) }
                )
                if (state.target != EnvironmentFieldTarget.GRAVITY ||
                    state.type == VectorFieldType.LINEAR_DIRECTIONAL
                ) {
                    EnvironmentAngleSlider(
                        label = fieldComponentName(DensityFieldValueType.ANGLE),
                        radians = state.angle,
                        valueFormatter = sliderValueFormatter,
                        inputValueFormatter = sliderInputValueFormatter,
                        onRadiansChanged = {
                            onValueChanged(DensityFieldValueType.ANGLE, it)
                        }
                    )
                }
                if (state.target != EnvironmentFieldTarget.GRAVITY) {
                    FieldValueSlider(
                        label = fieldComponentName(DensityFieldValueType.OFFSET),
                        value = state.offset,
                        min = -1.0,
                        max = 1.0,
                        valueFormatter = sliderValueFormatter,
                        inputValueFormatter = sliderInputValueFormatter,
                        onValueChanged = { onValueChanged(DensityFieldValueType.OFFSET, it) }
                    )
                    FieldValueSlider(
                        label = fieldComponentName(DensityFieldValueType.SOFTNESS),
                        value = state.softness,
                        min = 0.0,
                        max = 1.0,
                        valueFormatter = sliderValueFormatter,
                        inputValueFormatter = sliderInputValueFormatter,
                        onValueChanged = { onValueChanged(DensityFieldValueType.SOFTNESS, it) }
                    )
                }
            }
        }
    }
}

@Composable
private fun FieldLegend(ranges: SubstrateFieldRanges) {
    val lowColor = Color(0xFF2E6BC7)
    val midColor = Color(0xFF8CB8AD)
    val highColor = Color(0xFFF5A340)
    val midValue = (ranges.baseMin + ranges.baseMax) * 0.5

    Column(modifier = Modifier.fillMaxWidth()) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(14.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(Brush.horizontalGradient(listOf(lowColor, midColor, highColor)))
        )
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 4.dp),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text = formatTwoDecimals(ranges.baseMin),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = formatTwoDecimals(midValue),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = formatTwoDecimals(ranges.baseMax),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun ScalarFieldModeRow(
    selectedType: ScalarFieldType,
    onTypeChanged: (ScalarFieldType) -> Unit
) {
    FieldModeRow(scalarFieldModePresentationDescriptors, selectedType, onTypeChanged)
}

@Composable
private fun VectorFieldModeRow(
    selectedType: VectorFieldType,
    onTypeChanged: (VectorFieldType) -> Unit
) {
    FieldModeRow(vectorFieldModePresentationDescriptors, selectedType, onTypeChanged)
}

@Composable
private fun <T : EnvironmentFieldType> FieldModeRow(
    modes: List<EnvironmentFieldModePresentationDescriptor<T>>,
    selectedType: T,
    onTypeChanged: (T) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        modes.forEach { descriptor ->
            TextButton(
                onClick = { onTypeChanged(descriptor.type) },
                modifier = Modifier.weight(1f)
            ) {
                Text(
                    text = stringResource(descriptor.labelResource),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    color = if (descriptor.type == selectedType) {
                        MaterialTheme.colorScheme.primary
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    }
                )
            }
        }
    }
}

@Composable
private fun FieldValueSlider(
    label: String,
    value: Double,
    min: Double,
    max: Double,
    valueFormatter: (Double) -> String,
    inputValueFormatter: (Double) -> String,
    onValueChanged: (Double) -> Unit
) {
    SliderCard(
        label = label,
        currentValue = value,
        min = min,
        max = max,
        maxSteps = ENVIRONMENT_SLIDER_STEPS,
        valueFormatter = valueFormatter,
        inputValueFormatter = inputValueFormatter,
        editIcon = { SliderCardEditIcon() },
        strings = sliderCardStrings(),
        commitOnValueChangeFinished = true,
        onValueChange = onValueChanged
    )
}

@Composable
private fun EnvironmentAngleSlider(
    label: String,
    radians: Double,
    valueFormatter: (Double) -> String,
    inputValueFormatter: (Double) -> String,
    onRadiansChanged: (Double) -> Unit
) {
    FieldValueSlider(
        label = label,
        value = environmentAngleRadiansToEditorDegrees(radians),
        min = -180.0,
        max = 180.0,
        valueFormatter = { "${valueFormatter(it)}°" },
        inputValueFormatter = inputValueFormatter,
        onValueChanged = { onRadiansChanged(environmentAngleDegreesToRadians(it)) }
    )
}

@Composable
private fun environmentFieldTitle(target: EnvironmentFieldTarget): String =
    stringResource(target.presentationDescriptor().titleResource)

@Composable
private fun fieldComponentName(component: DensityFieldValueType): String =
    stringResource(component.presentationDescriptor().labelResource)
