package com.cytarch.cellularity.platform.files

import android.graphics.Bitmap
import android.os.Build
import java.io.ByteArrayOutputStream
import java.io.IOException

internal object SubstratePreviewEncoder {
    private const val WEBP_QUALITY = 85
    private const val MAXIMUM_ENCODED_BYTES = 8 * 1024 * 1024

    fun encodeWebpLossy(
        rgbaBytes: ByteArray,
        pixelWidth: Int,
        pixelHeight: Int,
        rowStrideBytes: Int
    ): ByteArray {
        val bitmap = StraightRgbaBitmapFactory.createConsuming(
            rgbaBytes = rgbaBytes,
            pixelWidth = pixelWidth,
            pixelHeight = pixelHeight,
            rowStrideBytes = rowStrideBytes,
            canonicalizeTransparentRgb = false
        )
        return try {
            val output = ByteArrayOutputStream()
            if (!bitmap.compress(webpLossyFormat(), WEBP_QUALITY, output)) {
                throw IOException("Android could not encode the substrate preview")
            }
            output.toByteArray().also { encoded ->
                if (encoded.isEmpty() || encoded.size > MAXIMUM_ENCODED_BYTES) {
                    throw IOException("Encoded substrate preview exceeds its size limit")
                }
            }
        } finally {
            bitmap.recycle()
        }
    }

    @Suppress("DEPRECATION")
    private fun webpLossyFormat(): Bitmap.CompressFormat =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Bitmap.CompressFormat.WEBP_LOSSY
        } else {
            Bitmap.CompressFormat.WEBP
        }
}
