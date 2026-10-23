package com.cytarch.cellularity.shared.model

/** Stable application representation of the native cell type contract. */
enum class CellType(val nativeValue: Int) {
    PHAGOCYTE(0),
    FLAGELLOCYTE(1),
    PHOTOCYTE(2),
    DEVOROCYTE(3),
    LIPOCYTE(4),
    KERATINOCYTE(5),
    BUOYOCYTE(6),
    GLUEOCYTE(7),
    VIROCYTE(8),
    NITROCYTE(9),
    STEREOCYTE(10),
    SENSEOCYTE(11),
    MYOCYTE(12),
    NEUROCYTE(13),
    SECROCYTE(14),
    STEMOCYTE(15),
    GAMETE(16),
    CILIOCYTE(17);

    companion object {
        fun fromNativeValue(value: Int): CellType =
            entries.firstOrNull { it.nativeValue == value }
                ?: throw IllegalArgumentException("Unknown cell type: $value")
    }
}
