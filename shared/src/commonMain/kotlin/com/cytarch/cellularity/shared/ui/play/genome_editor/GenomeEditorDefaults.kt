package com.cytarch.cellularity.shared.ui.play.genome_editor

import com.cytarch.cellularity.shared.model.EditorGeneTemplate
import com.cytarch.cellularity.shared.model.GeneDraft
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeMetadata
import kotlin.math.sqrt
import kotlin.random.Random

/** The editor's new genes and new genomes: random colour, editor template. One for every app. */
object GenomeEditorDefaults {
    fun createRandomizedEditorGene(geneIndex: Int): GeneDraft = EditorGeneTemplate.create(
        geneIndex = geneIndex,
        color = randomNormalizedColor(),
        metadata = GenomeMetadata(initialGeneIndex = 0)
    )

    fun createRandomizedEditorGenome(): GenomeDraft = GenomeDraft(
        List(GenomeContract.MIN_GENE_COUNT, ::createRandomizedEditorGene)
    )

    private fun randomNormalizedColor(): List<Float> {
        val color = FloatArray(GenomeContract.COLOR_COUNT) {
            Random.Default.nextFloat()
        }
        val magnitude = sqrt(color.sumOf { (it * it).toDouble() }).toFloat()
        return if (magnitude > 0.0f && magnitude.isFinite()) {
            color.map { it / magnitude }
        } else {
            listOf(1.0f, 0.0f, 0.0f)
        }
    }
}
