package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.LinkConstants
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sign

/** Pure conversions retained from the original editor and shared by reducer and UI adapters. */
object GenomeEditorMath {
    fun snap(value: Float, steps: Int, minimum: Float, maximum: Float): Float {
        require(steps > 0) { "Snap steps must be positive" }
        require(maximum > minimum) { "Snap range must be increasing" }
        if (!value.isFinite()) return value
        return minimum + (maximum - minimum) *
            (steps * (value - minimum) / (maximum - minimum)).roundToInt() / steps
    }

    fun snapGene(gene: GeneDraft, steps: Int = GenomeEditorConfig.COARSE_STEPS): GeneDraft {
        val snappedFloats = GeneFloatType.entries.fold(gene.floatValues) { values, type ->
            val range = GenomeContract.floatRange(type)
            values.withValue(type, snap(values[type], steps, range.min, range.max))
        }
        val snappedParametric = gene.parametricValues.map { value ->
            if (value.signalSubstanceChannelIndex.toInt() == -1) {
                value.copy(variableB = snap(value.variableB, steps, -1.0f, 1.0f))
            } else {
                value
            }
        }
        val priority = if (gene.massFlowPriority.isFinite() && gene.massFlowPriority > 0.0f) {
            exp(
                snap(
                    ln(gene.massFlowPriority.toDouble()).toFloat(),
                    steps,
                    ln(CellDataConstants.MIN_MASS_FLOW_PRIORITY.toDouble()).toFloat(),
                    ln(CellDataConstants.MAX_MASS_FLOW_PRIORITY.toDouble()).toFloat()
                )
            )
        } else {
            gene.massFlowPriority
        }
        return gene.copy(
            splitAngle = snap(gene.splitAngle, steps, 0.0f, (2.0 * PI).toFloat()),
            child1Angle = snap(gene.child1Angle, steps, 0.0f, (2.0 * PI).toFloat()),
            child2Angle = snap(gene.child2Angle, steps, 0.0f, (2.0 * PI).toFloat()),
            color = gene.color.map { snap(it, steps, 0.0f, 1.0f) },
            linkStrength = snap(gene.linkStrength, steps, 0.0f, LinkConstants.MAX_STIFFNESS),
            splitMass = snapSplitMass(gene.splitMass, steps),
            massFlowPriority = priority,
            splitRatio = snap(
                gene.splitRatio,
                steps,
                CellDataConstants.MIN_RATIO,
                1.0f - CellDataConstants.MIN_RATIO
            ),
            parametricValues = snappedParametric,
            floatValues = snappedFloats
        )
    }

    fun snapGenome(genome: GenomeDraft): GenomeDraft = GenomeDraft(
        genome.genes.map(::snapGene)
    )

    fun isSnapped(genome: GenomeDraft, steps: Int = GenomeEditorConfig.COARSE_STEPS): Boolean =
        genome.genes.all { gene ->
            exactly(gene.splitAngle, snap(gene.splitAngle, steps, 0.0f, (2.0 * PI).toFloat())) &&
                exactly(gene.child1Angle, snap(gene.child1Angle, steps, 0.0f, (2.0 * PI).toFloat())) &&
                exactly(gene.child2Angle, snap(gene.child2Angle, steps, 0.0f, (2.0 * PI).toFloat())) &&
                gene.color.all { exactly(it, snap(it, steps, 0.0f, 1.0f)) } &&
                exactly(
                    gene.linkStrength,
                    snap(gene.linkStrength, steps, 0.0f, LinkConstants.MAX_STIFFNESS)
                ) &&
                exactly(
                    gene.splitMass,
                    snapSplitMass(gene.splitMass, steps)
                ) &&
                priorityIsSnapped(gene, steps) &&
                exactly(
                    gene.splitRatio,
                    snap(
                        gene.splitRatio,
                        steps,
                        CellDataConstants.MIN_RATIO,
                        1.0f - CellDataConstants.MIN_RATIO
                    )
                ) &&
                GeneFloatType.entries.all { type ->
                    val range = GenomeContract.floatRange(type)
                    exactly(
                        gene.floatValues[type],
                        snap(gene.floatValues[type], steps, range.min, range.max)
                    )
                } &&
                gene.parametricValues.all { value ->
                    value.signalSubstanceChannelIndex.toInt() != -1 ||
                        exactly(value.variableB, snap(value.variableB, steps, -1.0f, 1.0f))
                }
        }

    fun exponentialScale(minimum: Double, maximum: Double, position: Double): Double =
        (maximum / minimum).pow(position) * minimum

    fun inverseExponentialScale(minimum: Double, maximum: Double, value: Double): Double =
        ln(value / minimum) / ln(maximum / minimum)

    private fun snapSplitMass(value: Float, steps: Int): Float =
        if (value.isFinite() && value >= CellDataConstants.MAX_MASS.toFloat()) {
            CellDataConstants.MAX_SPLIT_MASS
        } else {
            snap(
                value,
                steps,
                CellDataConstants.MIN_SPLIT_MASS,
                CellDataConstants.MAX_SPLITTABLE_MASS
            )
        }

    fun stepToVariable(step: Int, oneSide: Boolean, steps: Int): Float {
        require(steps >= 2) { "Variable scale requires at least two steps" }
        if (!oneSide) {
            if (step == steps / 2) return 0.0f
            val distance = abs(step - steps / 2.0f)
            return (GenomeEditorConfig.VARIABLE_MIN *
                (CellDataConstants.VALUE_VARIABLE_MAX / GenomeEditorConfig.VARIABLE_MIN).toDouble()
                    .pow(((distance - 1.0f) / (steps / 2.0f - 1.0f)).toDouble()) *
                sign(step - steps.toFloat() / 2.0f)).toFloat()
        }
        if (step == 0) return 0.0f
        return (GenomeEditorConfig.VARIABLE_MIN *
            (CellDataConstants.VALUE_VARIABLE_MAX / GenomeEditorConfig.VARIABLE_MIN).toDouble()
                .pow(((step - 1.0f) / (steps - 1.0f)).toDouble())).toFloat()
    }

    fun variableToStep(value: Float, oneSide: Boolean, steps: Int): Int {
        require(steps >= 2) { "Variable scale requires at least two steps" }
        if (!oneSide) {
            if (value == 0.0f) return steps / 2
            return (steps / 2.0f +
                (ln((abs(value) / GenomeEditorConfig.VARIABLE_MIN).toDouble()) /
                    ln(
                        (CellDataConstants.VALUE_VARIABLE_MAX / GenomeEditorConfig.VARIABLE_MIN)
                            .toDouble()
                    ) *
                    (steps / 2.0f - 1.0f) + 1.0f) * sign(value)).roundToInt()
        }
        if (value == 0.0f) return 0
        return (ln(
            ((value + CellDataConstants.VALUE_VARIABLE_MAX) * 0.5f /
                GenomeEditorConfig.VARIABLE_MIN).toDouble()
        ) / ln(
            (CellDataConstants.VALUE_VARIABLE_MAX / GenomeEditorConfig.VARIABLE_MIN).toDouble()
        ) * (steps - 1.0f) + 1.0f).roundToInt()
    }

    private fun priorityIsSnapped(gene: GeneDraft, steps: Int): Boolean {
        if (!gene.massFlowPriority.isFinite() || gene.massFlowPriority <= 0.0f) return false
        return exactly(
            gene.massFlowPriority,
            exp(
                snap(
                    ln(gene.massFlowPriority.toDouble()).toFloat(),
                    steps,
                    ln(CellDataConstants.MIN_MASS_FLOW_PRIORITY.toDouble()).toFloat(),
                    ln(CellDataConstants.MAX_MASS_FLOW_PRIORITY.toDouble()).toFloat()
                )
            )
        )
    }

    private fun exactly(first: Float, second: Float): Boolean = first.toDouble() == second.toDouble()
}
