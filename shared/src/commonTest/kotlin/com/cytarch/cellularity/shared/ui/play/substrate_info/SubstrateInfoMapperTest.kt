package com.cytarch.cellularity.shared.ui.play.substrate_info

import com.cytarch.cellularity.shared.model.EnvironmentConstants
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.SimulationStats
import com.cytarch.cellularity.shared.model.VectorFieldSnapshot
import com.cytarch.cellularity.shared.model.VectorFieldType
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse

class SubstrateInfoMapperTest {
    @Test
    fun navigationKeepsTheEstablishedFourTabsInOrder() {
        assertEquals(
            listOf(
                SubstrateInfoSection.MECHANICS,
                SubstrateInfoSection.SOURCES,
                SubstrateInfoSection.EVOLUTION,
                SubstrateInfoSection.STATS
            ),
            SubstrateInfoSection.entries
        )
    }

    @Test
    fun sliderFamiliesKeepTheirEstablishedSectionAndOrder() {
        assertEquals(
            listOf(
                SubstrateSliderType.VISCOSITY,
                SubstrateSliderType.FRICTION_DYNAMIC,
                SubstrateSliderType.FRICTION_STATIC,
                SubstrateSliderType.DENSITY,
                SubstrateSliderType.DENSITY_GRADIENT
            ),
            mechanicsSliderTypes
        )
        assertEquals(
            listOf(
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
            ),
            nutritionSliderTypes
        )
        assertEquals(
            listOf(
                SubstrateSliderType.TEST_MULTICELLULAR_COEF,
                SubstrateSliderType.TEST_MUTATION_COEF,
                SubstrateSliderType.TEST_FOOD_DECAY,
                SubstrateSliderType.TEST_IDLE_METABOLISM_SCALE,
                SubstrateSliderType.TEST_FLAGELLOCYTE_COST_SCALE,
                SubstrateSliderType.TEST_PHAGOCYTE_EAT_SPEED_SCALE
            ),
            experimentalSliderTypes
        )
    }

    @Test
    fun partitionsEverySliderIntoExactlyOneSection() {
        val mapped = environment().toSubstrateEnvironmentUiState()
        val mechanics = mapped.mechanics.sliders.map { it.type }
        val nutrition = mapped.nutrition.sliders.map { it.type }
        val evolution = listOf(mapped.evolution.radiation.type)
        val experimental = mapped.experimental.sliders.map { it.type }
        val all = mechanics + nutrition + evolution + experimental

        assertEquals(mechanicsSliderTypes, mechanics)
        assertEquals(nutritionSliderTypes, nutrition)
        assertEquals(listOf(SubstrateSliderType.RADIATION), evolution)
        assertEquals(experimentalSliderTypes, experimental)
        assertEquals(SubstrateSliderType.entries.size, all.size)
        assertEquals(SubstrateSliderType.entries.toSet(), all.toSet())
    }

    @Test
    fun preservesEverySliderValueAndRange() {
        val mapped = environment().toSubstrateEnvironmentUiState()
        val sliders = (
            mapped.mechanics.sliders +
                mapped.nutrition.sliders +
                mapped.evolution.radiation +
                mapped.experimental.sliders
            ).associateBy { it.type }
        val expectedValues = mapOf(
            SubstrateSliderType.RADIATION to 0.03,
            SubstrateSliderType.TEST_MULTICELLULAR_COEF to 1.1,
            SubstrateSliderType.TEST_MUTATION_COEF to 1.2,
            SubstrateSliderType.TEST_FOOD_DECAY to -1.3,
            SubstrateSliderType.TEST_IDLE_METABOLISM_SCALE to 1.4,
            SubstrateSliderType.TEST_FLAGELLOCYTE_COST_SCALE to 1.5,
            SubstrateSliderType.TEST_PHAGOCYTE_EAT_SPEED_SCALE to 1.6,
            SubstrateSliderType.VISCOSITY to 60.0,
            SubstrateSliderType.FRICTION_DYNAMIC to 0.2,
            SubstrateSliderType.FRICTION_STATIC to 0.3,
            SubstrateSliderType.FOOD_SPEED to 0.4,
            SubstrateSliderType.FOOD_SIZE to 0.5,
            SubstrateSliderType.FOOD_LUMPINESS to 0.6,
            SubstrateSliderType.FOOD_LUMP_SIZE to 0.07,
            SubstrateSliderType.FOOD_LIPID_LAYER to 0.8,
            SubstrateSliderType.SALINITY to 0.9,
            SubstrateSliderType.LIGHT_INTENSITY to 1.0,
            SubstrateSliderType.LIGHT_RANGE to 0.75,
            SubstrateSliderType.LIGHT_SPEED to 0.65,
            SubstrateSliderType.GRAVITY to 0.55,
            SubstrateSliderType.DENSITY to -0.45,
            SubstrateSliderType.DENSITY_GRADIENT to -0.35,
            SubstrateSliderType.NITRATES to 0.25
        )
        val expectedRanges = mapOf(
            SubstrateSliderType.RADIATION to (0.0 to EnvironmentConstants.MAX_RADIATION),
            SubstrateSliderType.TEST_MULTICELLULAR_COEF to (-10.0 to 10.0),
            SubstrateSliderType.TEST_MUTATION_COEF to (0.0 to 5.0),
            SubstrateSliderType.TEST_FOOD_DECAY to (-10.0 to 10.0),
            SubstrateSliderType.TEST_IDLE_METABOLISM_SCALE to (0.0 to 5.0),
            SubstrateSliderType.TEST_FLAGELLOCYTE_COST_SCALE to (0.0 to 5.0),
            SubstrateSliderType.TEST_PHAGOCYTE_EAT_SPEED_SCALE to (0.0 to 2.0),
            SubstrateSliderType.VISCOSITY to (0.0 to EnvironmentConstants.VISCOSITY_MAX),
            SubstrateSliderType.FRICTION_DYNAMIC to (0.0 to EnvironmentConstants.MAX_FRICTION),
            SubstrateSliderType.FRICTION_STATIC to (0.0 to EnvironmentConstants.MAX_FRICTION),
            SubstrateSliderType.FOOD_SPEED to (0.0 to EnvironmentConstants.MAX_FOOD_SPEED),
            SubstrateSliderType.FOOD_SIZE to
                (EnvironmentConstants.MIN_FOOD_SIZE to EnvironmentConstants.MAX_FOOD_SIZE),
            SubstrateSliderType.FOOD_LUMPINESS to
                (0.0 to EnvironmentConstants.MAX_FOOD_LUMPINESS),
            SubstrateSliderType.FOOD_LUMP_SIZE to
                (EnvironmentConstants.MIN_FOOD_LUMP_SIZE to EnvironmentConstants.MAX_LUMP_SIZE),
            SubstrateSliderType.FOOD_LIPID_LAYER to (0.0 to 1.0),
            SubstrateSliderType.SALINITY to (0.0 to 1.0),
            SubstrateSliderType.LIGHT_INTENSITY to (0.0 to EnvironmentConstants.MAX_LIGHT),
            SubstrateSliderType.LIGHT_RANGE to (0.0 to EnvironmentConstants.MAX_LIGHT_RANGE),
            SubstrateSliderType.LIGHT_SPEED to (0.0 to EnvironmentConstants.MAX_LIGHT_SPEED),
            SubstrateSliderType.GRAVITY to
                (EnvironmentConstants.MIN_GRAVITY to EnvironmentConstants.MAX_GRAVITY),
            SubstrateSliderType.DENSITY to
                (EnvironmentConstants.MIN_DENSITY to EnvironmentConstants.MAX_DENSITY),
            SubstrateSliderType.DENSITY_GRADIENT to
                (EnvironmentConstants.MIN_GRADIENT to EnvironmentConstants.MAX_GRADIENT),
            SubstrateSliderType.NITRATES to (0.0 to 1.0)
        )

        assertEquals(SubstrateSliderType.entries.toSet(), sliders.keys)
        expectedValues.forEach { (type, value) -> assertEquals(value, sliders.getValue(type).value) }
        expectedRanges.forEach { (type, range) ->
            assertEquals(range.first, sliders.getValue(type).min, "$type minimum")
            assertEquals(range.second, sliders.getValue(type).max, "$type maximum")
        }
    }

    @Test
    fun mapsEveryStructuredFieldIntoTheFieldsSlice() {
        val fields = environment().toSubstrateEnvironmentUiState().fields

        assertEquals(
            SubstrateFieldUiState(
                target = EnvironmentFieldTarget.DENSITY,
                type = ScalarFieldType.LINEAR_DIRECTIONAL,
                base = -0.4,
                strength = 0.5,
                angle = 0.6,
                offset = 0.7,
                softness = 0.8,
                ranges = SubstrateFieldRanges(-3.3, 3.3, -2.0, 2.0)
            ),
            fields.field(EnvironmentFieldTarget.DENSITY)
        )
        assertEquals(
            SubstrateFieldUiState(
                target = EnvironmentFieldTarget.GRAVITY,
                type = VectorFieldType.RADIAL_INWARD,
                base = 0.0,
                strength = 0.11,
                angle = 0.12,
                offset = 0.0,
                softness = 0.0,
                ranges = SubstrateFieldRanges(0.0, 0.0, 0.0, 1.0)
            ),
            fields.field(EnvironmentFieldTarget.GRAVITY)
        )
        assertEquals(
            SubstrateFieldUiState(
                target = EnvironmentFieldTarget.SALINITY,
                type = ScalarFieldType.RADIAL,
                base = 0.21,
                strength = 0.22,
                angle = 0.23,
                offset = 0.24,
                softness = 0.25,
                ranges = SubstrateFieldRanges(0.0, 1.0, -1.0, 1.0)
            ),
            fields.field(EnvironmentFieldTarget.SALINITY)
        )
        assertEquals(
            SubstrateFieldUiState(
                target = EnvironmentFieldTarget.FOOD_LIPID_LAYER,
                type = ScalarFieldType.CONSTANT,
                base = 0.31,
                strength = 0.32,
                angle = 0.33,
                offset = 0.34,
                softness = 0.35,
                ranges = SubstrateFieldRanges(0.0, 1.0, -1.0, 1.0)
            ),
            fields.field(EnvironmentFieldTarget.FOOD_LIPID_LAYER)
        )
    }

    @Test
    fun sectionCollectionsOwnTheirInputsAndGenePoolKeepsOneTypeEnabled() {
        val mapped = environment().toSubstrateEnvironmentUiState()
        val onlyEnabledIndex = mapped.evolution.genePool.indexOfFirst { it }

        assertEquals(4, onlyEnabledIndex)
        assertFalse(mapped.evolution.genePoolEnabled[onlyEnabledIndex])
        assertEquals(
            List(mapped.evolution.genePoolEnabled.size) { it != onlyEnabledIndex },
            mapped.evolution.genePoolEnabled
        )
        assertFalse(mapped.mechanics.sliders is MutableList<*>)
        assertFalse(mapped.nutrition.sliders is MutableList<*>)
        assertFalse(mapped.experimental.sliders is MutableList<*>)
        assertFalse(mapped.evolution.mutationWeights is MutableList<*>)
        assertFalse(mapped.evolution.genePool is MutableList<*>)
    }

    @Test
    fun sectionConstructorsDefensivelyCopyCollectionInputs() {
        val sliderSource = mutableListOf(
            SubstrateSliderValue(SubstrateSliderType.VISCOSITY, 1.0, 0.0, 2.0)
        )
        val weights = mutableListOf(1.0, 2.0)
        val pool = mutableListOf(true, false)
        val enabled = mutableListOf(true, true)
        val mechanics = SubstrateMechanicsSectionState(dieEdge = false, sliders = sliderSource)
        val evolution = SubstrateEvolutionSectionState(
            addRandom = false,
            onlyTweakMutations = false,
            mutationOnlyAfterSplit = false,
            aging = false,
            radiation = SubstrateSliderValue(SubstrateSliderType.RADIATION, 0.0, 0.0, 1.0),
            mutationWeights = weights,
            genePool = pool,
            genePoolEnabled = enabled
        )

        sliderSource.clear()
        weights.clear()
        pool.clear()
        enabled.clear()

        assertEquals(1, mechanics.sliders.size)
        assertEquals(listOf(1.0, 2.0), evolution.mutationWeights)
        assertEquals(listOf(true, false), evolution.genePool)
        assertEquals(listOf(true, true), evolution.genePoolEnabled)
    }

    @Test
    fun statsAndNavigationAreIndependentFromEnvironmentState() {
        val stats = SimulationStats(cellCount = 7, foodMass = 8.5, time = 9.25)
            .toSubstrateStatsSectionState()
        val state = SubstrateInfoState()

        assertEquals(SubstrateStatsSectionState(9.25, 7, 8.5), stats)
        assertEquals(SubstrateInfoSection.MECHANICS, state.selectedSection)
        state.openFieldEditor(EnvironmentFieldTarget.DENSITY)
        assertEquals(EnvironmentFieldTarget.DENSITY, state.editingFieldTarget)
        state.updateSelectedSection(SubstrateInfoSection.SOURCES)
        state.updateScrollOffset(SubstrateInfoSection.SOURCES, 123)
        state.updateScrollOffset(SubstrateInfoSection.EVOLUTION, -5)
        assertEquals(SubstrateInfoSection.SOURCES, state.selectedSection)
        assertEquals(null, state.editingFieldTarget)
        assertEquals(123, state.getScrollOffset(SubstrateInfoSection.SOURCES))
        assertEquals(0, state.getScrollOffset(SubstrateInfoSection.EVOLUTION))

        state.updateSelectedSection(SubstrateInfoSection.EVOLUTION)
        state.showMutationWeights()
        assertEquals(true, state.mutationWeightsVisible)
        state.updateSelectedSection(SubstrateInfoSection.STATS)
        assertEquals(false, state.mutationWeightsVisible)
    }
}

private fun environment(): EnvironmentSnapshot = EnvironmentSnapshot(
    revision = 17,
    addRandom = true,
    dieEdge = true,
    onlyTweakMutations = false,
    mutationOnlyAfterSplit = true,
    aging = false,
    mobileFood = true,
    gravityField = VectorFieldSnapshot(VectorFieldType.RADIAL_INWARD, 0.11, 0.12),
    densityField = ScalarFieldSnapshot(
        ScalarFieldType.LINEAR_DIRECTIONAL,
        -0.4,
        0.5,
        0.6,
        0.7,
        0.8
    ),
    salinityField = ScalarFieldSnapshot(ScalarFieldType.RADIAL, 0.21, 0.22, 0.23, 0.24, 0.25),
    foodLipidLayerField = ScalarFieldSnapshot(
        ScalarFieldType.CONSTANT,
        0.31,
        0.32,
        0.33,
        0.34,
        0.35
    ),
    radiation = 0.03,
    testMulticellularCoef = 1.1,
    testMutationCoef = 1.2,
    testFoodDecay = -1.3,
    testIdleMetabolismScale = 1.4,
    testFlagellocyteCostScale = 1.5,
    testPhagocyteEatSpeedScale = 1.6,
    viscosity = 60.0,
    frictionDynamic = 0.2,
    frictionStatic = 0.3,
    foodSpeed = 0.4,
    foodSize = 0.5,
    foodLumpiness = 0.6,
    foodLumpSize = 0.07,
    foodLipidLayer = 0.8,
    salinity = 0.9,
    lightIntensity = 1.0,
    lightRange = 0.75,
    lightSpeed = 0.65,
    gravity = 0.55,
    density = -0.45,
    densityGradient = -0.35,
    nitrates = 0.25,
    genePool = List(18) { it == 4 },
    mutationWeights = List(106) { index -> index / 10.0 }
)
