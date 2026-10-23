package com.cytarch.cellularity.shared.model

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class CanonicalGeneDefaultsTest {
    @Test
    fun canonicalDefaultSpecifiesEveryFieldWithoutGenomeOwnedState() {
        val owner = 7
        val gene = CanonicalGeneDefaults.materialize(owner)

        assertEquals(listOf(0.0f, 0.0f, 0.0f), gene.color)
        assertEquals(CellDataConstants.MIN_MASS.toFloat() * 3.5f, gene.splitMass)
        assertEquals(0.5f, gene.splitRatio)
        assertEquals(0.0f, gene.splitAngle)
        assertEquals(0.0f, gene.child1Angle)
        assertEquals(0.0f, gene.child2Angle)
        assertEquals(1.0f, gene.massFlowPriority)
        assertEquals(0.0f, gene.linkStrength)
        assertEquals(owner, gene.child1GeneIndex)
        assertEquals(owner, gene.child2GeneIndex)
        assertFalse(gene.makeLink)
        assertTrue(gene.child1KeepLinks)
        assertTrue(gene.child2KeepLinks)
        assertTrue(gene.stayAlive)
        assertFalse(gene.initial)
        assertFalse(gene.child1Mirror)
        assertFalse(gene.child2Mirror)
        assertEquals(CellType.PHAGOCYTE, gene.cellType)
        gene.parametricValues.forEach { value ->
            assertEquals(0.0f, value.variableA)
            assertEquals(0.0f, value.variableB)
            assertEquals(0.0f, value.variableC)
            assertEquals(0, value.signalSubstanceChannelIndex.toInt())
            assertEquals(0, value.formulaType.toInt())
        }
        gene.integerValues.forEachIndexed { index, value ->
            val expected = when (index) {
                GenomeContract.VIRUS_FROM_INTEGER_INDEX,
                GenomeContract.VIRUS_TO_INTEGER_INDEX -> owner
                GenomeContract.MAX_CONNECTION_COUNT_INDEX ->
                    GenomeContract.integerUpperBounds[index] - 1
                else -> 0
            }
            assertEquals(expected, value, "integer[$index]")
        }
        assertEquals(0.0f, gene.floatValues.smellAmplitude)
        assertEquals(0.0f, gene.floatValues.smellRed)
        assertEquals(0.0f, gene.floatValues.smellGreen)
        assertEquals(0.0f, gene.floatValues.smellBlue)
        assertEquals(0.0f, gene.floatValues.smellThreshold)
        assertEquals(0.0f, gene.floatValues.adhesinLength)
        assertEquals(1.0f, gene.floatValues.cytoskeleton)
    }

    @Test
    fun editorTemplateAddsOnlyIntentionalCreationOverrides() {
        val color = listOf(0.2f, 0.4f, 0.8f)
        val template = EditorGeneTemplate.create(
            geneIndex = 7,
            color = color,
            metadata = GenomeMetadata(initialGeneIndex = 7)
        )

        assertEquals(color, template.color)
        assertEquals(7, template.child1GeneIndex)
        assertEquals(7, template.child2GeneIndex)
        assertTrue(template.initial)
        assertEquals(
            listOf(0.0f, 0.0f, 0.0f),
            CanonicalGeneDefaults.materialize(7).color
        )
    }

    @Test
    fun initialGeneCompatibilityAdapterOwnsFlagMaterialization() {
        val source = GenomeDraft(
            List(20) { index ->
                CanonicalGeneDefaults.materialize(index).copy(initial = index == 0)
            }
        )
        assertEquals(GenomeMetadata(0), GenomeInitialGeneCompatibility.extract(source))

        val moved = GenomeInitialGeneCompatibility.apply(source, GenomeMetadata(17))
        assertEquals(GenomeMetadata(17), GenomeInitialGeneCompatibility.extract(moved))
        assertEquals(1, moved.genes.count { it.initial })

        val missing = GenomeDraft(source.genes.map { it.copy(initial = false) })
        assertNull(GenomeInitialGeneCompatibility.extract(missing))
        val duplicate = GenomeDraft(source.genes.mapIndexed { index, gene ->
            gene.copy(initial = index == 0 || index == 1)
        })
        assertNull(GenomeInitialGeneCompatibility.extract(duplicate))
        assertFailsWith<IllegalArgumentException> {
            GenomeMetadata(GenomeContract.MAX_GENE_COUNT)
        }
    }
}
