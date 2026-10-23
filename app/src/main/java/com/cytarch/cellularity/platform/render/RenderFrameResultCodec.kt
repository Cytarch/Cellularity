package com.cytarch.cellularity.platform.render

/** Allocation-free V1 codec for one native onscreen render transaction. */
object RenderFrameResultCodec {
    const val OUTCOME_PRESENTED = 0
    const val OUTCOME_DEFERRED = 1
    const val OUTCOME_SWAPCHAIN_RECREATED = 2
    const val OUTCOME_SURFACE_LOST = 3
    const val OUTCOME_SKIPPED_NOT_READY = 4
    const val OUTCOME_FAILED = 5

    const val FLAG_PRIMARY_CONTENT_PRESENTED = 1
    const val FLAG_FALLBACK_CLEAR_PRESENTED = 2

    const val INVALID_WIRE = 0xFFL
    const val SKIPPED_NOT_READY_WIRE = 4L
    const val FAILED_WIRE = 5L

    private const val BYTE_MASK = 0xFFL
    private const val RESERVED_MASK = 0xFFFFL
    private const val FLAGS_SHIFT = 8
    private const val RESERVED_SHIFT = 16
    private const val SEMANTIC_VALUE_SHIFT = 32

    @JvmStatic
    fun isValid(bits: Long): Boolean {
        if (((bits ushr RESERVED_SHIFT) and RESERVED_MASK) != 0L) return false
        val outcome = outcomeCode(bits)
        if (outcome !in OUTCOME_PRESENTED..OUTCOME_FAILED) return false
        return if (outcome == OUTCOME_PRESENTED) {
            val flags = flags(bits)
            flags == FLAG_PRIMARY_CONTENT_PRESENTED ||
                flags == FLAG_FALLBACK_CLEAR_PRESENTED
        } else {
            flags(bits) == 0
        }
    }

    @JvmStatic
    fun outcomeCode(bits: Long): Int = (bits and BYTE_MASK).toInt()

    @JvmStatic
    fun flags(bits: Long): Int = ((bits ushr FLAGS_SHIFT) and BYTE_MASK).toInt()

    @JvmStatic
    fun semanticValue(bits: Long): Int = (bits shr SEMANTIC_VALUE_SHIFT).toInt()

    @JvmStatic
    fun pack(outcome: Int, flags: Int, semanticValue: Int = 0): Long {
        if (outcome !in 0..BYTE_MASK.toInt() || flags !in 0..BYTE_MASK.toInt()) {
            return INVALID_WIRE
        }
        val bits = (outcome.toLong() and BYTE_MASK) or
            ((flags.toLong() and BYTE_MASK) shl FLAGS_SHIFT) or
            (semanticValue.toLong() shl SEMANTIC_VALUE_SHIFT)
        return if (isValid(bits)) bits else INVALID_WIRE
    }
}
