package com.cytarch.cellularity.shared.model

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class SimulationModelContractTest {
    @Test
    fun nativeEnumsRetainStableValues() {
        assertEquals(
            listOf(
                "PHAGOCYTE" to 0,
                "FLAGELLOCYTE" to 1,
                "PHOTOCYTE" to 2,
                "DEVOROCYTE" to 3,
                "LIPOCYTE" to 4,
                "KERATINOCYTE" to 5,
                "BUOYOCYTE" to 6,
                "GLUEOCYTE" to 7,
                "VIROCYTE" to 8,
                "NITROCYTE" to 9,
                "STEREOCYTE" to 10,
                "SENSEOCYTE" to 11,
                "MYOCYTE" to 12,
                "NEUROCYTE" to 13,
                "SECROCYTE" to 14,
                "STEMOCYTE" to 15,
                "GAMETE" to 16,
                "CILIOCYTE" to 17
            ),
            CellType.entries.map { it.name to it.nativeValue }
        )
        assertEquals(
            listOf("CONSTANT" to 0, "LINEAR_DIRECTIONAL" to 1, "RADIAL" to 2),
            ScalarFieldType.entries.map { it.name to it.nativeValue }
        )
        assertEquals(
            listOf("LINEAR_DIRECTIONAL" to 0, "RADIAL_INWARD" to 1, "RADIAL_OUTWARD" to 2),
            VectorFieldType.entries.map { it.name to it.nativeValue }
        )
    }

    @Test
    fun applicationConstantsMatchNativeContract() {
        assertEquals(-1, CellDataConstants.BOUNDARY_CONNECTION_INDEX)
        assertEquals(400.0, CellDataConstants.MASS_PER_AREA)
        assertEquals(0.03, CellDataConstants.MAX_CELL_RADIUS)
        assertEquals(0.072, CellDataConstants.MIN_MASS, 1e-15)
        assertEquals(0.36, CellDataConstants.MAX_MASS, 1e-15)
        assertEquals(2.16, CellDataConstants.MAX_LIPO_MASS, 1e-15)
        assertEquals(0.013416407864998739, CellDataConstants.MIN_CELL_RADIUS, 1e-15)
        assertEquals(240.0, CellDataConstants.MAX_AGE)
        assertEquals(1.8f, CellDataConstants.MAX_LIPIDS, 1e-6f)
        assertEquals(1.0f, CellDataConstants.MAX_SWIM_FORCE)
        assertEquals(20.0f, CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT)
        assertEquals(0.05f, CellDataConstants.MAX_SIGNAL_SUBSTANCE_OUTPUT_INVERSE)
        assertEquals(5.0f, CellDataConstants.SIGNAL_SUBSTANCE_FLOW)
        assertEquals(5.0f, CellDataConstants.SIGNAL_SUBSTANCE_DECAY)
        assertEquals(0.01f, CellDataConstants.SIGNAL_SUBSTANCE_INITIAL)
        assertEquals(4, CellDataConstants.SIGNAL_SUBSTANCE_COUNT)
        assertEquals(4, CellDataConstants.AUX_SIGNAL_SUBSTANCE_COUNT)
        assertEquals(4, CellDataConstants.NEUROCYTE_OUTPUT_COUNT)
        assertEquals(20, CellDataConstants.MAX_NEIGHBORS)
        assertEquals(40, CellDataConstants.LEGACY_GENOME_GENE_COUNT)
        assertEquals(256, CellDataConstants.MAX_GENOME_GENE_COUNT)
        assertEquals(8.0f, CellDataConstants.VALUE_VARIABLE_MAX)
        assertEquals(10.0f, CellDataConstants.MAX_MASS_FLOW_PRIORITY)
        assertEquals(0.1f, CellDataConstants.MIN_MASS_FLOW_PRIORITY)
        assertEquals(0.1f, CellDataConstants.MIN_RATIO)
        assertTrue(CellDataConstants.MAX_SPLITTABLE_MASS < CellDataConstants.MAX_MASS.toFloat())
        assertEquals(
            CellDataConstants.MAX_MASS.toFloat().toBits() - 1,
            CellDataConstants.MAX_SPLITTABLE_MASS.toBits()
        )
        assertEquals(0.3672f, CellDataConstants.MAX_SPLIT_MASS, 1e-7f)
        assertEquals(0.0648f, CellDataConstants.MIN_SPLIT_MASS, 1e-7f)
        assertEquals(15, CellDataConstants.MUTATE_PERIOD)
        assertEquals(35.0f, LinkConstants.MAX_STIFFNESS)

        assertEquals(2.0, EnvironmentConstants.MAX_LIGHT)
        assertEquals(1.0, EnvironmentConstants.MAX_LIGHT_RANGE)
        assertEquals(1.0, EnvironmentConstants.MAX_LIGHT_SPEED)
        assertEquals(0.09, EnvironmentConstants.MAX_RADIATION, 1e-15)
        assertEquals(150.0, EnvironmentConstants.VISCOSITY_MAX)
        assertEquals(1.152, EnvironmentConstants.MAX_FOOD_SIZE, 1e-15)
        assertEquals(0.0216, EnvironmentConstants.MIN_FOOD_SIZE, 1e-15)
        assertEquals(100.0, EnvironmentConstants.MAX_FOOD_LUMPINESS)
        assertEquals(0.01, EnvironmentConstants.MIN_FOOD_LUMP_SIZE)
        assertEquals(0.2, EnvironmentConstants.MAX_LUMP_SIZE)
        assertEquals(15.0, EnvironmentConstants.MAX_FOOD_SPEED, 1e-12)
        assertEquals(8.0, EnvironmentConstants.MAX_FRICTION)
        assertEquals(-3.3, EnvironmentConstants.MIN_DENSITY)
        assertEquals(3.3, EnvironmentConstants.MAX_DENSITY)
        assertEquals(0.0, EnvironmentConstants.MIN_GRAVITY)
        assertEquals(1.0, EnvironmentConstants.MAX_GRAVITY)
        assertEquals(0.0, EnvironmentConstants.MIN_GRADIENT)
        assertEquals(-2.0, EnvironmentConstants.MAX_GRADIENT)
    }

    @Test
    fun snapshotsDefensivelyCopyCollections() {
        val color = mutableListOf(0.1f, 0.2f, 0.3f)
        val signals = mutableListOf(1.0f, 2.0f, 3.0f, 4.0f)
        val cell = cellSnapshot(color, signals)
        val genePool = mutableListOf(true, false)
        val mutationWeights = mutableListOf(0.25, 0.75)
        val environment = environmentSnapshot(genePool, mutationWeights)

        color[0] = 9.0f
        signals[0] = 9.0f
        genePool[0] = false
        mutationWeights[0] = 9.0

        assertEquals(0.1f, cell.color[0])
        assertEquals(1.0f, cell.signalSubstance[0])
        assertEquals(true, environment.genePool[0])
        assertEquals(0.25, environment.mutationWeights[0])
        assertFalse(cell.color is MutableList<*>)
        assertFalse(environment.genePool is MutableList<*>)
    }

    @Test
    fun genomeDraftDefensivelyCopiesEditableValues() {
        val color = mutableListOf(0.1f, 0.2f, 0.3f)
        val parameters = MutableList(GenomeContract.PARAMETRIC_VALUE_COUNT) {
            ParametricValueDraft(1f, 2f, 3f, 4, 1)
        }
        val integers = MutableList(GenomeContract.INTEGER_VALUE_COUNT) { 5 }
        val gene = geneDraft(color, parameters, integers)
        val geneCount = 3
        val genes = MutableList(geneCount) { index ->
            gene.copy(initial = index == 0)
        }
        val genome = GenomeDraft(genes)

        color[0] = 9.0f
        parameters.clear()
        integers[0] = 9
        genes.clear()

        assertEquals(0.1f, gene.color[0])
        assertEquals(GenomeContract.PARAMETRIC_VALUE_COUNT, gene.parametricValues.size)
        assertEquals(5, gene.integerValues[0])
        assertEquals(geneCount, genome.genes.size)
        assertFalse(gene.integerValues is MutableList<*>)
        assertFalse(genome.genes is MutableList<*>)
    }

    @Test
    fun typedResultKeepsFailureCode() {
        val result: CellularityResult<Int> = CellularityResult.Failure(
            CellularityError(CellularityErrorCode.INVALID_HEADER, "Invalid header")
        )

        val failure = assertIs<CellularityResult.Failure>(result)
        assertEquals(CellularityErrorCode.INVALID_HEADER, failure.error.code)
    }

    private fun cellSnapshot(color: Iterable<Float>, signals: Iterable<Float>) = CellSnapshot(
        id = 1,
        positionX = 0.0,
        positionY = 0.0,
        positionZ = 0.0,
        radius = 0.01,
        age = 1.0,
        mass = 0.1,
        nitrates = 0.2,
        velocityX = 0.0,
        velocityY = 0.0,
        angle = 0.0,
        angularVelocity = 0.0,
        toxin = 0.0f,
        toxication = 0.0f,
        lipids = 0.0f,
        geneIndex = 0,
        originTag = 0,
        selectTag = 0,
        mutationCount = 0,
        telomeresCount = -1,
        mirrored = false,
        cellType = CellType.PHAGOCYTE,
        color = color,
        signalSubstance = signals,
        newSignalSubstance = signals
    )

    private fun environmentSnapshot(
        genePool: Iterable<Boolean>,
        mutationWeights: Iterable<Double>
    ) = EnvironmentSnapshot(
        revision = 1,
        addRandom = false,
        dieEdge = false,
        onlyTweakMutations = false,
        mutationOnlyAfterSplit = false,
        aging = true,
        mobileFood = false,
        gravityField = VectorFieldSnapshot(VectorFieldType.LINEAR_DIRECTIONAL, 0.0, 0.0),
        densityField = ScalarFieldSnapshot(ScalarFieldType.CONSTANT, 0.0, 0.0, 0.0, 0.0, 0.0),
        salinityField = ScalarFieldSnapshot(ScalarFieldType.CONSTANT, 0.0, 0.0, 0.0, 0.0, 0.0),
        foodLipidLayerField = ScalarFieldSnapshot(ScalarFieldType.CONSTANT, 0.0, 0.0, 0.0, 0.0, 0.0),
        radiation = 0.0,
        testMulticellularCoef = 0.0,
        testMutationCoef = 0.0,
        testFoodDecay = 0.0,
        testIdleMetabolismScale = 1.0,
        testFlagellocyteCostScale = 1.0,
        testPhagocyteEatSpeedScale = 1.0,
        viscosity = 0.0,
        frictionDynamic = 0.0,
        frictionStatic = 0.0,
        foodSpeed = 0.0,
        foodSize = 0.0,
        foodLumpiness = 0.0,
        foodLumpSize = 0.0,
        foodLipidLayer = 0.0,
        salinity = 0.0,
        lightIntensity = 0.0,
        lightRange = 0.0,
        lightSpeed = 0.0,
        gravity = 0.0,
        density = 0.0,
        densityGradient = 0.0,
        nitrates = 0.0,
        genePool = genePool,
        mutationWeights = mutationWeights
    )

    private fun geneDraft(
        color: Iterable<Float>,
        parameters: Iterable<ParametricValueDraft>,
        integers: Iterable<Int>
    ) = GeneDraft(
        color = color,
        splitMass = 0.1f,
        splitRatio = 0.5f,
        splitAngle = 0.0f,
        child1Angle = 0.0f,
        child2Angle = 0.0f,
        massFlowPriority = 1.0f,
        linkStrength = 1.0f,
        child1GeneIndex = 0,
        child2GeneIndex = 0,
        makeLink = false,
        child1KeepLinks = true,
        child2KeepLinks = true,
        stayAlive = true,
        initial = true,
        child1Mirror = false,
        child2Mirror = false,
        cellType = CellType.PHAGOCYTE,
        parametricValues = parameters,
        integerValues = integers,
        floatValues = GeneFloatValues(0f, 0f, 0f, 0f, 0f, 0f, 1f)
    )
}
