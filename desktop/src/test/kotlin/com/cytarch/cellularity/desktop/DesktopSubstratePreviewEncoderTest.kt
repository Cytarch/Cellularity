package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopSubstratePreviewEncoder
import org.jetbrains.skia.Image
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * The desktop can make the picture a plate's file is allowed to carry.
 *
 * The container's preview attachment takes only a WebP payload -- that is the format's policy for a
 * substrate, and Android produces it with the platform's own bitmap writer, which a desktop does not
 * have. The desktop uses the Skia the interface is drawn with, and whether that Skia *has* a WebP
 * encoder is a question about the binary rather than about its API: the format is offered either way
 * and answered with nothing when the build cannot do it. A save would then quietly write plates with
 * no pictures, which is legal and looks like a feature that does not work.
 *
 * So the capability is measured rather than assumed, in the same terms the container checks: the
 * bytes must decode, and they must begin with the signature the attachment requires.
 */
class DesktopSubstratePreviewEncoderTest {
    @Test
    fun theDesktopEncodesTheWebpAPlatePrefers() {
        val side = 512
        val rgba = ByteArray(side * side * 4)
        for (pixel in 0 until side * side) {
            val offset = pixel * 4
            rgba[offset] = (pixel % 256).toByte()
            rgba[offset + 1] = ((pixel / 256) % 256).toByte()
            rgba[offset + 2] = 0x40
            rgba[offset + 3] = 0xff.toByte()
        }

        val encoded = DesktopSubstratePreviewEncoder.encodeWebpLossy(rgba, side, side, side * 4)

        assertTrue(encoded != null && encoded.isNotEmpty(), "the desktop could not encode a preview")
        assertEquals("RIFF", String(encoded, 0, 4, Charsets.US_ASCII))
        assertEquals("WEBP", String(encoded, 8, 4, Charsets.US_ASCII))
        // And it is a picture, not merely a header: a reader has to be able to open it.
        val decoded = Image.makeFromEncoded(encoded)
        assertEquals(side, decoded.width)
        assertEquals(side, decoded.height)
    }

    @Test
    fun theEncoderRefusesWhatIsNotAFrame() {
        // A caller that hands over a frame of the wrong shape gets nothing rather than a picture of
        // somebody else's memory: the row stride and the byte count are both checked.
        assertTrue(DesktopSubstratePreviewEncoder.encodeWebpLossy(ByteArray(16), 4, 4, 8) == null)
        assertTrue(DesktopSubstratePreviewEncoder.encodeWebpLossy(ByteArray(0), 0, 0, 0) == null)
    }
}
