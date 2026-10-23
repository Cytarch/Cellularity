package com.cytarch.cellularity.platform.files

/** Frozen contract for the original Java object-stream `.genome` format. */
internal object LegacyGenomeFormat {
    const val MIN_PRODUCT_VERSION = 1
    const val MAX_PRODUCT_VERSION = 103
    const val OBSOLETE_EXTENSION_PRODUCT_VERSION = 103
    const val OBSOLETE_GENE_EXTENSION_FIELD_COUNT = 8

    const val GENOME_15_LAST_PRODUCT_VERSION = 3
    const val GENOME_20_LAST_PRODUCT_VERSION = 17
    const val GENOME_15_GENE_COUNT = 15
    const val GENOME_20_GENE_COUNT = 20

    /** Effective in-memory result of every supported legacy import. */
    const val EFFECTIVE_IMPORTED_GENE_COUNT = 40

    fun serializedGeneCount(productVersion: Int): Int = when {
        productVersion <= GENOME_15_LAST_PRODUCT_VERSION -> GENOME_15_GENE_COUNT
        productVersion <= GENOME_20_LAST_PRODUCT_VERSION -> GENOME_20_GENE_COUNT
        else -> EFFECTIVE_IMPORTED_GENE_COUNT
    }
}
