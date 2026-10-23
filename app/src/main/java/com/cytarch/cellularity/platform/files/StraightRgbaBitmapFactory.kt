package com.cytarch.cellularity.platform.files

import android.graphics.Bitmap
import androidx.core.graphics.createBitmap
import java.nio.ByteBuffer

/** Builds an Android bitmap from the tightly packed straight-alpha RGBA capture payload. */
internal object StraightRgbaBitmapFactory {
    private const val RGBA_CHANNELS = 4

    /**
     * The payload is consumed by this call and may be normalized in place. Callers must not
     * reuse it afterwards.
     */
    fun createConsuming(
        rgbaBytes: ByteArray,
        pixelWidth: Int,
        pixelHeight: Int,
        rowStrideBytes: Int,
        canonicalizeTransparentRgb: Boolean
    ): Bitmap {
        requireValidFrame(rgbaBytes, pixelWidth, pixelHeight, rowStrideBytes)
        if (canonicalizeTransparentRgb) {
            clearFullyTransparentRgb(rgbaBytes)
        }

        val bitmap = createBitmap(pixelWidth, pixelHeight, Bitmap.Config.ARGB_8888)
        return try {
            // copyPixelsFromBuffer copies RGBA bytes directly into Android's RGBA_8888 storage.
            // Keeping the bitmap straight-alpha prevents Android from treating those bytes as
            // premultiplied colors and unpremultiplying them again during encoding.
            bitmap.setPremultiplied(false)
            bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(rgbaBytes))
            bitmap
        } catch (failure: Throwable) {
            bitmap.recycle()
            throw failure
        }
    }

    internal fun requireValidFrame(
        rgbaBytes: ByteArray,
        pixelWidth: Int,
        pixelHeight: Int,
        rowStrideBytes: Int
    ) {
        require(pixelWidth > 0 && pixelHeight > 0 && pixelWidth <= Int.MAX_VALUE / RGBA_CHANNELS) {
            "Rendered preview has invalid dimensions"
        }
        require(rowStrideBytes == pixelWidth * RGBA_CHANNELS) {
            "Rendered preview has an invalid row stride"
        }
        require(rgbaBytes.size.toLong() == rowStrideBytes.toLong() * pixelHeight.toLong()) {
            "Rendered preview has an invalid RGBA payload length"
        }
    }

    internal fun clearFullyTransparentRgb(rgbaBytes: ByteArray) {
        var offset = 0
        while (offset < rgbaBytes.size) {
            if (rgbaBytes[offset + 3].toInt() == 0) {
                rgbaBytes[offset] = 0
                rgbaBytes[offset + 1] = 0
                rgbaBytes[offset + 2] = 0
            }
            offset += RGBA_CHANNELS
        }
    }
}
