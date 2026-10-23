package com.cytarch.cellularity.shared.ui.play.substrate_info

import com.cytarch.cellularity.shared.model.EnvironmentConstants
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.SimulationStats

internal val mechanicsSliderTypes = listOf(
    SubstrateSliderType.VISCOSITY,
    SubstrateSliderType.FRICTION_DYNAMIC,
    SubstrateSliderType.FRICTION_STATIC,
    SubstrateSliderType.DENSITY,
    SubstrateSliderType.DENSITY_GRADIENT
)

internal val nutritionSliderTypes = listOf(
    SubstrateSliderType.FOOD_SPEED,
    SubstrateSliderType.FOOD_SIZE,
    SubstrateSliderType.FOOD_LUMPINESS,
    SubstrateSliderType.FOOD_LUMP_SIZE,
    SubstrateSliderType.FOOD_LIPID_LAYER,
    SubstrateSliderType.SALINITY,
    SubstrateSliderType.LIGHT_INTENSITY,
    SubstrateSliderType.LIGHT_RANGE,
    SubstrateSliderType.LIGHT_SPEED,
    SubstrateSliderType.GRAVITY,
    SubstrateSliderType.NITRATES
)

internal val experimentalSliderTypes = listOf(
    SubstrateSliderType.TEST_MULTICELLULAR_COEF,
    SubstrateSliderType.TEST_MUTATION_COEF,
    SubstrateSliderType.TEST_FOOD_DECAY,
    SubstrateSliderType.TEST_IDLE_METABOLISM_SCALE,
    SubstrateSliderType.TEST_FLAGELLOCYTE_COST_SCALE,
    SubstrateSliderType.TEST_PHAGOCYTE_EAT_SPEED_SCALE
)

fun EnvironmentSnapshot.toSubstrateEnvironmentUiState(): SubstrateEnvironmentUiState {
    val activeGeneCount = genePool.count { it }
    val onlyActiveGene = genePool.indexOfFirst { it }.takeIf { activeGeneCount == 1 }
    val enabledGenePool = genePool.indices.map { index -> index != onlyActiveGene }

    return SubstrateEnvironmentUiState(
        revision = revision,
        mechanics = SubstrateMechanicsSectionState(
            dieEdge = dieEdge,
            sliders = mechanicsSliderTypes.map(::slider)
        ),
        fields = SubstrateFieldsSectionState(
            density = SubstrateFieldUiState(
                target = EnvironmentFieldTarget.DENSITY,
                type = densityField.type,
                base = densityField.base,
                strength = densityField.strength,
                angle = densityField.angle,
                offset = densityField.offset,
                softness = densityField.softness,
                ranges = SubstrateFieldRanges(
                    baseMin = EnvironmentConstants.MIN_DENSITY,
                    baseMax = EnvironmentConstants.MAX_DENSITY,
                    strengthMin = -2.0,
                    strengthMax = 2.0
                )
            ),
            gravity = SubstrateFieldUiState(
                target = EnvironmentFieldTarget.GRAVITY,
                type = gravityField.type,
                base = 0.0,
                strength = gravityField.strength,
                angle = gravityField.angle,
                offset = 0.0,
                softness = 0.0,
                ranges = SubstrateFieldRanges(
                    baseMin = 0.0,
                    baseMax = 0.0,
                    strengthMin = EnvironmentConstants.MIN_GRAVITY,
                    strengthMax = EnvironmentConstants.MAX_GRAVITY
                )
            ),
            salinity = SubstrateFieldUiState(
                target = EnvironmentFieldTarget.SALINITY,
                type = salinityField.type,
                base = salinityField.base,
                strength = salinityField.strength,
                angle = salinityField.angle,
                offset = salinityField.offset,
                softness = salinityField.softness,
                ranges = unitFieldRanges()
            ),
            foodLipidLayer = SubstrateFieldUiState(
                target = EnvironmentFieldTarget.FOOD_LIPID_LAYER,
                type = foodLipidLayerField.type,
                base = foodLipidLayerField.base,
                strength = foodLipidLayerField.strength,
                angle = foodLipidLayerField.angle,
                offset = foodLipidLayerField.offset,
                softness = foodLipidLayerField.softness,
                ranges = unitFieldRanges()
            )
        ),
        nutrition = SubstrateNutritionSectionState(
            mobileFood = mobileFood,
            sliders = nutritionSliderTypes.map(::slider)
        ),
        evolution = SubstrateEvolutionSectionState(
            addRandom = addRandom,
            onlyTweakMutations = onlyTweakMutations,
            mutationOnlyAfterSplit = mutationOnlyAfterSplit,
            aging = aging,
            radiation = slider(SubstrateSliderType.RADIATION),
            mutationWeights = mutationWeights,
            genePool = genePool,
            genePoolEnabled = enabledGenePool
        ),
        experimental = SubstrateExperimentalSectionState(
            sliders = experimentalSliderTypes.map(::slider)
        )
    )
}

fun SimulationStats.toSubstrateStatsSectionState(): SubstrateStatsSectionState =
    SubstrateStatsSectionState(
        time = time,
        cellCount = cellCount,
        foodMass = foodMass
    )

private fun EnvironmentSnapshot.slider(type: SubstrateSliderType): SubstrateSliderValue =
    when (type) {
        SubstrateSliderType.RADIATION ->
            slider(type, radiation, 0.0, EnvironmentConstants.MAX_RADIATION)
        SubstrateSliderType.TEST_MULTICELLULAR_COEF ->
            slider(type, testMulticellularCoef, -10.0, 10.0)
        SubstrateSliderType.TEST_MUTATION_COEF ->
            slider(type, testMutationCoef, 0.0, 5.0)
        SubstrateSliderType.TEST_FOOD_DECAY ->
            slider(type, testFoodDecay, -10.0, 10.0)
        SubstrateSliderType.TEST_IDLE_METABOLISM_SCALE ->
            slider(type, testIdleMetabolismScale, 0.0, 5.0)
        SubstrateSliderType.TEST_FLAGELLOCYTE_COST_SCALE ->
            slider(type, testFlagellocyteCostScale, 0.0, 5.0)
        SubstrateSliderType.TEST_PHAGOCYTE_EAT_SPEED_SCALE ->
            slider(type, testPhagocyteEatSpeedScale, 0.0, 2.0)
        SubstrateSliderType.VISCOSITY ->
            slider(type, viscosity, 0.0, EnvironmentConstants.VISCOSITY_MAX)
        SubstrateSliderType.FRICTION_DYNAMIC ->
            slider(type, frictionDynamic, 0.0, EnvironmentConstants.MAX_FRICTION)
        SubstrateSliderType.FRICTION_STATIC ->
            slider(type, frictionStatic, 0.0, EnvironmentConstants.MAX_FRICTION)
        SubstrateSliderType.FOOD_SPEED ->
            slider(type, foodSpeed, 0.0, EnvironmentConstants.MAX_FOOD_SPEED)
        SubstrateSliderType.FOOD_SIZE ->
            slider(type, foodSize, EnvironmentConstants.MIN_FOOD_SIZE, EnvironmentConstants.MAX_FOOD_SIZE)
        SubstrateSliderType.FOOD_LUMPINESS ->
            slider(type, foodLumpiness, 0.0, EnvironmentConstants.MAX_FOOD_LUMPINESS)
        SubstrateSliderType.FOOD_LUMP_SIZE ->
            slider(
                type,
                foodLumpSize,
                EnvironmentConstants.MIN_FOOD_LUMP_SIZE,
                EnvironmentConstants.MAX_LUMP_SIZE
            )
        SubstrateSliderType.FOOD_LIPID_LAYER -> slider(type, foodLipidLayer, 0.0, 1.0)
        SubstrateSliderType.SALINITY -> slider(type, salinity, 0.0, 1.0)
        SubstrateSliderType.LIGHT_INTENSITY ->
            slider(type, lightIntensity, 0.0, EnvironmentConstants.MAX_LIGHT)
        SubstrateSliderType.LIGHT_RANGE ->
            slider(type, lightRange, 0.0, EnvironmentConstants.MAX_LIGHT_RANGE)
        SubstrateSliderType.LIGHT_SPEED ->
            slider(type, lightSpeed, 0.0, EnvironmentConstants.MAX_LIGHT_SPEED)
        SubstrateSliderType.GRAVITY ->
            slider(type, gravity, EnvironmentConstants.MIN_GRAVITY, EnvironmentConstants.MAX_GRAVITY)
        SubstrateSliderType.DENSITY ->
            slider(type, density, EnvironmentConstants.MIN_DENSITY, EnvironmentConstants.MAX_DENSITY)
        SubstrateSliderType.DENSITY_GRADIENT ->
            slider(
                type,
                densityGradient,
                EnvironmentConstants.MIN_GRADIENT,
                EnvironmentConstants.MAX_GRADIENT
            )
        SubstrateSliderType.NITRATES -> slider(type, nitrates, 0.0, 1.0)
    }

private fun unitFieldRanges(): SubstrateFieldRanges = SubstrateFieldRanges(
    baseMin = 0.0,
    baseMax = 1.0,
    strengthMin = -1.0,
    strengthMax = 1.0
)

private fun slider(
    type: SubstrateSliderType,
    value: Double,
    min: Double,
    max: Double
): SubstrateSliderValue = SubstrateSliderValue(type, value, min, max)
