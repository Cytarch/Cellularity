package com.cytarch.cellularity.shared.simulation

import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.SimulationStats
import com.cytarch.cellularity.shared.model.VectorFieldSnapshot
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.ui.play.substrate_info.DensityFieldValueType
import com.cytarch.cellularity.shared.ui.play.substrate_info.EnvironmentCheckboxType
import com.cytarch.cellularity.shared.ui.play.substrate_info.EnvironmentFieldTarget
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoGatewayController
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateSliderType
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs

class SimulationGatewayContractTest {
    @Test
    fun fakeExposesFlowsAndRecordsCoarseOperations() = runBlocking {
        val gateway = FakeSimulationGateway(environment(revision = 7))
        val target = StoredSubstrateId("sample")

        assertEquals(SimulationSessionState.Ready, gateway.state.value)
        assertEquals(7, gateway.environment.value.revision)

        assertEquals(SimulationResult.Queued, gateway.execute(SimulationCommand.Reset))
        assertEquals(SimulationCommand.Reset, gateway.commands.single())
        assertEquals(SaveResult.Saved(target), gateway.save(target))
        assertEquals(listOf<StoredSubstrateId?>(target), gateway.saveTargets)

        gateway.emitStats(SimulationStats(cellCount = 3, foodMass = 2.0, time = 1.0))
        gateway.emitEnvironment(environment(revision = 8))
        assertEquals(3, gateway.stats.value.cellCount)
        assertEquals(8, gateway.environment.value.revision)

        gateway.close()
        gateway.close()
        assertEquals(SimulationSessionState.Closed, gateway.state.value)
        assertEquals(1, gateway.closeCount)
        assertIs<SimulationResult.Failure>(gateway.execute(SimulationCommand.Reset))
        Unit
    }

    @Test
    fun environmentPatchOwnsItsChangeList() {
        val source = mutableListOf<EnvironmentChange>(
            EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, true)
        )
        val patch = EnvironmentPatch(baseRevision = 4, changes = source)
        source.clear()

        assertEquals(1, patch.changes.size)
        assertFalse(patch.changes is MutableList<*>)
    }

    @Test
    fun substrateControllerMapsEveryCheckboxAndSlider() = runBlocking {
        val gateway = FakeSimulationGateway(environment(revision = 42))
        val controller = SubstrateInfoGatewayController(gateway)
        val expectedBooleans = listOf(
            EnvironmentBooleanProperty.ADD_RANDOM,
            EnvironmentBooleanProperty.DIE_EDGE,
            EnvironmentBooleanProperty.ONLY_TWEAK_MUTATIONS,
            EnvironmentBooleanProperty.MUTATION_ONLY_AFTER_SPLIT,
            EnvironmentBooleanProperty.AGING,
            EnvironmentBooleanProperty.MOBILE_FOOD
        )
        val expectedScalars = listOf(
            EnvironmentScalarProperty.RADIATION,
            EnvironmentScalarProperty.TEST_MULTICELLULAR_COEF,
            EnvironmentScalarProperty.TEST_MUTATION_COEF,
            EnvironmentScalarProperty.TEST_FOOD_DECAY,
            EnvironmentScalarProperty.TEST_IDLE_METABOLISM_SCALE,
            EnvironmentScalarProperty.TEST_FLAGELLOCYTE_COST_SCALE,
            EnvironmentScalarProperty.TEST_PHAGOCYTE_EAT_SPEED_SCALE,
            EnvironmentScalarProperty.VISCOSITY,
            EnvironmentScalarProperty.FRICTION_DYNAMIC,
            EnvironmentScalarProperty.FRICTION_STATIC,
            EnvironmentScalarProperty.FOOD_SPEED,
            EnvironmentScalarProperty.FOOD_SIZE,
            EnvironmentScalarProperty.FOOD_LUMPINESS,
            EnvironmentScalarProperty.FOOD_LUMP_SIZE,
            EnvironmentScalarProperty.FOOD_LIPID_LAYER,
            EnvironmentScalarProperty.SALINITY,
            EnvironmentScalarProperty.LIGHT_INTENSITY,
            EnvironmentScalarProperty.LIGHT_RANGE,
            EnvironmentScalarProperty.LIGHT_SPEED,
            EnvironmentScalarProperty.GRAVITY,
            EnvironmentScalarProperty.DENSITY,
            EnvironmentScalarProperty.DENSITY_GRADIENT,
            EnvironmentScalarProperty.NITRATES
        )

        EnvironmentCheckboxType.entries.forEach { controller.changeCheckbox(it, true) }
        SubstrateSliderType.entries.forEach { controller.changeSlider(it, 1.25) }

        val booleanChanges = gateway.environmentPatches
            .take(EnvironmentCheckboxType.entries.size)
            .map { it.changes.single() as EnvironmentChange.BooleanValue }
        val scalarChanges = gateway.environmentPatches
            .drop(EnvironmentCheckboxType.entries.size)
            .map { it.changes.single() as EnvironmentChange.ScalarValue }
        assertEquals(expectedBooleans, booleanChanges.map { it.property })
        assertEquals(expectedScalars, scalarChanges.map { it.property })
        assertEquals(List(gateway.environmentPatches.size) { 42L },
            gateway.environmentPatches.map { it.baseRevision })
    }

    @Test
    fun substrateControllerMapsFieldsMutationAndGenePool() = runBlocking {
        val gateway = FakeSimulationGateway(environment(revision = 9))
        val controller = SubstrateInfoGatewayController(gateway)

        controller.changeFieldType(EnvironmentFieldTarget.GRAVITY, VectorFieldType.RADIAL_INWARD)
        controller.changeFieldType(EnvironmentFieldTarget.DENSITY, ScalarFieldType.RADIAL)
        controller.changeFieldValue(
            EnvironmentFieldTarget.SALINITY,
            DensityFieldValueType.OFFSET,
            0.4
        )
        controller.changeMutationWeight(3, 0.7)
        controller.resetMutationWeights()
        controller.changeGenePool(2, false)

        val changes = gateway.environmentPatches.map { it.changes.single() }
        assertEquals(
            EnvironmentChange.FieldType(EnvironmentField.GRAVITY, VectorFieldType.RADIAL_INWARD),
            changes[0]
        )
        assertEquals(
            EnvironmentChange.FieldType(EnvironmentField.DENSITY, ScalarFieldType.RADIAL),
            changes[1]
        )
        assertEquals(
            EnvironmentChange.FieldValue(
                EnvironmentField.SALINITY,
                EnvironmentFieldComponent.OFFSET,
                0.4
            ),
            changes[2]
        )
        assertEquals(EnvironmentChange.MutationWeight(3, 0.7), changes[3])
        assertIs<EnvironmentChange.ResetMutationWeights>(changes[4])
        assertEquals(EnvironmentChange.GenePoolEnabled(2, false), changes[5])
    }

    @Test
    fun substrateControllerRebasesOneAbsoluteChangeAfterRevisionConflict() = runBlocking {
        val gateway = FakeSimulationGateway(environment(revision = 9)).apply {
            queuedApplyResults += ApplyResult.Rejected(
                currentRevision = 10,
                rejections = listOf(
                    EnvironmentPatchRejection(
                        changeIndex = -1,
                        reason = EnvironmentPatchRejectionReason.REVISION_CONFLICT
                    )
                )
            )
            queuedApplyResults += ApplyResult.Applied(11)
        }
        val controller = SubstrateInfoGatewayController(gateway)

        assertEquals(
            ApplyResult.Applied(11),
            controller.changeCheckbox(EnvironmentCheckboxType.AGING, false)
        )
        assertEquals(listOf(9L, 10L), gateway.environmentPatches.map { it.baseRevision })
        assertEquals(
            gateway.environmentPatches[0].changes,
            gateway.environmentPatches[1].changes
        )
    }
}

private fun environment(revision: Long): EnvironmentSnapshot = EnvironmentSnapshot(
    revision = revision,
    addRandom = false,
    dieEdge = false,
    onlyTweakMutations = false,
    mutationOnlyAfterSplit = false,
    aging = true,
    mobileFood = false,
    gravityField = VectorFieldSnapshot(VectorFieldType.LINEAR_DIRECTIONAL, 0.0, 0.0),
    densityField = scalarField(),
    salinityField = scalarField(),
    foodLipidLayerField = scalarField(),
    radiation = 0.0,
    testMulticellularCoef = 1.0,
    testMutationCoef = 1.0,
    testFoodDecay = 1.0,
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
    genePool = emptyList(),
    mutationWeights = emptyList()
)

private fun scalarField() = ScalarFieldSnapshot(
    type = ScalarFieldType.CONSTANT,
    base = 0.0,
    strength = 0.0,
    angle = 0.0,
    offset = 0.0,
    softness = 0.0
)
