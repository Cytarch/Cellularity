package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.GeneFloatType
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class GenomeDraftValidatorTest {
    @Test
    fun canonicalGenomeIsValid() {
        assertTrue(GenomeDraftValidator.validate(testGenome()).isValid)
    }

    @Test
    fun reportsFiniteRangeIndexEnumAndInitialFailuresByTypedTarget() {
        val source = testGenome()
        val invalidGene = source.genes[0].copy(
            color = listOf(Float.NaN, 0.0f, 2.0f),
            splitRatio = 0.0f,
            child1GeneIndex = -1,
            parametricValues = source.genes[0].parametricValues.mapIndexed { index, value ->
                if (index == 0) {
                    value.copy(variableA = Float.POSITIVE_INFINITY, formulaType = 9)
                } else {
                    value
                }
            },
            integerValues = source.genes[0].integerValues.mapIndexed { index, value ->
                if (index == 0) GenomeContract.integerUpperBounds[index] else value
            },
            floatValues = source.genes[0].floatValues.withValue(GeneFloatType.CYTOSKELETON, 3.0f),
            initial = false
        )
        val validation = GenomeDraftValidator.validate(source.withGene(0, invalidGene))

        assertFalse(validation.isValid)
        assertTrue(validation.issues.any {
            it.target == GenomeValidationTarget.Color(GeneIndex(0), 0) &&
                it.reason == GenomeValidationReason.NON_FINITE
        })
        assertTrue(validation.issues.any {
            it.target == GenomeValidationTarget.Scalar(GeneIndex(0), GenomeScalarField.SPLIT_RATIO) &&
                it.reason == GenomeValidationReason.OUT_OF_RANGE
        })
        assertTrue(validation.issues.any {
            it.target == GenomeValidationTarget.ChildGene(GeneIndex(0), GenomeChild.FIRST) &&
                it.reason == GenomeValidationReason.INVALID_INDEX
        })
        assertTrue(validation.issues.any {
            it.target == GenomeValidationTarget.ParametricFormula(GeneIndex(0), ParametricValueId(0)) &&
                it.reason == GenomeValidationReason.INVALID_ENUM
        })
        assertTrue(validation.issues.any {
            it.target == GenomeValidationTarget.IntegerValue(GeneIndex(0), GeneIntegerId(0)) &&
                it.reason == GenomeValidationReason.INVALID_INDEX
        })
        assertTrue(validation.issues.any {
            it.target == GenomeValidationTarget.FloatValue(GeneIndex(0), GeneFloatType.CYTOSKELETON)
        })
        assertEquals(1, validation.issues.count {
            it.target == GenomeValidationTarget.InitialGeneCount
        })
    }

    @Test
    fun validatorReportsEveryConstrainedFloatFamily() {
        val source = testGenome()
        var genome = source
        GeneFloatType.entries.forEach { type ->
            val range = GenomeContract.floatRange(type)
            genome = genome.withGene(
                0,
                genome.genes[0].withFloatValue(type, range.max + 1.0f)
            )
        }

        val issues = GenomeDraftValidator.validate(genome).issues

        GeneFloatType.entries.forEach { type ->
            assertTrue(issues.any {
                it.target == GenomeValidationTarget.FloatValue(GeneIndex(0), type) &&
                    it.reason == GenomeValidationReason.OUT_OF_RANGE
            })
        }
    }

    private fun testGenome(): GenomeDraft = GenomeDraft.editorDefaults {
        listOf(1.0f, 0.0f, 0.0f)
    }
}
