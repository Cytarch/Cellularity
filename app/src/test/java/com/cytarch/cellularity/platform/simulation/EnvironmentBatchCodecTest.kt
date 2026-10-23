package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.shared.model.EnvironmentContract
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.EnvironmentBooleanProperty
import com.cytarch.cellularity.shared.simulation.EnvironmentChange
import com.cytarch.cellularity.shared.simulation.EnvironmentField
import com.cytarch.cellularity.shared.simulation.EnvironmentFieldComponent
import com.cytarch.cellularity.shared.simulation.EnvironmentPatch
import com.cytarch.cellularity.shared.simulation.EnvironmentPatchRejectionReason
import com.cytarch.cellularity.shared.simulation.EnvironmentScalarProperty
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class EnvironmentBatchCodecTest {
    @Test
    fun snapshotDecodesEveryFieldAndOwnsCollections() {
        val bytes = stateBytes()

        val snapshot = EnvironmentBatchCodec.decodeSnapshot(bytes)

        assertEquals(EnvironmentContract.SCHEMA_VERSION, snapshot.schemaVersion)
        assertEquals(41, snapshot.revision)
        assertTrue(snapshot.addRandom)
        assertFalse(snapshot.dieEdge)
        assertTrue(snapshot.onlyTweakMutations)
        assertFalse(snapshot.mutationOnlyAfterSplit)
        assertTrue(snapshot.aging)
        assertTrue(snapshot.mobileFood)
        assertEquals(VectorFieldType.RADIAL_OUTWARD, snapshot.gravityField.type)
        assertEquals(0.11, snapshot.gravityField.strength, 0.0)
        assertEquals(-0.12, snapshot.gravityField.angle, 0.0)
        assertEquals(ScalarFieldType.RADIAL, snapshot.densityField.type)
        assertEquals(listOf(-0.21, 0.22, 0.23, -0.24, 0.25), snapshot.densityField.values())
        assertEquals(ScalarFieldType.LINEAR_DIRECTIONAL, snapshot.salinityField.type)
        assertEquals(listOf(0.31, -0.32, 0.33, 0.34, 0.35), snapshot.salinityField.values())
        assertEquals(ScalarFieldType.CONSTANT, snapshot.foodLipidLayerField.type)
        assertEquals(listOf(0.41, -0.42, -0.43, 0.44, 0.45), snapshot.foodLipidLayerField.values())
        assertEquals((1..23).map(Int::toDouble), snapshot.scalarValues())
        assertEquals(EnvironmentContract.GENE_POOL_SIZE, snapshot.genePool.size)
        assertEquals(EnvironmentContract.MUTATION_WEIGHT_COUNT, snapshot.mutationWeights.size)
        assertFalse(snapshot.genePool is MutableList<*>)
        assertFalse(snapshot.mutationWeights is MutableList<*>)
    }

    @Test
    fun patchEncodesStableIdsForEveryChangeFamily() {
        val patch = EnvironmentPatch(
            baseRevision = 7,
            changes = listOf(
                EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, true),
                EnvironmentChange.ScalarValue(EnvironmentScalarProperty.NITRATES, 0.75),
                EnvironmentChange.FieldType(EnvironmentField.GRAVITY, VectorFieldType.RADIAL_INWARD),
                EnvironmentChange.FieldType(EnvironmentField.DENSITY, ScalarFieldType.RADIAL),
                EnvironmentChange.FieldValue(
                    EnvironmentField.SALINITY,
                    EnvironmentFieldComponent.OFFSET,
                    -0.25
                ),
                EnvironmentChange.MutationWeight(3, 0.5),
                EnvironmentChange.ResetMutationWeights,
                EnvironmentChange.GenePoolEnabled(2, false)
            )
        )
        val input = DataInputStream(ByteArrayInputStream(EnvironmentBatchCodec.encodePatch(patch)))

        assertEquals(EnvironmentBatchCodec.PATCH_MAGIC, input.readInt())
        assertEquals(EnvironmentContract.SCHEMA_VERSION, input.readInt())
        assertEquals(7, input.readLong())
        assertEquals(8, input.readInt())
        assertEquals(listOf(1, EnvironmentBooleanProperty.AGING.wireId), input.readInts(2))
        assertTrue(input.readBoolean())
        assertEquals(listOf(2, EnvironmentScalarProperty.NITRATES.wireId), input.readInts(2))
        assertEquals(0.75, input.readDouble(), 0.0)
        assertEquals(listOf(3, EnvironmentField.GRAVITY.wireId, 2, 1), input.readInts(4))
        assertEquals(listOf(3, EnvironmentField.DENSITY.wireId, 1, 2), input.readInts(4))
        assertEquals(
            listOf(4, EnvironmentField.SALINITY.wireId, EnvironmentFieldComponent.OFFSET.wireId),
            input.readInts(3)
        )
        assertEquals(-0.25, input.readDouble(), 0.0)
        assertEquals(listOf(5, 3), input.readInts(2))
        assertEquals(0.5, input.readDouble(), 0.0)
        assertEquals(6, input.readInt())
        assertEquals(listOf(7, 2), input.readInts(2))
        assertFalse(input.readBoolean())
        assertEquals(0, input.available())
    }

    @Test
    fun appliedResultCarriesNativeRevision() {
        val patch = EnvironmentPatch.single(
            EnvironmentChange.ScalarValue(EnvironmentScalarProperty.RADIATION, 0.02),
            baseRevision = 4
        )

        val result = EnvironmentBatchCodec.decodeApplyResult(
            resultBytes(applied = true, revision = 5),
            patch
        )

        assertEquals(ApplyResult.Applied(5), result)
    }

    @Test
    fun rejectedResultCarriesEveryTypedFieldRejection() {
        val patch = EnvironmentPatch(
            baseRevision = 4,
            changes = listOf(
                EnvironmentChange.ScalarValue(EnvironmentScalarProperty.RADIATION, 99.0),
                EnvironmentChange.MutationWeight(-1, 0.5)
            )
        )

        val result = EnvironmentBatchCodec.decodeApplyResult(
            resultBytes(
                applied = false,
                revision = 4,
                rejections = listOf(
                    0 to EnvironmentPatchRejectionReason.OUT_OF_RANGE,
                    1 to EnvironmentPatchRejectionReason.INVALID_INDEX
                )
            ),
            patch
        ) as ApplyResult.Rejected

        assertEquals(4, result.currentRevision)
        assertEquals(listOf(0, 1), result.rejections.map { it.changeIndex })
        assertEquals(
            listOf(
                EnvironmentPatchRejectionReason.OUT_OF_RANGE,
                EnvironmentPatchRejectionReason.INVALID_INDEX
            ),
            result.rejections.map { it.reason }
        )
    }

    @Test
    fun snapshotRejectsWrongSchemaAndCollectionCounts() {
        val wrongSchema = stateBytes(schemaVersion = EnvironmentContract.SCHEMA_VERSION + 1)
        val wrongGenePoolCount = stateBytes(genePoolCount = EnvironmentContract.GENE_POOL_SIZE - 1)

        assertThrows(EnvironmentContractException::class.java) {
            EnvironmentBatchCodec.decodeSnapshot(wrongSchema)
        }
        assertThrows(EnvironmentContractException::class.java) {
            EnvironmentBatchCodec.decodeSnapshot(wrongGenePoolCount)
        }
    }

    @Test
    fun snapshotRejectsNonFiniteAndTrailingData() {
        val nonFinite = stateBytes(firstScalar = Double.NaN)
        val trailing = stateBytes() + byteArrayOf(1)

        assertThrows(EnvironmentContractException::class.java) {
            EnvironmentBatchCodec.decodeSnapshot(nonFinite)
        }
        assertThrows(EnvironmentContractException::class.java) {
            EnvironmentBatchCodec.decodeSnapshot(trailing)
        }
    }

    @Test
    fun resultRejectsMalformedStatusIndexAndReason() {
        val patch = EnvironmentPatch.single(
            EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, true),
            baseRevision = 1
        )
        val inconsistent = resultBytes(
            applied = true,
            revision = 2,
            rejections = listOf(0 to EnvironmentPatchRejectionReason.OUT_OF_RANGE)
        )
        val unknownIndex = rawResultBytes(applied = false, revision = 1, changeIndex = 8, reason = 9)
        val unknownReason = rawResultBytes(applied = false, revision = 1, changeIndex = 0, reason = 99)

        assertThrows(EnvironmentContractException::class.java) {
            EnvironmentBatchCodec.decodeApplyResult(inconsistent, patch)
        }
        assertThrows(EnvironmentContractException::class.java) {
            EnvironmentBatchCodec.decodeApplyResult(unknownIndex, patch)
        }
        assertThrows(EnvironmentContractException::class.java) {
            EnvironmentBatchCodec.decodeApplyResult(unknownReason, patch)
        }
    }

    @Test
    fun patchEnforcesNonEmptyBoundedOwnedChangeList() {
        assertThrows(IllegalArgumentException::class.java) {
            EnvironmentPatch(baseRevision = 0, changes = emptyList())
        }
        assertThrows(IllegalArgumentException::class.java) {
            EnvironmentPatch(
                baseRevision = 0,
                changes = List(EnvironmentContract.MAX_PATCH_CHANGES + 1) {
                    EnvironmentChange.BooleanValue(EnvironmentBooleanProperty.AGING, true)
                }
            )
        }
        val source = mutableListOf<EnvironmentChange>(
            EnvironmentChange.ScalarValue(EnvironmentScalarProperty.RADIATION, Double.NaN)
        )
        val patch = EnvironmentPatch(baseRevision = 0, changes = source)
        source.clear()
        val encoded = EnvironmentBatchCodec.encodePatch(patch)
        val input = DataInputStream(ByteArrayInputStream(encoded))
        input.skipBytes(4 + 4 + 8 + 4 + 4 + 4)
        assertTrue(input.readDouble().isNaN())
        assertEquals(1, patch.changes.size)
    }

    private fun stateBytes(
        schemaVersion: Int = EnvironmentContract.SCHEMA_VERSION,
        genePoolCount: Int = EnvironmentContract.GENE_POOL_SIZE,
        firstScalar: Double = 1.0
    ): ByteArray = writeBytes { output ->
        output.writeInt(EnvironmentBatchCodec.STATE_MAGIC)
        output.writeInt(schemaVersion)
        output.writeLong(41)
        listOf(true, false, true, false, true, true).forEach(output::writeBoolean)
        output.writeInt(VectorFieldType.RADIAL_OUTWARD.nativeValue)
        output.writeDouble(0.11)
        output.writeDouble(-0.12)
        output.writeScalarField(ScalarFieldType.RADIAL, -0.21, 0.22, 0.23, -0.24, 0.25)
        output.writeScalarField(ScalarFieldType.LINEAR_DIRECTIONAL, 0.31, -0.32, 0.33, 0.34, 0.35)
        output.writeScalarField(ScalarFieldType.CONSTANT, 0.41, -0.42, -0.43, 0.44, 0.45)
        output.writeDouble(firstScalar)
        (2..23).forEach { output.writeDouble(it.toDouble()) }
        output.writeInt(genePoolCount)
        repeat(genePoolCount) { output.writeBoolean(it % 2 == 0) }
        output.writeInt(EnvironmentContract.MUTATION_WEIGHT_COUNT)
        repeat(EnvironmentContract.MUTATION_WEIGHT_COUNT) { output.writeDouble(it / 10.0) }
    }

    private fun resultBytes(
        applied: Boolean,
        revision: Long,
        rejections: List<Pair<Int, EnvironmentPatchRejectionReason>> = emptyList()
    ): ByteArray = writeBytes { output ->
        output.writeInt(EnvironmentBatchCodec.RESULT_MAGIC)
        output.writeInt(EnvironmentContract.SCHEMA_VERSION)
        output.writeBoolean(applied)
        output.writeLong(revision)
        output.writeInt(rejections.size)
        rejections.forEach { (index, reason) ->
            output.writeInt(index)
            output.writeInt(reason.wireId)
        }
    }

    private fun rawResultBytes(
        applied: Boolean,
        revision: Long,
        changeIndex: Int,
        reason: Int
    ): ByteArray = writeBytes { output ->
        output.writeInt(EnvironmentBatchCodec.RESULT_MAGIC)
        output.writeInt(EnvironmentContract.SCHEMA_VERSION)
        output.writeBoolean(applied)
        output.writeLong(revision)
        output.writeInt(1)
        output.writeInt(changeIndex)
        output.writeInt(reason)
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

private fun DataInputStream.readInts(count: Int): List<Int> = List(count) { readInt() }

private fun com.cytarch.cellularity.shared.model.ScalarFieldSnapshot.values() =
    listOf(base, strength, angle, offset, softness)

private fun com.cytarch.cellularity.shared.model.EnvironmentSnapshot.scalarValues() = listOf(
    radiation,
    testMulticellularCoef,
    testMutationCoef,
    testFoodDecay,
    testIdleMetabolismScale,
    testFlagellocyteCostScale,
    testPhagocyteEatSpeedScale,
    viscosity,
    frictionDynamic,
    frictionStatic,
    foodSpeed,
    foodSize,
    foodLumpiness,
    foodLumpSize,
    foodLipidLayer,
    salinity,
    lightIntensity,
    lightRange,
    lightSpeed,
    gravity,
    density,
    densityGradient,
    nitrates
)
