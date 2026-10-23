package com.cytarch.cellularity.shared.model

/** Genome-owned state temporarily mirrored by [GeneDraft.initial]. */
data class GenomeMetadata(val initialGeneIndex: Int) {
    init {
        require(initialGeneIndex in 0 until GenomeContract.MAX_GENE_COUNT) {
            "Unknown initial gene index: $initialGeneIndex"
        }
    }
}

/** Adapter for the fixed draft's per-gene initial flags. */
object GenomeInitialGeneCompatibility {
    fun extract(genome: GenomeDraft): GenomeMetadata? {
        var selected = -1
        for (index in genome.genes.indices) {
            if (!genome.genes[index].initial) continue
            if (selected >= 0) return null
            selected = index
        }
        return if (selected >= 0) GenomeMetadata(selected) else null
    }

    fun apply(genome: GenomeDraft, metadata: GenomeMetadata): GenomeDraft {
        require(metadata.initialGeneIndex in genome.genes.indices) {
            "Initial gene index is outside the genome"
        }
        return GenomeDraft(genome.genes.mapIndexed { index, gene ->
            gene.copy(initial = index == metadata.initialGeneIndex)
        })
    }
}
