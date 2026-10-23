package com.cytarch.cellularity.platform.files

import android.graphics.Bitmap
import java.io.ByteArrayOutputStream
import java.io.IOException

internal object GenomePreviewEncoder {
    private const val MAXIMUM_ENCODED_BYTES = 8 * 1024 * 1024

    fun encodePng(
        rgbaBytes: ByteArray,
        pixelWidth: Int,
        pixelHeight: Int,
        rowStrideBytes: Int
    ): EncodedGenomePreview {
        val bitmap = StraightRgbaBitmapFactory.createConsuming(
            rgbaBytes = rgbaBytes,
            pixelWidth = pixelWidth,
            pixelHeight = pixelHeight,
            rowStrideBytes = rowStrideBytes,
            canonicalizeTransparentRgb = true
        )
        return try {
            val output = ByteArrayOutputStream()
            if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)) {
                throw IOException("Android could not encode the genome preview")
            }
            val encoded = output.toByteArray()
            if (encoded.isEmpty() || encoded.size > MAXIMUM_ENCODED_BYTES) {
                throw IOException("Encoded genome preview exceeds its size limit")
            }
            EncodedGenomePreview(
                codec = PREVIEW_CODEC_PNG,
                pixelWidth = pixelWidth,
                pixelHeight = pixelHeight,
                bytes = encoded
            )
        } finally {
            bitmap.recycle()
        }
    }
}
