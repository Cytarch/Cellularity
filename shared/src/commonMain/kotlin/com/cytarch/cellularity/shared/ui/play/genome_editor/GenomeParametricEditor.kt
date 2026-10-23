package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GenomeFloatRange
import com.cytarch.cellularity.shared.model.ParametricValueDraft
import kotlin.math.sin

data class GenomeParametricEditorValues(
    val fixedValue: Float,
    val variableA: Float,
    val variableB: Float,
    val variableC: Float
)

data class GenomeParametricEditorBounds(
    val variableA: GenomeFloatRange,
    val variableB: GenomeFloatRange,
    val variableC: GenomeFloatRange
)

/** Converts the compact wire coefficients to the physical values shown by the editor. */
object GenomeParametricEditor {
    fun decode(
        value: ParametricValueDraft,
        minimum: Float,
        maximum: Float
    ): GenomeParametricEditorValues {
        require(maximum > minimum)
        val center = center(minimum, maximum)
        val halfRange = halfRange(minimum, maximum)
        return GenomeParametricEditorValues(
            fixedValue = center + halfRange * value.variableB,
            variableA = when (value.formulaType.toInt()) {
                2 -> center + halfRange * value.variableA
                3 -> value.variableA
                else -> halfRange * value.variableA
            },
            variableB = if (value.formulaType.toInt() == 3) {
                value.variableB
            } else {
                center + halfRange * value.variableB
            },
            variableC = value.variableC
        )
    }

    fun bounds(
        formulaType: Int,
        minimum: Float,
        maximum: Float
    ): GenomeParametricEditorBounds {
        require(formulaType in 0..3)
        require(maximum > minimum)
        val center = center(minimum, maximum)
        val halfRange = halfRange(minimum, maximum)
        val variableLimit = CellDataConstants.VALUE_VARIABLE_MAX
        return GenomeParametricEditorBounds(
            variableA = when (formulaType) {
                2 -> GenomeFloatRange(minimum, maximum)
                3 -> GenomeFloatRange(-1.0f, 1.0f)
                else -> GenomeFloatRange(-halfRange * variableLimit, halfRange * variableLimit)
            },
            variableB = when (formulaType) {
                2 -> GenomeFloatRange(minimum, maximum)
                3 -> GenomeFloatRange(-variableLimit, variableLimit)
                else -> GenomeFloatRange(
                    center - halfRange * variableLimit,
                    center + halfRange * variableLimit
                )
            },
            variableC = if (formulaType == 3) {
                GenomeFloatRange(-variableLimit, variableLimit)
            } else {
                GenomeFloatRange(-1.0f, 1.0f)
            }
        )
    }

    fun encode(
        formulaType: Int,
        signalChannel: Int,
        values: GenomeParametricEditorValues,
        minimum: Float,
        maximum: Float
    ): ParametricValueDraft {
        require(formulaType in 0..3)
        require(signalChannel in 0 until com.cytarch.cellularity.shared.model.GenomeContract.SIGNAL_INPUT_COUNT)
        require(maximum > minimum)
        val center = center(minimum, maximum)
        val halfRange = halfRange(minimum, maximum)
        return when (formulaType) {
            0 -> ParametricValueDraft(
                variableA = 0.0f,
                variableB = normalizeByHalfRange(values.fixedValue, center, halfRange, 1.0f),
                variableC = 0.0f,
                signalSubstanceChannelIndex = signalChannel.toShort(),
                formulaType = 0
            )
            1 -> ParametricValueDraft(
                variableA = normalizeByScale(
                    values.variableA,
                    halfRange,
                    CellDataConstants.VALUE_VARIABLE_MAX
                ),
                variableB = normalizeByHalfRange(
                    values.variableB,
                    center,
                    halfRange,
                    CellDataConstants.VALUE_VARIABLE_MAX
                ),
                variableC = values.variableC.coerceIn(-1.0f, 1.0f),
                signalSubstanceChannelIndex = signalChannel.toShort(),
                formulaType = 1
            )
            2 -> ParametricValueDraft(
                variableA = normalizeByHalfRange(values.variableA, center, halfRange, 1.0f),
                variableB = normalizeByHalfRange(values.variableB, center, halfRange, 1.0f),
                variableC = values.variableC.coerceIn(-1.0f, 1.0f),
                signalSubstanceChannelIndex = signalChannel.toShort(),
                formulaType = 2
            )
            else -> ParametricValueDraft(
                variableA = values.variableA.coerceIn(-1.0f, 1.0f),
                variableB = values.variableB.coerceIn(
                    -CellDataConstants.VALUE_VARIABLE_MAX,
                    CellDataConstants.VALUE_VARIABLE_MAX
                ),
                variableC = values.variableC.coerceIn(
                    -CellDataConstants.VALUE_VARIABLE_MAX,
                    CellDataConstants.VALUE_VARIABLE_MAX
                ),
                signalSubstanceChannelIndex = signalChannel.toShort(),
                formulaType = 3
            )
        }
    }

    fun evaluatePhysical(
        formulaType: Int,
        input: Float,
        values: GenomeParametricEditorValues,
        minimum: Float,
        maximum: Float
    ): Float {
        require(formulaType in 0..3)
        require(maximum > minimum)
        if (formulaType == 0) return values.fixedValue.coerceIn(minimum, maximum)
        val center = center(minimum, maximum)
        val halfRange = halfRange(minimum, maximum)
        val result = when (formulaType) {
            1 -> input * normalizeByScale(
                values.variableA,
                halfRange,
                CellDataConstants.VALUE_VARIABLE_MAX
            ) + normalizeByHalfRange(
                values.variableB,
                center,
                halfRange,
                CellDataConstants.VALUE_VARIABLE_MAX
            )
            2 -> if (input < values.variableC) {
                normalizeByHalfRange(values.variableA, center, halfRange, 1.0f)
            } else {
                normalizeByHalfRange(values.variableB, center, halfRange, 1.0f)
            }
            else -> values.variableA.coerceIn(-1.0f, 1.0f) *
                sin((input * values.variableC - values.variableB).toDouble()).toFloat()
        }
        return (center + halfRange * result.coerceIn(-1.0f, 1.0f))
            .coerceIn(minimum, maximum)
    }

    private fun center(minimum: Float, maximum: Float): Float = (minimum + maximum) * 0.5f

    private fun halfRange(minimum: Float, maximum: Float): Float = (maximum - minimum) * 0.5f

    private fun normalizeByHalfRange(
        value: Float,
        center: Float,
        halfRange: Float,
        limit: Float
    ): Float = if (!value.isFinite() || halfRange == 0.0f) {
        0.0f
    } else {
        ((value - center) / halfRange).coerceIn(-limit, limit)
    }

    private fun normalizeByScale(value: Float, scale: Float, limit: Float): Float =
        if (!value.isFinite() || scale == 0.0f) {
            0.0f
        } else {
            (value / scale).coerceIn(-limit, limit)
        }
}
