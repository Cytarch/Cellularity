package com.cytarch.cellularity.platform.render.preview

import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.ui.play.substrate_info.EnvironmentFieldTarget
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateFieldUiState

internal data class EnvironmentFieldPreviewUpdate(
    val targetWire: Int,
    val fieldTypeWire: Int,
    val base: Double,
    val strength: Double,
    val angle: Double,
    val offset: Double,
    val softness: Double
)

internal fun SubstrateFieldUiState.toEnvironmentFieldPreviewUpdate():
    EnvironmentFieldPreviewUpdate {
    val targetWire = when (target) {
        EnvironmentFieldTarget.DENSITY -> 0
        EnvironmentFieldTarget.SALINITY -> 1
        EnvironmentFieldTarget.FOOD_LIPID_LAYER -> 2
        EnvironmentFieldTarget.GRAVITY -> 3
    }
    return when (target) {
        EnvironmentFieldTarget.GRAVITY -> {
            val vectorType = type as? VectorFieldType
                ?: throw IllegalArgumentException(
                    "Gravity preview requires a vector field type"
                )
            EnvironmentFieldPreviewUpdate(
                targetWire = targetWire,
                fieldTypeWire = when (vectorType) {
                    VectorFieldType.LINEAR_DIRECTIONAL -> 0
                    VectorFieldType.RADIAL_INWARD -> 1
                    VectorFieldType.RADIAL_OUTWARD -> 2
                },
                base = 0.0,
                strength = strength,
                angle = angle,
                offset = 0.0,
                softness = 0.0
            )
        }
        EnvironmentFieldTarget.DENSITY,
        EnvironmentFieldTarget.SALINITY,
        EnvironmentFieldTarget.FOOD_LIPID_LAYER -> {
            val scalarType = type as? ScalarFieldType
                ?: throw IllegalArgumentException(
                    "Scalar preview requires a scalar field type"
                )
            EnvironmentFieldPreviewUpdate(
                targetWire = targetWire,
                fieldTypeWire = when (scalarType) {
                    ScalarFieldType.CONSTANT -> 0
                    ScalarFieldType.LINEAR_DIRECTIONAL -> 1
                    ScalarFieldType.RADIAL -> 2
                },
                base = base,
                strength = strength,
                angle = angle,
                offset = offset,
                softness = softness
            )
        }
    }
}
