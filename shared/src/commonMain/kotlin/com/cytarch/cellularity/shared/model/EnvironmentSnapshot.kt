package com.cytarch.cellularity.shared.model

data class VectorFieldSnapshot(
    val type: VectorFieldType,
    val strength: Double,
    val angle: Double
)

data class ScalarFieldSnapshot(
    val type: ScalarFieldType,
    val base: Double,
    val strength: Double,
    val angle: Double,
    val offset: Double,
    val softness: Double
)

/** Immutable application snapshot of confirmed native environment state. */
class EnvironmentSnapshot(
    val schemaVersion: Int = EnvironmentContract.SCHEMA_VERSION,
    val revision: Long,
    val addRandom: Boolean,
    val dieEdge: Boolean,
    val onlyTweakMutations: Boolean,
    val mutationOnlyAfterSplit: Boolean,
    val aging: Boolean,
    val mobileFood: Boolean,
    val gravityField: VectorFieldSnapshot,
    val densityField: ScalarFieldSnapshot,
    val salinityField: ScalarFieldSnapshot,
    val foodLipidLayerField: ScalarFieldSnapshot,
    val radiation: Double,
    val testMulticellularCoef: Double,
    val testMutationCoef: Double,
    val testFoodDecay: Double,
    val testIdleMetabolismScale: Double,
    val testFlagellocyteCostScale: Double,
    val testPhagocyteEatSpeedScale: Double,
    val viscosity: Double,
    val frictionDynamic: Double,
    val frictionStatic: Double,
    val foodSpeed: Double,
    val foodSize: Double,
    val foodLumpiness: Double,
    val foodLumpSize: Double,
    val foodLipidLayer: Double,
    val salinity: Double,
    val lightIntensity: Double,
    val lightRange: Double,
    val lightSpeed: Double,
    val gravity: Double,
    val density: Double,
    val densityGradient: Double,
    val nitrates: Double,
    genePool: Iterable<Boolean>,
    mutationWeights: Iterable<Double>
) {
    val genePool: List<Boolean> = immutableListCopy(genePool)
    val mutationWeights: List<Double> = immutableListCopy(mutationWeights)

    init {
        require(schemaVersion > 0) { "Environment schema version must be positive" }
        require(revision >= 0) { "Environment revision must be non-negative" }
    }
}
