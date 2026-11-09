package com.cytarch.cellularity.platform.files

import com.cytarch.cellularity.NativeCellularityControllerBridge
import com.cytarch.cellularity.shared.files.genome.EncodedGenomePreview
import com.cytarch.cellularity.shared.files.genome.PREVIEW_CODEC_NONE
import com.cytarch.cellularity.shared.files.genome.ReleaseGenomeCodecBridge

/** Android's JNI route to the shared native release-genome codec. */
internal object NativeReleaseGenomeCodecBridge : ReleaseGenomeCodecBridge {
    override fun encode(
        genomeDraftWire: ByteArray,
        preview: EncodedGenomePreview?
    ): ByteArray = NativeCellularityControllerBridge.encodeReleaseGenome(
        genomeDraftWire,
        preview?.bytes,
        preview?.codec ?: PREVIEW_CODEC_NONE,
        preview?.pixelWidth ?: 0,
        preview?.pixelHeight ?: 0
    )

    override fun decode(releaseGenome: ByteArray): ByteArray =
        NativeCellularityControllerBridge.decodeReleaseGenome(releaseGenome)
}
