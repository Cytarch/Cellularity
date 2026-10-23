package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.CellularityDataException
import com.cytarch.cellularity.shared.files.release.EmbeddedPreviewFormat
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.CellularityErrorCode
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeWireCodecTest {
    @Test
    fun cellSnapshotsDecodeAcrossSupportedWireVersions() {
        listOf(1, 4, 5, 6, 7, 8).forEach { version ->
            val cell = checkNotNull(
                NativeWireCodec.decodeCellSnapshot(cellSnapshotBytes(version))
            )

            assertEquals(0, cell.id)
            assertEquals(0.1, cell.positionX, 0.0)
            assertEquals(-0.1, cell.positionY, 0.0)
            assertEquals(3, cell.mutationCount)
            assertEquals(17, cell.telomeresCount)
            assertTrue(cell.mirrored)
            assertEquals(CellType.PHAGOCYTE, cell.cellType)
            assertEquals(0.01f, cell.signalSubstance[0], 1e-6f)
            assertFalse(cell.color is MutableList<*>)
            assertEquals(if (version >= 7) 41L else 0L, cell.organismId)
            assertEquals(if (version >= 7) 17L else 0L, cell.parentOrganismId)
        }
        assertEquals(null, NativeWireCodec.decodeCellSnapshot(null))
    }

    @Test
    fun cellWireRejectsMalformedHeadersAndPayloads() {
        assertFailure(
            cellHeader(0),
            CellularityErrorCode.INVALID_VERSION,
            NativeWireCodec::decodeCellSnapshot
        )
        assertFailure(
            cellHeader(9),
            CellularityErrorCode.NEWER_SNAPSHOT_VERSION,
            NativeWireCodec::decodeCellSnapshot
        )
        val current = cellSnapshotBytes(8)
        assertFailure(
            current.copyOf(current.size - 1),
            CellularityErrorCode.TRUNCATED_INPUT,
            NativeWireCodec::decodeCellSnapshot
        )
        assertFailure(
            current + 0,
            CellularityErrorCode.TRAILING_DATA,
            NativeWireCodec::decodeCellSnapshot
        )
        assertFailure(
            cellSnapshotBytes(7, organismId = -1L),
            CellularityErrorCode.INVALID_VALUE,
            NativeWireCodec::decodeCellSnapshot
        )
        assertFailure(
            cellSnapshotBytes(7, organismId = 0L, parentOrganismId = 17L),
            CellularityErrorCode.INVALID_VALUE,
            NativeWireCodec::decodeCellSnapshot
        )
        assertFailure(
            cellSnapshotBytes(7, organismId = 17L, parentOrganismId = 17L),
            CellularityErrorCode.INVALID_VALUE,
            NativeWireCodec::decodeCellSnapshot
        )
        assertFailure(
            cellSnapshotBytes(7, organismId = 17L, parentOrganismId = 41L),
            CellularityErrorCode.INVALID_VALUE,
            NativeWireCodec::decodeCellSnapshot
        )
    }

    @Test
    fun inspectionDecodesConfigurationAndCounts() {
        val inspection = NativeWireCodec.decodeSubstrateInspection(inspectionBytes())

        assertEquals(1, inspection.familyLocalVersion)
        assertEquals(12.5, inspection.time, 0.0)
        assertEquals(3, inspection.cellCount)
        assertEquals(8, inspection.configuration.maxCellCount)
        assertEquals(16, inspection.configuration.maxFoodCount)
        assertEquals(2.0, inspection.configuration.radius, 0.0)
        val preview = checkNotNull(inspection.preview)
        assertEquals(EmbeddedPreviewFormat.WEBP_LOSSY, preview.format)
        assertEquals(512, preview.pixelWidth)
        assertEquals(512, preview.pixelHeight)
        assertEquals(7632L, preview.byteCount)
        assertEquals(0x89abcdefL, preview.encodedCrc32)
        assertTrue(preview.usable)
        assertEquals(NativeSubstrateFormatFamily.RELEASE_CLTY, inspection.formatFamily)
        assertEquals(1, inspection.contentSchemaMajor)
        assertEquals(0, inspection.contentSchemaMinor)
        assertTrue(inspection.loadSupported)
        assertEquals(7, inspection.foodCount)
        assertEquals(18.25, inspection.foodMass)
        assertEquals(1_700_000_000_000L, inspection.createdAtUnixMillis)
        assertEquals(2, inspection.knownOrganismCount)
        assertTrue(inspection.organismIdentityComplete)
        assertEquals(0.0, inspection.configuration.innerRadius, 0.0)
        val ring = NativeWireCodec.decodeSubstrateInspection(inspectionBytes(innerRadius = 0.5))
        assertEquals(0.5, ring.configuration.innerRadius, 0.0)

        val future = NativeWireCodec.decodeSubstrateInspection(
            inspectionBytes(
                productVersion = 2,
                contentSchemaMajor = 2,
                loadSupported = 0
            )
        )
        assertFalse(future.loadSupported)
    }

    @Test
    fun inspectionRejectsMalformedAndUnsafeConfigurations() {
        listOf(-1.0, 0.01, 1.99, Double.NaN).forEach { inner ->
            assertInspectionFailure(inspectionBytes(innerRadius = inner), CellularityErrorCode.INVALID_VALUE)
        }
        assertInspectionFailure(
            inspectionBytes(magic = 0),
            CellularityErrorCode.INVALID_HEADER
        )
        assertInspectionFailure(
            inspectionBytes(wireVersion = 0),
            CellularityErrorCode.INVALID_VERSION
        )
        assertInspectionFailure(
            inspectionBytes(wireVersion = 8),
            CellularityErrorCode.NEWER_SNAPSHOT_VERSION
        )
        assertInspectionFailure(
            inspectionBytes(cellCount = -1),
            CellularityErrorCode.INVALID_COUNT
        )
        assertInspectionFailure(
            inspectionBytes(cellCount = 9, maxCellCount = 8),
            CellularityErrorCode.LIMIT_EXCEEDED
        )
        assertInspectionFailure(
            inspectionBytes(radius = Double.NaN),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(previewUsable = 2),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(previewWidth = 5000),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(previewCodec = 0),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(foodCount = 17),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(foodMass = Double.NaN),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(createdAtUnixMillis = -1),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(formatFamily = 0),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(contentSchemaMajor = 2),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(knownOrganismCount = 4),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(organismIdentityComplete = 2),
            CellularityErrorCode.INVALID_VALUE
        )
        assertInspectionFailure(
            inspectionBytes(
                knownOrganismCount = -1,
                organismIdentityComplete = 1
            ),
            CellularityErrorCode.INVALID_VALUE
        )
    }

    @Test
    fun inspectionVersionTwoRemainsReadableWithoutPreviewDescriptor() {
        val inspection = NativeWireCodec.decodeSubstrateInspection(
            inspectionBytes(wireVersion = 2)
        )

        assertEquals(null, inspection.preview)
    }

    @Test
    fun inspectionVersionFiveRemainsReadableWithoutOrganismMetadata() {
        val inspection = NativeWireCodec.decodeSubstrateInspection(
            inspectionBytes(wireVersion = 5)
        )

        assertEquals(null, inspection.knownOrganismCount)
        assertFalse(inspection.organismIdentityComplete)
    }

    @Test
    fun inspectionRejectsTruncationTrailingDataAndOversizedWire() {
        val valid = inspectionBytes()
        assertInspectionFailure(
            valid.copyOf(valid.size - 1),
            CellularityErrorCode.TRUNCATED_INPUT
        )
        assertInspectionFailure(valid + 0, CellularityErrorCode.TRAILING_DATA)
        assertInspectionFailure(ByteArray(161), CellularityErrorCode.LIMIT_EXCEEDED)
    }

    private fun inspectionBytes(
        magic: Int = 0x434c494e,
        wireVersion: Int = 7,
        productVersion: Int = 1,
        time: Double = 12.5,
        cellCount: Int = 3,
        maxCellCount: Int = 8,
        maxFoodCount: Int = 16,
        radius: Double = 2.0,
        previewCodec: Int = 3,
        previewWidth: Int = 512,
        previewHeight: Int = 512,
        previewBytes: Long = 7632L,
        previewCrc32: Int = 0x89abcdef.toInt(),
        previewUsable: Int = 1,
        formatFamily: Int = 2,
        contentSchemaMajor: Int = 1,
        contentSchemaMinor: Int = 0,
        loadSupported: Int = 1,
        foodCount: Int = 7,
        foodMass: Double = 18.25,
        createdAtUnixMillis: Long = 1_700_000_000_000L,
        knownOrganismCount: Int = 2,
        organismIdentityComplete: Int = 1,
        innerRadius: Double = 0.0
    ): ByteArray = ByteArrayOutputStream().also { bytes ->
        DataOutputStream(bytes).use { output ->
            output.writeInt(magic)
            output.writeInt(wireVersion)
            output.writeInt(productVersion)
            output.writeDouble(time)
            output.writeInt(cellCount)
            output.writeInt(maxCellCount)
            output.writeInt(maxFoodCount)
            output.writeDouble(radius)
            if (wireVersion >= 3) {
                output.writeInt(previewCodec)
                output.writeInt(previewWidth)
                output.writeInt(previewHeight)
                output.writeLong(previewBytes)
                output.writeInt(previewCrc32)
                output.writeInt(previewUsable)
            }
            if (wireVersion >= 4) {
                output.writeInt(formatFamily)
                output.writeInt(contentSchemaMajor)
                output.writeInt(contentSchemaMinor)
                output.writeInt(loadSupported)
            }
            if (wireVersion >= 5) {
                output.writeInt(foodCount)
                output.writeDouble(foodMass)
                output.writeLong(createdAtUnixMillis)
            }
            if (wireVersion >= 6) {
                output.writeInt(knownOrganismCount)
                output.writeInt(organismIdentityComplete)
            }
            if (wireVersion >= 7) {
                output.writeDouble(innerRadius)
            }
        }
    }.toByteArray()

    private fun cellHeader(version: Int): ByteArray =
        ByteArrayOutputStream().also { bytes ->
            DataOutputStream(bytes).use { output ->
                output.writeInt(0x434c4353)
                output.writeInt(version)
            }
        }.toByteArray()

    private fun cellSnapshotBytes(
        version: Int,
        organismId: Long = 41L,
        parentOrganismId: Long = 17L
    ): ByteArray =
        ByteArrayOutputStream().also { bytes ->
            DataOutputStream(bytes).use { output ->
                output.writeInt(0x434c4353)
                output.writeInt(version)
                output.writeInt(0)
                listOf(
                    0.1,
                    -0.1,
                    0.2,
                    0.012,
                    2.5,
                    0.3,
                    0.4,
                    0.01,
                    -0.02,
                    0.5,
                    -0.03
                ).forEach(output::writeDouble)
                listOf(0.1f, 0.2f, 0.3f).forEach(output::writeFloat)
                output.writeInt(0)
                if (version in 5..7) output.writeInt(7)
                output.writeInt(11)
                output.writeInt(12)
                output.writeInt(3)
                output.writeInt(4)
                output.writeInt(17)
                output.writeByte(1)
                output.writeInt(CellType.PHAGOCYTE.nativeValue)
                listOf(0.2f, 0.4f, 0.6f, 1.0f).forEach(output::writeFloat)
                repeat(4) { index -> output.writeFloat(0.01f * (index + 1)) }
                repeat(4) { index -> output.writeFloat(0.02f * (index + 1)) }
                if (version >= 7) {
                    output.writeLong(organismId)
                    output.writeLong(parentOrganismId)
                }
            }
        }.toByteArray()

    private fun assertInspectionFailure(bytes: ByteArray, code: CellularityErrorCode) {
        assertFailure(bytes, code, NativeWireCodec::decodeSubstrateInspection)
    }

    private fun assertFailure(
        bytes: ByteArray,
        code: CellularityErrorCode,
        decode: (ByteArray) -> Any?
    ) {
        val failure = runCatching { decode(bytes) }.exceptionOrNull()
        assertTrue(failure is CellularityDataException)
        assertEquals(code, (failure as CellularityDataException).code)
    }

}
