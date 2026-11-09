/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import com.cytarch.cellularity.shared.files.genome.EncodedGenomePreview
import com.cytarch.cellularity.shared.files.genome.PREVIEW_CODEC_NONE
import com.cytarch.cellularity.shared.files.genome.ReleaseGenomeCodecBridge

/** The desktop's JNI route to the shared native release-genome codec; Android has the twin. */
object DesktopReleaseGenomeCodecBridge : ReleaseGenomeCodecBridge {
    override fun encode(
        genomeDraftWire: ByteArray,
        preview: EncodedGenomePreview?
    ): ByteArray {
        DesktopNativeRenderer.requireLoaded()
        return DesktopNativeRenderer.nativeEncodeReleaseGenome(
            genomeDraftWire,
            preview?.bytes,
            preview?.codec ?: PREVIEW_CODEC_NONE,
            preview?.pixelWidth ?: 0,
            preview?.pixelHeight ?: 0
        )
    }

    override fun decode(releaseGenome: ByteArray): ByteArray {
        DesktopNativeRenderer.requireLoaded()
        return DesktopNativeRenderer.nativeDecodeReleaseGenome(releaseGenome)
    }
}
