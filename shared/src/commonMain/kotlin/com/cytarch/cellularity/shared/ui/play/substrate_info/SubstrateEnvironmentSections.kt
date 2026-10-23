package com.cytarch.cellularity.shared.ui.play.substrate_info

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.common_edit_value_precisely_action
import com.cytarch.cellularity.shared.generated.resources.common_ok_action
import com.cytarch.cellularity.shared.generated.resources.common_cancel_action
import com.cytarch.cellularity.shared.generated.resources.simulation_hour_abbreviation
import com.cytarch.cellularity.shared.generated.resources.substrate_mobile_nutrients
import com.cytarch.cellularity.shared.generated.resources.play_sample_age_label
import com.cytarch.cellularity.shared.generated.resources.play_sample_cell_count_label
import com.cytarch.cellularity.shared.generated.resources.play_sample_nutrient_amount_label
import com.cytarch.cellularity.shared.generated.resources.settings_icon
import com.cytarch.cellularity.shared.generated.resources.common_invalid_number_message
import com.cytarch.cellularity.shared.generated.resources.substrate_density
import com.cytarch.cellularity.shared.generated.resources.substrate_density_gradient
import com.cytarch.cellularity.shared.generated.resources.substrate_nutrient_lumpiness
import com.cytarch.cellularity.shared.generated.resources.substrate_dynamic_friction
import com.cytarch.cellularity.shared.generated.resources.substrate_static_friction
import com.cytarch.cellularity.shared.generated.resources.substrate_gravity
import com.cytarch.cellularity.shared.generated.resources.substrate_kill_cells_at_edge
import com.cytarch.cellularity.shared.generated.resources.substrate_light_amount
import com.cytarch.cellularity.shared.generated.resources.substrate_light_range
import com.cytarch.cellularity.shared.generated.resources.substrate_light_direction_change
import com.cytarch.cellularity.shared.generated.resources.substrate_nitrates
import com.cytarch.cellularity.shared.generated.resources.substrate_nutrient_lump_size
import com.cytarch.cellularity.shared.generated.resources.substrate_nutrient_chunk_size
import com.cytarch.cellularity.shared.generated.resources.substrate_nutrient_rate
import com.cytarch.cellularity.shared.generated.resources.substrate_radiation_level
import com.cytarch.cellularity.shared.generated.resources.substrate_salinity
import com.cytarch.cellularity.shared.generated.resources.substrate_viscosity
import com.cytarch.cellularity.shared.generated.resources.substrate_test_flagellocyte_cost_scale
import com.cytarch.cellularity.shared.generated.resources.substrate_test_nutrient_decay
import com.cytarch.cellularity.shared.generated.resources.substrate_test_idle_metabolism_scale
import com.cytarch.cellularity.shared.generated.resources.substrate_test_multicellular_coefficient
import com.cytarch.cellularity.shared.generated.resources.substrate_test_mutation_coefficient
import com.cytarch.cellularity.shared.generated.resources.substrate_test_phagocyte_eating_speed_scale
import com.cytarch.cellularity.shared.generated.resources.substrate_time_value_format
import com.cytarch.cellularity.shared.generated.resources.substrate_nutrient_coating
import com.cytarch.cellularity.shared.ui.components.DefaultCard
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledCheckbox
import com.cytarch.cellularity.shared.ui.components.SliderCard
import com.cytarch.cellularity.shared.ui.components.SliderCardStrings
import com.cytarch.cellularity.shared.ui.text.formatLocalizedNumber
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.compose.resources.stringResource

internal const val ENVIRONMENT_SLIDER_STEPS = 1000

@Composable
internal fun SubstrateMechanicsSection(
    state: SubstrateMechanicsSectionState,
    fields: SubstrateFieldsSectionState,
    onCheckboxChanged: (EnvironmentCheckboxType, Boolean) -> Unit,
    onFieldSelected: (EnvironmentFieldTarget) -> Unit,
    onSliderChanged: (SubstrateSliderType, Double) -> Unit,
    sliderValueFormatter: (Double) -> String,
    sliderInputValueFormatter: (Double) -> String
) {
    DefaultCard {
        Column(modifier = Modifier.padding(8.dp)) {
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.substrate_kill_cells_at_edge),
                checked = state.dieEdge,
                onCheckedChange = { onCheckboxChanged(EnvironmentCheckboxType.DIE_EDGE, it) }
            )
            FieldEntry(fields.density) {
                onFieldSelected(EnvironmentFieldTarget.DENSITY)
            }
            FieldEntry(fields.gravity) {
                onFieldSelected(EnvironmentFieldTarget.GRAVITY)
            }
        }
    }
    EnvironmentSliderCards(
        sliders = state.sliders,
        onSliderChanged = onSliderChanged,
        sliderValueFormatter = sliderValueFormatter,
        sliderInputValueFormatter = sliderInputValueFormatter
    )
}

@Composable
internal fun SubstrateSourcesSection(
    state: SubstrateNutritionSectionState,
    fields: SubstrateFieldsSectionState,
    experimentalState: SubstrateExperimentalSectionState,
    showExperimentalFeatures: Boolean,
    onCheckboxChanged: (EnvironmentCheckboxType, Boolean) -> Unit,
    onFieldSelected: (EnvironmentFieldTarget) -> Unit,
    onSliderChanged: (SubstrateSliderType, Double) -> Unit,
    sliderValueFormatter: (Double) -> String,
    sliderInputValueFormatter: (Double) -> String
) {
    DefaultCard {
        Column(modifier = Modifier.padding(8.dp)) {
            DefaultLabeledCheckbox(
                label = stringResource(Res.string.substrate_mobile_nutrients),
                checked = state.mobileFood,
                onCheckedChange = { onCheckboxChanged(EnvironmentCheckboxType.MOBILE_FOOD, it) }
            )
            FieldEntry(fields.salinity) {
                onFieldSelected(EnvironmentFieldTarget.SALINITY)
            }
            FieldEntry(fields.foodLipidLayer) {
                onFieldSelected(EnvironmentFieldTarget.FOOD_LIPID_LAYER)
            }
        }
    }
    if (showExperimentalFeatures) {
        EnvironmentSliderCards(
            sliders = experimentalState.sliders.filter {
                it.type == SubstrateSliderType.TEST_FOOD_DECAY
            },
            onSliderChanged = onSliderChanged,
            sliderValueFormatter = sliderValueFormatter,
            sliderInputValueFormatter = sliderInputValueFormatter
        )
    }
    EnvironmentSliderCards(
        sliders = state.sliders,
        onSliderChanged = onSliderChanged,
        sliderValueFormatter = sliderValueFormatter,
        sliderInputValueFormatter = sliderInputValueFormatter
    )
}

@Composable
internal fun SubstrateStatsSection(state: SubstrateStatsSectionState) {
    DefaultCard {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            val hour = stringResource(Res.string.simulation_hour_abbreviation)
            StatRow(
                label = stringResource(Res.string.play_sample_age_label).trim(),
                value = stringResource(
                    Res.string.substrate_time_value_format,
                    formatOneDecimal(state.time),
                    hour
                )
            )
            StatRow(
                label = stringResource(Res.string.play_sample_cell_count_label).trim(),
                value = state.cellCount.toString()
            )
            StatRow(
                label = stringResource(Res.string.play_sample_nutrient_amount_label).trim(),
                value = formatOneDecimal(state.foodMass)
            )
        }
    }
}

@Composable
private fun StatRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.primary
        )
    }
}

@Composable
internal fun EnvironmentSliderCards(
    sliders: List<SubstrateSliderValue>,
    onSliderChanged: (SubstrateSliderType, Double) -> Unit,
    sliderValueFormatter: (Double) -> String,
    sliderInputValueFormatter: (Double) -> String
) {
    sliders.forEach { slider ->
        SliderCard(
            label = substrateSliderLabel(slider.type),
            currentValue = slider.value,
            min = slider.min,
            max = slider.max,
            maxSteps = ENVIRONMENT_SLIDER_STEPS,
            valueFormatter = sliderValueFormatter,
            inputValueFormatter = sliderInputValueFormatter,
            editIcon = { SliderCardEditIcon() },
            strings = sliderCardStrings(),
            commitOnValueChangeFinished = true,
            onValueChange = { value -> onSliderChanged(slider.type, value) }
        )
    }
}

@Composable
internal fun SliderCardEditIcon() {
    Icon(
        painter = painterResource(Res.drawable.settings_icon),
        contentDescription = stringResource(Res.string.common_edit_value_precisely_action),
        tint = Color.Gray
    )
}

@Composable
internal fun sliderCardStrings(): SliderCardStrings = SliderCardStrings(
    invalidNumber = stringResource(Res.string.common_invalid_number_message),
    confirm = stringResource(Res.string.common_ok_action),
    dismiss = stringResource(Res.string.common_cancel_action)
)

@Composable
internal fun substrateSliderLabel(type: SubstrateSliderType): String = when (type) {
    SubstrateSliderType.RADIATION -> stringResource(Res.string.substrate_radiation_level)
    SubstrateSliderType.TEST_MULTICELLULAR_COEF ->
        stringResource(Res.string.substrate_test_multicellular_coefficient)
    SubstrateSliderType.TEST_MUTATION_COEF ->
        stringResource(Res.string.substrate_test_mutation_coefficient)
    SubstrateSliderType.TEST_FOOD_DECAY ->
        stringResource(Res.string.substrate_test_nutrient_decay)
    SubstrateSliderType.TEST_IDLE_METABOLISM_SCALE ->
        stringResource(Res.string.substrate_test_idle_metabolism_scale)
    SubstrateSliderType.TEST_FLAGELLOCYTE_COST_SCALE ->
        stringResource(Res.string.substrate_test_flagellocyte_cost_scale)
    SubstrateSliderType.TEST_PHAGOCYTE_EAT_SPEED_SCALE ->
        stringResource(Res.string.substrate_test_phagocyte_eating_speed_scale)
    SubstrateSliderType.VISCOSITY -> stringResource(Res.string.substrate_viscosity)
    SubstrateSliderType.FRICTION_DYNAMIC -> stringResource(Res.string.substrate_dynamic_friction)
    SubstrateSliderType.FRICTION_STATIC -> stringResource(Res.string.substrate_static_friction)
    SubstrateSliderType.FOOD_SPEED -> stringResource(Res.string.substrate_nutrient_rate)
    SubstrateSliderType.FOOD_SIZE -> stringResource(Res.string.substrate_nutrient_chunk_size)
    SubstrateSliderType.FOOD_LUMPINESS -> stringResource(Res.string.substrate_nutrient_lumpiness)
    SubstrateSliderType.FOOD_LUMP_SIZE -> stringResource(Res.string.substrate_nutrient_lump_size)
    SubstrateSliderType.FOOD_LIPID_LAYER -> stringResource(Res.string.substrate_nutrient_coating)
    SubstrateSliderType.SALINITY -> stringResource(Res.string.substrate_salinity)
    SubstrateSliderType.LIGHT_INTENSITY -> stringResource(Res.string.substrate_light_amount)
    SubstrateSliderType.LIGHT_RANGE -> stringResource(Res.string.substrate_light_range)
    SubstrateSliderType.LIGHT_SPEED -> stringResource(Res.string.substrate_light_direction_change)
    SubstrateSliderType.GRAVITY -> stringResource(Res.string.substrate_gravity)
    SubstrateSliderType.DENSITY -> stringResource(Res.string.substrate_density)
    SubstrateSliderType.DENSITY_GRADIENT -> stringResource(Res.string.substrate_density_gradient)
    SubstrateSliderType.NITRATES -> stringResource(Res.string.substrate_nitrates)
}

internal fun formatTwoDecimals(value: Double): String =
    formatLocalizedNumber(value, minimumFractionDigits = 0, maximumFractionDigits = 2)

private fun formatOneDecimal(value: Double): String =
    formatLocalizedNumber(value, minimumFractionDigits = 1, maximumFractionDigits = 1)
