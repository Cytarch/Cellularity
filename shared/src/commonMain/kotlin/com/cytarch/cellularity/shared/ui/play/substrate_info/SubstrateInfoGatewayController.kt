package com.cytarch.cellularity.shared.ui.play.substrate_info

import com.cytarch.cellularity.shared.model.EnvironmentFieldType
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.EnvironmentBooleanProperty
import com.cytarch.cellularity.shared.simulation.EnvironmentChange
import com.cytarch.cellularity.shared.simulation.EnvironmentField
import com.cytarch.cellularity.shared.simulation.EnvironmentFieldComponent
import com.cytarch.cellularity.shared.simulation.EnvironmentPatch
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejectionReason
import com.cytarch.cellularity.shared.simulation.EnvironmentScalarProperty
import com.cytarch.cellularity.shared.simulation.SimulationGateway
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Maps Substrate Info actions to serialized, observable environment edits. */
class SubstrateInfoGatewayController(
    private val gateway: SimulationGateway
) {
    private val applyMutex = Mutex()
    private val mutableEditStatus = MutableStateFlow<SubstrateEditStatus>(SubstrateEditStatus.Idle)

    val editStatus: StateFlow<SubstrateEditStatus> = mutableEditStatus.asStateFlow()

    suspend fun changeCheckbox(type: EnvironmentCheckboxType, value: Boolean): ApplyResult =
        apply(
            change = EnvironmentChange.BooleanValue(
                property = when (type) {
                    EnvironmentCheckboxType.ADD_RANDOM -> EnvironmentBooleanProperty.ADD_RANDOM
                    EnvironmentCheckboxType.DIE_EDGE -> EnvironmentBooleanProperty.DIE_EDGE
                    EnvironmentCheckboxType.TWEAK -> EnvironmentBooleanProperty.ONLY_TWEAK_MUTATIONS
                    EnvironmentCheckboxType.MUTATION_ONLY_AFTER_SPLIT ->
                        EnvironmentBooleanProperty.MUTATION_ONLY_AFTER_SPLIT
                    EnvironmentCheckboxType.AGING -> EnvironmentBooleanProperty.AGING
                    EnvironmentCheckboxType.MOBILE_FOOD -> EnvironmentBooleanProperty.MOBILE_FOOD
                },
                value = value
            ),
            target = SubstrateEditTarget.Checkbox(type)
        )

    suspend fun changeSlider(type: SubstrateSliderType, value: Double): ApplyResult =
        apply(
            change = EnvironmentChange.ScalarValue(
                property = when (type) {
                    SubstrateSliderType.RADIATION -> EnvironmentScalarProperty.RADIATION
                    SubstrateSliderType.TEST_MULTICELLULAR_COEF ->
                        EnvironmentScalarProperty.TEST_MULTICELLULAR_COEF
                    SubstrateSliderType.TEST_MUTATION_COEF ->
                        EnvironmentScalarProperty.TEST_MUTATION_COEF
                    SubstrateSliderType.TEST_FOOD_DECAY -> EnvironmentScalarProperty.TEST_FOOD_DECAY
                    SubstrateSliderType.TEST_IDLE_METABOLISM_SCALE ->
                        EnvironmentScalarProperty.TEST_IDLE_METABOLISM_SCALE
                    SubstrateSliderType.TEST_FLAGELLOCYTE_COST_SCALE ->
                        EnvironmentScalarProperty.TEST_FLAGELLOCYTE_COST_SCALE
                    SubstrateSliderType.TEST_PHAGOCYTE_EAT_SPEED_SCALE ->
                        EnvironmentScalarProperty.TEST_PHAGOCYTE_EAT_SPEED_SCALE
                    SubstrateSliderType.VISCOSITY -> EnvironmentScalarProperty.VISCOSITY
                    SubstrateSliderType.FRICTION_DYNAMIC -> EnvironmentScalarProperty.FRICTION_DYNAMIC
                    SubstrateSliderType.FRICTION_STATIC -> EnvironmentScalarProperty.FRICTION_STATIC
                    SubstrateSliderType.FOOD_SPEED -> EnvironmentScalarProperty.FOOD_SPEED
                    SubstrateSliderType.FOOD_SIZE -> EnvironmentScalarProperty.FOOD_SIZE
                    SubstrateSliderType.FOOD_LUMPINESS -> EnvironmentScalarProperty.FOOD_LUMPINESS
                    SubstrateSliderType.FOOD_LUMP_SIZE -> EnvironmentScalarProperty.FOOD_LUMP_SIZE
                    SubstrateSliderType.FOOD_LIPID_LAYER -> EnvironmentScalarProperty.FOOD_LIPID_LAYER
                    SubstrateSliderType.SALINITY -> EnvironmentScalarProperty.SALINITY
                    SubstrateSliderType.LIGHT_INTENSITY -> EnvironmentScalarProperty.LIGHT_INTENSITY
                    SubstrateSliderType.LIGHT_RANGE -> EnvironmentScalarProperty.LIGHT_RANGE
                    SubstrateSliderType.LIGHT_SPEED -> EnvironmentScalarProperty.LIGHT_SPEED
                    SubstrateSliderType.GRAVITY -> EnvironmentScalarProperty.GRAVITY
                    SubstrateSliderType.DENSITY -> EnvironmentScalarProperty.DENSITY
                    SubstrateSliderType.DENSITY_GRADIENT -> EnvironmentScalarProperty.DENSITY_GRADIENT
                    SubstrateSliderType.NITRATES -> EnvironmentScalarProperty.NITRATES
                },
                value = value
            ),
            target = SubstrateEditTarget.Slider(type)
        )

    suspend fun changeFieldType(
        target: EnvironmentFieldTarget,
        type: EnvironmentFieldType
    ): ApplyResult = apply(
        change = EnvironmentChange.FieldType(target.toSimulationField(), type),
        target = SubstrateEditTarget.FieldType(target)
    )

    suspend fun changeFieldValue(
        target: EnvironmentFieldTarget,
        component: DensityFieldValueType,
        value: Double
    ): ApplyResult = apply(
        change = EnvironmentChange.FieldValue(
            field = target.toSimulationField(),
            component = when (component) {
                DensityFieldValueType.BASE -> EnvironmentFieldComponent.BASE
                DensityFieldValueType.STRENGTH -> EnvironmentFieldComponent.STRENGTH
                DensityFieldValueType.ANGLE -> EnvironmentFieldComponent.ANGLE
                DensityFieldValueType.OFFSET -> EnvironmentFieldComponent.OFFSET
                DensityFieldValueType.SOFTNESS -> EnvironmentFieldComponent.SOFTNESS
            },
            value = value
        ),
        target = SubstrateEditTarget.FieldValue(target, component)
    )

    suspend fun changeMutationWeight(index: Int, value: Double): ApplyResult =
        apply(
            change = EnvironmentChange.MutationWeight(index, value),
            target = SubstrateEditTarget.MutationWeight(index)
        )

    suspend fun resetMutationWeights(): ApplyResult =
        apply(
            change = EnvironmentChange.ResetMutationWeights,
            target = SubstrateEditTarget.ResetMutationWeights
        )

    suspend fun changeGenePool(index: Int, enabled: Boolean): ApplyResult =
        apply(
            change = EnvironmentChange.GenePoolEnabled(index, enabled),
            target = SubstrateEditTarget.GenePool(index)
        )

    private suspend fun apply(
        change: EnvironmentChange,
        target: SubstrateEditTarget
    ): ApplyResult = applyMutex.withLock {
        mutableEditStatus.value = SubstrateEditStatus.Pending(target)
        try {
            applyWithRevisionRetry(change).also { result ->
                mutableEditStatus.value = when (result) {
                    is ApplyResult.Applied -> SubstrateEditStatus.Applied(target, result.revision)
                    is ApplyResult.Rejected -> SubstrateEditStatus.Rejected(
                        target = target,
                        currentRevision = result.currentRevision,
                        reasons = result.rejections.map { it.reason }
                    )
                    is ApplyResult.Failure -> SubstrateEditStatus.Failed(target, result.error)
                }
            }
        } catch (failure: Throwable) {
            mutableEditStatus.value = SubstrateEditStatus.Idle
            throw failure
        }
    }

    private suspend fun applyWithRevisionRetry(change: EnvironmentChange): ApplyResult {
        var baseRevision = gateway.environment.value.revision
        var lastResult: ApplyResult? = null
        repeat(MAX_REVISION_ATTEMPTS) {
            val result = gateway.applyEnvironment(
                EnvironmentPatch.single(change = change, baseRevision = baseRevision)
            )
            lastResult = result
            val revisionConflict = result is ApplyResult.Rejected &&
                result.rejections.all {
                    it.reason == EnvironmentPatchRejectionReason.REVISION_CONFLICT
                }
            if (!revisionConflict) return result
            baseRevision = (result as ApplyResult.Rejected).currentRevision
        }
        return checkNotNull(lastResult)
    }

    private companion object {
        const val MAX_REVISION_ATTEMPTS = 2
    }
}

private fun EnvironmentFieldTarget.toSimulationField(): EnvironmentField = when (this) {
    EnvironmentFieldTarget.GRAVITY -> EnvironmentField.GRAVITY
    EnvironmentFieldTarget.DENSITY -> EnvironmentField.DENSITY
    EnvironmentFieldTarget.SALINITY -> EnvironmentField.SALINITY
    EnvironmentFieldTarget.FOOD_LIPID_LAYER -> EnvironmentField.FOOD_LIPID_LAYER
}
