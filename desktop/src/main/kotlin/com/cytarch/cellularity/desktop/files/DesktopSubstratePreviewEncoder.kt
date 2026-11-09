package com.cytarch.cellularity.desktop.files

import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.EncodedImageFormat
import org.jetbrains.skia.Image
import org.jetbrains.skia.ImageInfo

/**
 * Encodes a plate's preview picture the way a plate's preview is stored.
 *
 * The container's policy for a substrate preview is WebP lossy at the format's own square size and
 * quality, and the attachment point accepts nothing else -- so the desktop has to produce exactly
 * what the phone produces, or a plate saved here would carry a picture Android would refuse to read.
 * Android encodes with the platform's own bitmap writer; the desktop has no such writer, and uses
 * the Skia that is already in the package because the interface is drawn with it.
 *
 * It is here rather than in the save so that the pixels and the file format stay apart: the capture
 * knows what the renderer drew, this knows what the container stores, and neither has to know how a
 * save is arranged.
 */
internal object DesktopSubstratePreviewEncoder {
    /**
     * The encoded picture, or null when the pixels cannot be encoded.
     *
     * Null is an answer and not an error: a plate without a picture is a complete plate, and a save
     * writes one rather than failing because an ornament could not be made. The one thing that must
     * not happen is attaching bytes the format would refuse, which is why the result is checked for
     * the WebP signature before it is handed back.
     */
    fun encodeWebpLossy(
        rgbaBytes: ByteArray,
        pixelWidth: Int,
        pixelHeight: Int,
        rowStrideBytes: Int
    ): ByteArray? {
        if (pixelWidth <= 0 || pixelHeight <= 0) return null
        val packedRowBytes = pixelWidth * BYTES_PER_PIXEL
        if (rowStrideBytes < packedRowBytes || rgbaBytes.size < rowStrideBytes * pixelHeight) {
            return null
        }
        // Repacked when the renderer's rows are padded: the encoder reads tightly packed rows, and
        // the padding is the renderer's business rather than the format's. With the stride the
        // capture produces there is nothing to do, which is the ordinary case.
        val pixels = if (rowStrideBytes == packedRowBytes) {
            rgbaBytes
        } else {
            ByteArray(packedRowBytes * pixelHeight).also { packed ->
                for (row in 0 until pixelHeight) {
                    rgbaBytes.copyInto(
                        destination = packed,
                        destinationOffset = row * packedRowBytes,
                        startIndex = row * rowStrideBytes,
                        endIndex = row * rowStrideBytes + packedRowBytes
                    )
                }
            }
        }
        val bitmap = Bitmap()
        return try {
            bitmap.allocPixels(
                ImageInfo(
                    pixelWidth,
                    pixelHeight,
                    ColorType.RGBA_8888,
                    ColorAlphaType.UNPREMUL
                )
            )
            bitmap.installPixels(pixels)
            val encoded = Image.makeFromBitmap(bitmap)
                .encodeToData(EncodedImageFormat.WEBP, QUALITY)
                ?.bytes
            encoded?.takeIf(::hasWebpSignature)
        } catch (failure: RuntimeException) {
            // Skia answers a format it cannot encode with nothing rather than with an exception, so
            // reaching here means something else went wrong with the pixels. Either way there is no
            // picture, and the save goes on without one.
            null
        } finally {
            bitmap.close()
        }
    }

    /**
     * Whether [bytes] is a WebP document at all.
     *
     * Checked here rather than left to the container: the attachment refuses a payload that is not
     * WebP, and a refusal halfway through attaching is a worse outcome than not offering the picture
     * in the first place -- it would leave a plate whose save failed for a reason that has nothing
     * to do with the plate. The signature is the format's own: `RIFF`, a size, then `WEBP`.
     */
    /**
     * A saved genome's picture as PNG, as Android's GenomePreviewEncoder makes it: straight alpha,
     * fully transparent pixels canonicalised to black, at most 8 MiB.
     */
    fun encodeGenomePng(
        rgbaBytes: ByteArray,
        pixelWidth: Int,
        pixelHeight: Int
    ): com.cytarch.cellularity.shared.files.genome.EncodedGenomePreview? {
        if (pixelWidth <= 0 || pixelHeight <= 0) return null
        if (rgbaBytes.size.toLong() != pixelWidth.toLong() * pixelHeight * BYTES_PER_PIXEL) return null
        val pixels = rgbaBytes.copyOf()
        var offset = 0
        while (offset < pixels.size) {
            if (pixels[offset + 3].toInt() == 0) {
                pixels[offset] = 0
                pixels[offset + 1] = 0
                pixels[offset + 2] = 0
            }
            offset += BYTES_PER_PIXEL
        }
        val bitmap = Bitmap()
        return try {
            bitmap.allocPixels(
                ImageInfo(pixelWidth, pixelHeight, ColorType.RGBA_8888, ColorAlphaType.UNPREMUL)
            )
            bitmap.installPixels(pixels)
            val encoded = Image.makeFromBitmap(bitmap)
                .encodeToData(EncodedImageFormat.PNG)
                ?.bytes
                ?.takeIf { it.isNotEmpty() && it.size <= MAXIMUM_GENOME_PREVIEW_BYTES }
                ?: return null
            com.cytarch.cellularity.shared.files.genome.EncodedGenomePreview(
                codec = com.cytarch.cellularity.shared.files.genome.PREVIEW_CODEC_PNG,
                pixelWidth = pixelWidth,
                pixelHeight = pixelHeight,
                bytes = encoded
            )
        } catch (failure: RuntimeException) {
            null
        } finally {
            bitmap.close()
        }
    }

    private const val MAXIMUM_GENOME_PREVIEW_BYTES = 8 * 1024 * 1024

    private fun hasWebpSignature(bytes: ByteArray): Boolean =
        bytes.size > WEBP_HEADER_BYTES &&
            bytes[0] == 'R'.code.toByte() &&
            bytes[1] == 'I'.code.toByte() &&
            bytes[2] == 'F'.code.toByte() &&
            bytes[3] == 'F'.code.toByte() &&
            bytes[8] == 'W'.code.toByte() &&
            bytes[9] == 'E'.code.toByte() &&
            bytes[10] == 'B'.code.toByte() &&
            bytes[11] == 'P'.code.toByte()

    private const val BYTES_PER_PIXEL = 4
    private const val WEBP_HEADER_BYTES = 12

    /** The quality the phone writes a plate's picture at, and therefore the one written here. */
    private const val QUALITY = 85
}
