package com.cytarch.cellularity.platform.files

import org.junit.Assert.assertThrows
import org.junit.Test

class SubstratePreviewEncoderTest {
    @Test
    fun rejectsAnythingExceptOneCompleteRgbaFrame() {
        assertThrows(IllegalArgumentException::class.java) {
            SubstratePreviewEncoder.encodeWebpLossy(ByteArray(15), 2, 2, 8)
        }
        assertThrows(IllegalArgumentException::class.java) {
            SubstratePreviewEncoder.encodeWebpLossy(ByteArray(16), 2, 2, 12)
        }
    }
}
