package com.cytarch.cellularity.platform.files

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class GenomePreviewEncoderTest {
    @Test
    fun clearsHiddenColorsOnlyForFullyTransparentPixels() {
        val rgba = byteArrayOf(
            0x7f, 0x55, 0x33, 0,
            0x44, 0x66, 0x22, 0x7f
        )
        StraightRgbaBitmapFactory.clearFullyTransparentRgb(rgba)
        assertArrayEquals(
            byteArrayOf(
                0, 0, 0, 0,
                0x44, 0x66, 0x22, 0x7f
            ),
            rgba
        )
    }

    @Test
    fun rejectsAnythingExceptOneCompleteRgbaFrame() {
        assertThrows(IllegalArgumentException::class.java) {
            GenomePreviewEncoder.encodePng(ByteArray(15), 2, 2, 8)
        }
        assertThrows(IllegalArgumentException::class.java) {
            GenomePreviewEncoder.encodePng(ByteArray(16), 2, 2, 12)
        }
    }
}
