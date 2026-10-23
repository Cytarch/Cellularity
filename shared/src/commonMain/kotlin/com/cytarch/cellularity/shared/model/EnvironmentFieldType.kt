package com.cytarch.cellularity.shared.model

sealed interface EnvironmentFieldType {
    val nativeValue: Int
}

enum class ScalarFieldType(override val nativeValue: Int) : EnvironmentFieldType {
    CONSTANT(0),
    LINEAR_DIRECTIONAL(1),
    RADIAL(2);

    companion object {
        fun fromNativeValue(value: Int): ScalarFieldType =
            entries.firstOrNull { it.nativeValue == value }
                ?: throw IllegalArgumentException("Unknown scalar field type: $value")
    }
}

enum class VectorFieldType(override val nativeValue: Int) : EnvironmentFieldType {
    LINEAR_DIRECTIONAL(0),
    RADIAL_INWARD(1),
    RADIAL_OUTWARD(2);

    companion object {
        fun fromNativeValue(value: Int): VectorFieldType =
            entries.firstOrNull { it.nativeValue == value }
                ?: throw IllegalArgumentException("Unknown vector field type: $value")
    }
}
