package com.cytarch.cellularity.shared.ui.play.substrate_info

import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.substrate_field_component_angle
import com.cytarch.cellularity.shared.generated.resources.substrate_field_component_base
import com.cytarch.cellularity.shared.generated.resources.substrate_field_component_offset
import com.cytarch.cellularity.shared.generated.resources.substrate_field_component_softness
import com.cytarch.cellularity.shared.generated.resources.substrate_field_component_strength
import com.cytarch.cellularity.shared.generated.resources.substrate_field_density
import com.cytarch.cellularity.shared.generated.resources.substrate_field_gravity
import com.cytarch.cellularity.shared.generated.resources.substrate_field_mode_constant
import com.cytarch.cellularity.shared.generated.resources.substrate_field_mode_directional
import com.cytarch.cellularity.shared.generated.resources.substrate_field_mode_linear_directional
import com.cytarch.cellularity.shared.generated.resources.substrate_field_mode_radial
import com.cytarch.cellularity.shared.generated.resources.substrate_field_mode_radial_inward
import com.cytarch.cellularity.shared.generated.resources.substrate_field_mode_radial_outward
import com.cytarch.cellularity.shared.generated.resources.substrate_field_nutrient_coating
import com.cytarch.cellularity.shared.generated.resources.substrate_field_salinity
import com.cytarch.cellularity.shared.generated.resources.substrate_section_evolution
import com.cytarch.cellularity.shared.generated.resources.substrate_section_mechanics
import com.cytarch.cellularity.shared.generated.resources.substrate_section_sources
import com.cytarch.cellularity.shared.generated.resources.substrate_section_stats
import com.cytarch.cellularity.shared.model.EnvironmentFieldType
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import org.jetbrains.compose.resources.StringResource

internal data class EnvironmentFieldPresentationDescriptor(
    val target: EnvironmentFieldTarget,
    val titleResource: StringResource
)

internal data class EnvironmentFieldModePresentationDescriptor<out T : EnvironmentFieldType>(
    val type: T,
    val labelResource: StringResource
)

internal data class EnvironmentFieldComponentPresentationDescriptor(
    val component: DensityFieldValueType,
    val labelResource: StringResource
)

internal data class SubstrateInfoTabPresentationDescriptor(
    val section: SubstrateInfoSection,
    val titleResource: StringResource
)

internal fun EnvironmentFieldTarget.presentationDescriptor(): EnvironmentFieldPresentationDescriptor =
    when (this) {
        EnvironmentFieldTarget.DENSITY -> EnvironmentFieldPresentationDescriptor(
            this,
            Res.string.substrate_field_density
        )
        EnvironmentFieldTarget.GRAVITY -> EnvironmentFieldPresentationDescriptor(
            this,
            Res.string.substrate_field_gravity
        )
        EnvironmentFieldTarget.SALINITY -> EnvironmentFieldPresentationDescriptor(
            this,
            Res.string.substrate_field_salinity
        )
        EnvironmentFieldTarget.FOOD_LIPID_LAYER -> EnvironmentFieldPresentationDescriptor(
            this,
            Res.string.substrate_field_nutrient_coating
        )
    }

internal fun ScalarFieldType.presentationDescriptor():
    EnvironmentFieldModePresentationDescriptor<ScalarFieldType> = when (this) {
        ScalarFieldType.CONSTANT -> EnvironmentFieldModePresentationDescriptor(
            this,
            Res.string.substrate_field_mode_constant
        )
        ScalarFieldType.LINEAR_DIRECTIONAL -> EnvironmentFieldModePresentationDescriptor(
            this,
            Res.string.substrate_field_mode_linear_directional
        )
        ScalarFieldType.RADIAL -> EnvironmentFieldModePresentationDescriptor(
            this,
            Res.string.substrate_field_mode_radial
        )
    }

internal fun VectorFieldType.presentationDescriptor():
    EnvironmentFieldModePresentationDescriptor<VectorFieldType> = when (this) {
        VectorFieldType.LINEAR_DIRECTIONAL -> EnvironmentFieldModePresentationDescriptor(
            this,
            Res.string.substrate_field_mode_directional
        )
        VectorFieldType.RADIAL_INWARD -> EnvironmentFieldModePresentationDescriptor(
            this,
            Res.string.substrate_field_mode_radial_inward
        )
        VectorFieldType.RADIAL_OUTWARD -> EnvironmentFieldModePresentationDescriptor(
            this,
            Res.string.substrate_field_mode_radial_outward
        )
    }

internal fun EnvironmentFieldType.presentationDescriptor():
    EnvironmentFieldModePresentationDescriptor<EnvironmentFieldType> = when (this) {
        is ScalarFieldType -> presentationDescriptor()
        is VectorFieldType -> presentationDescriptor()
    }

internal fun DensityFieldValueType.presentationDescriptor():
    EnvironmentFieldComponentPresentationDescriptor = when (this) {
        DensityFieldValueType.BASE -> EnvironmentFieldComponentPresentationDescriptor(
            this,
            Res.string.substrate_field_component_base
        )
        DensityFieldValueType.STRENGTH -> EnvironmentFieldComponentPresentationDescriptor(
            this,
            Res.string.substrate_field_component_strength
        )
        DensityFieldValueType.ANGLE -> EnvironmentFieldComponentPresentationDescriptor(
            this,
            Res.string.substrate_field_component_angle
        )
        DensityFieldValueType.OFFSET -> EnvironmentFieldComponentPresentationDescriptor(
            this,
            Res.string.substrate_field_component_offset
        )
        DensityFieldValueType.SOFTNESS -> EnvironmentFieldComponentPresentationDescriptor(
            this,
            Res.string.substrate_field_component_softness
        )
    }

internal fun SubstrateInfoSection.presentationDescriptor():
    SubstrateInfoTabPresentationDescriptor = when (this) {
        SubstrateInfoSection.MECHANICS -> SubstrateInfoTabPresentationDescriptor(
            this,
            Res.string.substrate_section_mechanics
        )
        SubstrateInfoSection.SOURCES -> SubstrateInfoTabPresentationDescriptor(
            this,
            Res.string.substrate_section_sources
        )
        SubstrateInfoSection.EVOLUTION -> SubstrateInfoTabPresentationDescriptor(
            this,
            Res.string.substrate_section_evolution
        )
        SubstrateInfoSection.STATS -> SubstrateInfoTabPresentationDescriptor(
            this,
            Res.string.substrate_section_stats
        )
    }

internal val environmentFieldPresentationDescriptors =
    EnvironmentFieldTarget.entries.map { it.presentationDescriptor() }

internal val scalarFieldModePresentationDescriptors =
    ScalarFieldType.entries.map { it.presentationDescriptor() }

internal val vectorFieldModePresentationDescriptors =
    VectorFieldType.entries.map { it.presentationDescriptor() }

internal val environmentFieldComponentPresentationDescriptors =
    DensityFieldValueType.entries.map { it.presentationDescriptor() }

internal val substrateInfoTabPresentationDescriptors =
    SubstrateInfoSection.entries.map { it.presentationDescriptor() }
