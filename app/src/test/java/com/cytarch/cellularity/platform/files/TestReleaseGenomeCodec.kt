package com.cytarch.cellularity.platform.files

/** JVM-test stand-in for the JNI-owned release body codec. */
internal class TestReleaseGenomeCodec : ReleaseGenomeCodecBridge {
    var lastEncodedWire: ByteArray? = null
        private set
    var lastEncodedPreview: EncodedGenomePreview? = null
        private set

    override fun encode(
        genomeDraftWire: ByteArray,
        preview: EncodedGenomePreview?
    ): ByteArray {
        lastEncodedWire = genomeDraftWire.copyOf()
        lastEncodedPreview = preview
        return wrap(genomeDraftWire)
    }

    override fun decode(releaseGenome: ByteArray): ByteArray {
        require(releaseGenome.size >= MAGIC.size && releaseGenome.copyOfRange(0, MAGIC.size)
            .contentEquals(MAGIC)) {
            "Test release genome does not start with CLTY"
        }
        return releaseGenome.copyOfRange(MAGIC.size, releaseGenome.size)
    }

    companion object {
        private val MAGIC = byteArrayOf('C'.code.toByte(), 'L'.code.toByte(), 'T'.code.toByte(), 'Y'.code.toByte())

        fun wrap(genomeDraftWire: ByteArray): ByteArray = MAGIC + genomeDraftWire
    }
}
