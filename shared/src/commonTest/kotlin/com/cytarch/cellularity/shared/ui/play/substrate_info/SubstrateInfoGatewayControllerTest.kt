package com.cytarch.cellularity.shared.ui.play.substrate_info

import com.cytarch.cellularity.shared.model.CellularityError
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldSnapshot
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldSnapshot
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejection
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejectionReason
import com.cytarch.cellularity.shared.simulation.FakeSimulationGateway
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNull

class SubstrateInfoGatewayControllerTest {
    @Test
    fun exposesPendingThenAppliedStatusForTheSameTarget() = runBlocking {
        val enteredApply = CompletableDeferred<Unit>()
        val finishApply = CompletableDeferred<Unit>()
        val gateway = FakeSimulationGateway(environment(revision = 4)).apply {
            beforeEnvironmentApply = {
                enteredApply.complete(Unit)
                finishApply.await()
            }
        }
        val controller = SubstrateInfoGatewayController(gateway)
        val target = SubstrateEditTarget.Slider(SubstrateSliderType.VISCOSITY)

        val result = async {
            controller.changeSlider(SubstrateSliderType.VISCOSITY, 12.0)
        }
        enteredApply.await()
        assertEquals(SubstrateEditStatus.Pending(target), controller.editStatus.value)

        finishApply.complete(Unit)
        assertEquals(ApplyResult.Applied(5), result.await())
        assertEquals(SubstrateEditStatus.Applied(target, 5), controller.editStatus.value)
        assertEquals(SubstrateEditFeedbackKind.APPLIED, controller.editStatus.value.feedbackKind())
        assertNull(controller.editStatus.value.visibleFeedbackKind())
    }

    @Test
    fun rejectedResultPreservesTargetRevisionAndDisplayReason() = runBlocking {
        val gateway = FakeSimulationGateway(environment(revision = 8)).apply {
            queuedApplyResults += ApplyResult.Rejected(
                currentRevision = 8,
                rejections = listOf(
                    EnvironmentPatchRejection(
                        changeIndex = 0,
                        reason = EnvironmentPatchRejectionReason.OUT_OF_RANGE
                    )
                )
            )
        }
        val controller = SubstrateInfoGatewayController(gateway)

        assertIs<ApplyResult.Rejected>(
            controller.changeSlider(SubstrateSliderType.DENSITY, 100.0)
        )
        val status = assertIs<SubstrateEditStatus.Rejected>(controller.editStatus.value)
        assertEquals(SubstrateEditTarget.Slider(SubstrateSliderType.DENSITY), status.target)
        assertEquals(8, status.currentRevision)
        assertEquals(listOf(EnvironmentPatchRejectionReason.OUT_OF_RANGE), status.reasons)
        assertEquals(SubstrateEditFeedbackKind.INVALID_VALUE, status.feedbackKind())
    }

    @Test
    fun repeatedRevisionConflictIsDisplayedAfterTheSingleRetry() = runBlocking {
        val gateway = FakeSimulationGateway(environment(revision = 9)).apply {
            queuedApplyResults += revisionConflict(currentRevision = 10)
            queuedApplyResults += revisionConflict(currentRevision = 11)
        }
        val controller = SubstrateInfoGatewayController(gateway)

        val result = assertIs<ApplyResult.Rejected>(
            controller.changeCheckbox(EnvironmentCheckboxType.AGING, false)
        )
        assertEquals(11, result.currentRevision)
        assertEquals(listOf(9L, 10L), gateway.environmentPatches.map { it.baseRevision })
        assertEquals(
            SubstrateEditFeedbackKind.REVISION_CONFLICT,
            controller.editStatus.value.feedbackKind()
        )
    }

    @Test
    fun failureIsExposedInsteadOfLeavingTheUiPending() = runBlocking {
        val error = CellularityError(CellularityErrorCode.INTERNAL_FAILURE, "apply failed")
        val gateway = FakeSimulationGateway(environment(revision = 3)).apply { fail(error) }
        val controller = SubstrateInfoGatewayController(gateway)

        assertEquals(
            ApplyResult.Failure(error),
            controller.changeGenePool(index = 2, enabled = false)
        )
        assertEquals(
            SubstrateEditStatus.Failed(SubstrateEditTarget.GenePool(2), error),
            controller.editStatus.value
        )
        assertEquals(SubstrateEditFeedbackKind.FAILED, controller.editStatus.value.feedbackKind())
    }

    @Test
    fun everyNativeRejectionHasDeterministicUserFeedback() {
        fun feedback(vararg reasons: EnvironmentPatchRejectionReason) =
            SubstrateEditStatus.Rejected(
                target = SubstrateEditTarget.ResetMutationWeights,
                currentRevision = 1,
                reasons = reasons.asList()
            ).feedbackKind()

        assertEquals(
            SubstrateEditFeedbackKind.EMPTY_GENE_POOL,
            feedback(
                EnvironmentPatchRejectionReason.OUT_OF_RANGE,
                EnvironmentPatchRejectionReason.EMPTY_GENE_POOL
            )
        )
        assertEquals(
            SubstrateEditFeedbackKind.REVISION_CONFLICT,
            feedback(EnvironmentPatchRejectionReason.REVISION_CONFLICT)
        )
        assertEquals(
            SubstrateEditFeedbackKind.INVALID_VALUE,
            feedback(EnvironmentPatchRejectionReason.NON_FINITE_VALUE)
        )
        val genericReasons = EnvironmentPatchRejectionReason.entries.filterNot {
            it == EnvironmentPatchRejectionReason.EMPTY_GENE_POOL ||
                it == EnvironmentPatchRejectionReason.REVISION_CONFLICT ||
                it == EnvironmentPatchRejectionReason.NON_FINITE_VALUE ||
                it == EnvironmentPatchRejectionReason.OUT_OF_RANGE
        }
        genericReasons.forEach { reason ->
            assertEquals(SubstrateEditFeedbackKind.REJECTED, feedback(reason), reason.name)
        }
    }
}

private fun revisionConflict(currentRevision: Long): ApplyResult.Rejected =
    ApplyResult.Rejected(
        currentRevision = currentRevision,
        rejections = listOf(
            EnvironmentPatchRejection(
                changeIndex = -1,
                reason = EnvironmentPatchRejectionReason.REVISION_CONFLICT
            )
        )
    )

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
    genePool = List(18) { true },
    mutationWeights = List(106) { 1.0 }
)

private fun scalarField() = ScalarFieldSnapshot(
    type = ScalarFieldType.CONSTANT,
    base = 0.0,
    strength = 0.0,
    angle = 0.0,
    offset = 0.0,
    softness = 0.0
)
