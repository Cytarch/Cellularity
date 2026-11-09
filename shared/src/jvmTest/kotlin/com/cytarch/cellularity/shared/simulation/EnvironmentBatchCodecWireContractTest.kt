package com.cytarch.cellularity.shared.simulation

import com.cytarch.cellularity.shared.model.EnvironmentContract
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs

private const val UNKNOWN_REASON_WIRE_ID = 99

/**
 * Wire-contract checks that the round-trip tests in [EnvironmentBatchCodecTest] do not
 * reach.
 *
 * The codec's reader is hand-written, so the two ways it can go quietly wrong are a
 * short read on a truncated document and a byte-order disagreement with the native
 * writer in native/io/NativeByteStream.h. Both are asserted here directly: every
 * possible cut of a valid document must fail as an [EnvironmentContractException], and
 * the numeric fields must sit in the payload most significant byte first, which
 * [DataOutputStream] and the native writer both do.
 */
class EnvironmentBatchCodecWireContractTest {
    @Test
    fun everyTruncatedStatePrefixFailsAsAContractError() {
        val complete = stateBytes()

        // The complete document must decode, so only the cut can explain each failure.
        EnvironmentBatchCodec.decodeSnapshot(complete)

        for (length in 0 until complete.size) {
            assertFailsWith<EnvironmentContractException>(
                "environment state cut to $length of ${complete.size} bytes"
            ) {
                EnvironmentBatchCodec.decodeSnapshot(complete.copyOf(length))
            }
        }
    }

    @Test
    fun everyTruncatedResultPrefixFailsAsAContractError() {
        val patch = EnvironmentPatch.single(
            EnvironmentChange.ScalarValue(EnvironmentScalarProperty.RADIATION, 0.5),
            baseRevision = 3
        )
        val complete = resultBytes(applied = true, revision = 4)

        EnvironmentBatchCodec.decodeApplyResult(complete, patch)

        for (length in 0 until complete.size) {
            assertFailsWith<EnvironmentContractException>(
                "environment apply result cut to $length of ${complete.size} bytes"
            ) {
                EnvironmentBatchCodec.decodeApplyResult(complete.copyOf(length), patch)
            }
        }
    }

    @Test
    fun everyTruncatedRejectionPrefixFailsAsAContractError() {
        val patch = EnvironmentPatch.single(
            EnvironmentChange.ScalarValue(EnvironmentScalarProperty.RADIATION, 99.0),
            baseRevision = 3
        )
        val complete = resultBytes(
            applied = false,
            revision = 3,
            rejections = listOf(0 to EnvironmentPatchRejectionReason.OUT_OF_RANGE)
        )

        EnvironmentBatchCodec.decodeApplyResult(complete, patch)

        for (length in 0 until complete.size) {
            assertFailsWith<EnvironmentContractException>(
                "rejected apply result cut to $length of ${complete.size} bytes"
            ) {
                EnvironmentBatchCodec.decodeApplyResult(complete.copyOf(length), patch)
            }
        }
    }

    @Test
    fun encodedPatchNumbersAreMostSignificantByteFirst() {
        val patch = EnvironmentPatch.single(
            EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, true),
            baseRevision = 0x0102030405060708L
        )

        val encoded = EnvironmentBatchCodec.encodePatch(patch)

        // "CEVP", then schema, then the base revision, then the change count.
        assertContentEquals(byteArrayOf(0x43, 0x45, 0x56, 0x50), encoded.copyOfRange(0, 4))
        assertContentEquals(byteArrayOf(0, 0, 0, 1), encoded.copyOfRange(4, 8))
        assertContentEquals(byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8), encoded.copyOfRange(8, 16))
        assertContentEquals(byteArrayOf(0, 0, 0, 1), encoded.copyOfRange(16, 20))
    }

    @Test
    fun decodedRevisionIsMostSignificantByteFirst() {
        val document = stateBytes(revision = 0x0102030405060708L)

        // Assert the bytes before asserting the value, so this cannot pass by reading a
        // document the oracle happened to lay out some other way.
        assertContentEquals(byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8), document.copyOfRange(8, 16))
        assertEquals(
            0x0102030405060708L,
            EnvironmentBatchCodec.decodeSnapshot(document).revision
        )
    }

    @Test
    fun resultRejectsItsOwnHeaderRevisionAndRejectionCount() {
        val patch = EnvironmentPatch.single(
            EnvironmentChange.ScalarValue(EnvironmentScalarProperty.RADIATION, 0.5),
            baseRevision = 3
        )
        val wrongMagic = resultBytes(applied = true, revision = 4).also { it[0] = 0 }
        val wrongSchema = resultBytes(
            applied = true,
            revision = 4,
            schemaVersion = EnvironmentContract.SCHEMA_VERSION + 1
        )
        val negativeRevision = resultBytes(applied = true, revision = -1)
        val tooManyRejections = rawResultBytes(
            rejectionCount = patch.changes.size + 2,
            entries = emptyList()
        )

        assertFailsWith<EnvironmentContractException>("result header") {
            EnvironmentBatchCodec.decodeApplyResult(wrongMagic, patch)
        }
        assertFailsWith<EnvironmentContractException>("result schema") {
            EnvironmentBatchCodec.decodeApplyResult(wrongSchema, patch)
        }
        assertFailsWith<EnvironmentContractException>("result revision") {
            EnvironmentBatchCodec.decodeApplyResult(negativeRevision, patch)
        }
        assertFailsWith<EnvironmentContractException>("result rejection count") {
            EnvironmentBatchCodec.decodeApplyResult(tooManyRejections, patch)
        }
    }

    @Test
    fun unknownRejectionReasonIsWrappedAsAContractError() {
        val patch = EnvironmentPatch.single(
            EnvironmentChange.ScalarValue(EnvironmentScalarProperty.RADIATION, 0.5),
            baseRevision = 3
        )

        val failure = assertFailsWith<EnvironmentContractException>("unknown rejection reason") {
            EnvironmentBatchCodec.decodeApplyResult(
                rawResultBytes(rejectionCount = 1, entries = listOf(0 to UNKNOWN_REASON_WIRE_ID)),
                patch
            )
        }

        // An unknown wire id is an IllegalArgumentException in the enum, and the codec is
        // the boundary that turns it into the contract failure callers catch.
        assertIs<IllegalArgumentException>(failure.cause)
    }

    @Test
    fun snapshotRejectsPayloadAboveTheWireSizeLimit() {
        assertFailsWith<EnvironmentContractException> {
            EnvironmentBatchCodec.decodeSnapshot(ByteArray(EnvironmentBatchCodec.MAX_WIRE_BYTES + 1))
        }
        assertFailsWith<EnvironmentContractException> {
            EnvironmentBatchCodec.decodeApplyResult(
                ByteArray(EnvironmentBatchCodec.MAX_WIRE_BYTES + 1),
                EnvironmentPatch.single(
                    EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, true),
                    baseRevision = 0
                )
            )
        }
    }

    @Test
    fun snapshotRejectsWrongMagicAndNegativeRevision() {
        val wrongMagic = stateBytes().also { it[0] = 0 }
        val negativeRevision = stateBytes(revision = -1)

        assertFailsWith<EnvironmentContractException> {
            EnvironmentBatchCodec.decodeSnapshot(wrongMagic)
        }
        assertFailsWith<EnvironmentContractException> {
            EnvironmentBatchCodec.decodeSnapshot(negativeRevision)
        }
    }

    private fun stateBytes(
        revision: Long = 41,
        schemaVersion: Int = EnvironmentContract.SCHEMA_VERSION,
        genePoolCount: Int = EnvironmentContract.GENE_POOL_SIZE,
        mutationWeightCount: Int = EnvironmentContract.MUTATION_WEIGHT_COUNT
    ): ByteArray = writeBytes { output ->
        output.writeInt(EnvironmentBatchCodec.STATE_MAGIC)
        output.writeInt(schemaVersion)
        output.writeLong(revision)
        listOf(true, false, true, false, true, true).forEach(output::writeBoolean)
        output.writeInt(VectorFieldType.RADIAL_OUTWARD.nativeValue)
        output.writeDouble(0.11)
        output.writeDouble(-0.12)
        output.writeScalarField(ScalarFieldType.RADIAL, -0.21, 0.22, 0.23, -0.24, 0.25)
        output.writeScalarField(ScalarFieldType.LINEAR_DIRECTIONAL, 0.31, -0.32, 0.33, 0.34, 0.35)
        output.writeScalarField(ScalarFieldType.CONSTANT, 0.41, -0.42, -0.43, 0.44, 0.45)
        (1..23).forEach { output.writeDouble(it.toDouble()) }
        output.writeInt(genePoolCount)
        repeat(genePoolCount) { output.writeBoolean(it % 2 == 0) }
        output.writeInt(mutationWeightCount)
        repeat(mutationWeightCount) { output.writeDouble(it / 10.0) }
    }

    private fun resultBytes(
        applied: Boolean,
        revision: Long,
        rejections: List<Pair<Int, EnvironmentPatchRejectionReason>> = emptyList(),
        schemaVersion: Int = EnvironmentContract.SCHEMA_VERSION
    ): ByteArray = writeBytes { output ->
        output.writeInt(EnvironmentBatchCodec.RESULT_MAGIC)
        output.writeInt(schemaVersion)
        output.writeBoolean(applied)
        output.writeLong(revision)
        output.writeInt(rejections.size)
        rejections.forEach { (index, reason) ->
            output.writeInt(index)
            output.writeInt(reason.wireId)
        }
    }

    /** Writes the declared rejection count independently of the entries that follow it. */
    private fun rawResultBytes(
        rejectionCount: Int,
        entries: List<Pair<Int, Int>>,
        applied: Boolean = false,
        revision: Long = 3
    ): ByteArray = writeBytes { output ->
        output.writeInt(EnvironmentBatchCodec.RESULT_MAGIC)
        output.writeInt(EnvironmentContract.SCHEMA_VERSION)
        output.writeBoolean(applied)
        output.writeLong(revision)
        output.writeInt(rejectionCount)
        entries.forEach { (index, reason) ->
            output.writeInt(index)
            output.writeInt(reason)
        }
    }

    private fun writeBytes(write: (DataOutputStream) -> Unit): ByteArray {
        val bytes = ByteArrayOutputStream()
        DataOutputStream(bytes).use(write)
        return bytes.toByteArray()
    }
}

private fun DataOutputStream.writeScalarField(
    type: ScalarFieldType,
    base: Double,
    strength: Double,
    angle: Double,
    offset: Double,
    softness: Double
) {
    writeInt(type.nativeValue)
    writeDouble(base)
    writeDouble(strength)
    writeDouble(angle)
    writeDouble(offset)
    writeDouble(softness)
}
