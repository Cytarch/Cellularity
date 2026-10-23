package com.cytarch.cellularity.platform.render.preview

import com.cytarch.cellularity.NativeCellularityControllerBridge
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.ui.play.substrate_info.EnvironmentFieldTarget
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateFieldRanges
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateFieldUiState
import java.lang.reflect.Modifier
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class EnvironmentFieldPreviewStateTest {
    @Test
    fun everyScalarTargetAndTypeUsesExplicitStableWireValues() {
        val targets = listOf(
            EnvironmentFieldTarget.DENSITY to 0,
            EnvironmentFieldTarget.SALINITY to 1,
            EnvironmentFieldTarget.FOOD_LIPID_LAYER to 2
        )
        val types = listOf(
            ScalarFieldType.CONSTANT to 0,
            ScalarFieldType.LINEAR_DIRECTIONAL to 1,
            ScalarFieldType.RADIAL to 2
        )

        targets.forEach { (target, targetWire) ->
            types.forEach { (type, typeWire) ->
                val update = state(target, type).toEnvironmentFieldPreviewUpdate()
                assertEquals(targetWire, update.targetWire)
                assertEquals(typeWire, update.fieldTypeWire)
                assertEquals(0.11, update.base, 0.0)
                assertEquals(0.22, update.strength, 0.0)
                assertEquals(0.33, update.angle, 0.0)
                assertEquals(0.44, update.offset, 0.0)
                assertEquals(0.55, update.softness, 0.0)
            }
        }
    }

    @Test
    fun everyGravityTypeUsesItsOwnWireDomainAndCanonicalReservedValues() {
        val types = listOf(
            VectorFieldType.LINEAR_DIRECTIONAL to 0,
            VectorFieldType.RADIAL_INWARD to 1,
            VectorFieldType.RADIAL_OUTWARD to 2
        )

        types.forEach { (type, typeWire) ->
            val update = state(
                EnvironmentFieldTarget.GRAVITY,
                type
            ).toEnvironmentFieldPreviewUpdate()
            assertEquals(3, update.targetWire)
            assertEquals(typeWire, update.fieldTypeWire)
            assertEquals(0L, update.base.toRawBits())
            assertEquals(0L, update.offset.toRawBits())
            assertEquals(0L, update.softness.toRawBits())
            assertEquals(0.22, update.strength, 0.0)
            assertEquals(0.33, update.angle, 0.0)
        }
    }

    @Test
    fun targetRejectsFieldTypesFromTheOtherDomain() {
        assertThrows(IllegalArgumentException::class.java) {
            state(
                EnvironmentFieldTarget.GRAVITY,
                ScalarFieldType.LINEAR_DIRECTIONAL
            ).toEnvironmentFieldPreviewUpdate()
        }
        assertThrows(IllegalArgumentException::class.java) {
            state(
                EnvironmentFieldTarget.DENSITY,
                VectorFieldType.LINEAR_DIRECTIONAL
            ).toEnvironmentFieldPreviewUpdate()
        }
    }

    @Test
    fun efa1DecoderReadsEveryStatusAndUnsignedRevisionBits() {
        EnvironmentFieldPreviewUpdateStatus.entries.forEach { status ->
            val accepted = status == EnvironmentFieldPreviewUpdateStatus.APPLIED ||
                status == EnvironmentFieldPreviewUpdateStatus.DUPLICATE
            val revision = if (accepted) Long.MIN_VALUE else 0L
            val result = EnvironmentFieldPreviewUpdateResultLayout.decode(
                resultBuffer(status.wireValue, revision)
            )
            assertEquals(status, result.status)
            assertEquals(revision, result.ingressRevision)
            assertEquals(accepted, result.accepted)
        }
    }

    @Test
    fun efa1DecoderRejectsInvalidStorageHeaderAndStatusSemantics() {
        assertThrows(IllegalArgumentException::class.java) {
            EnvironmentFieldPreviewUpdateResultLayout.decode(
                ByteBuffer.allocate(EnvironmentFieldPreviewUpdateResultLayout.SIZE_BYTES)
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            EnvironmentFieldPreviewUpdateResultLayout.decode(ByteBuffer.allocateDirect(8))
        }
        assertThrows(IllegalStateException::class.java) {
            EnvironmentFieldPreviewUpdateResultLayout.decode(
                resultBuffer(0, 1).putInt(0, 0)
            )
        }
        assertThrows(IllegalStateException::class.java) {
            EnvironmentFieldPreviewUpdateResultLayout.decode(resultBuffer(0, 0))
        }
        assertThrows(IllegalStateException::class.java) {
            EnvironmentFieldPreviewUpdateResultLayout.decode(resultBuffer(4, 1))
        }
        assertThrows(IllegalStateException::class.java) {
            EnvironmentFieldPreviewUpdateResultLayout.decode(resultBuffer(99, 0))
        }
    }

    @Test
    fun exactNativePublishMethodsBelongOnlyToTheFieldBridge() {
        val publish = EnvironmentFieldPreviewBridge::class.java.getDeclaredMethod(
            "nativePublishEnvironmentFieldPreviewState",
            Long::class.javaPrimitiveType,
            Long::class.javaPrimitiveType,
            Int::class.javaPrimitiveType,
            Int::class.javaPrimitiveType,
            Double::class.javaPrimitiveType,
            Double::class.javaPrimitiveType,
            Double::class.javaPrimitiveType,
            Double::class.javaPrimitiveType,
            Double::class.javaPrimitiveType,
            ByteBuffer::class.java
        )
        assertTrue(Modifier.isPrivate(publish.modifiers))
        assertTrue(Modifier.isStatic(publish.modifiers))
        assertTrue(Modifier.isNative(publish.modifiers))
        assertEquals(Void.TYPE, publish.returnType)
        assertEquals(
            1,
            EnvironmentFieldPreviewBridge::class.java.declaredMethods.count {
                it.name == "nativePublishEnvironmentFieldPreviewState"
            }
        )
        assertFalse(
            EnvironmentFieldPreviewBridge::class.java.declaredMethods.any {
                it.name == "nativeResolveEndpointGeneration"
            }
        )
        assertFalse(
            NativeCellularityControllerBridge::class.java.declaredMethods.any {
                it.name == "nativePublishEnvironmentFieldPreviewState" ||
                    it.name == "nativeResolveEndpointGeneration"
            }
        )
    }

    private fun state(
        target: EnvironmentFieldTarget,
        type: com.cytarch.cellularity.shared.model.EnvironmentFieldType
    ) = SubstrateFieldUiState(
        target = target,
        type = type,
        base = 0.11,
        strength = 0.22,
        angle = 0.33,
        offset = 0.44,
        softness = 0.55,
        ranges = SubstrateFieldRanges(-1.0, 1.0, -2.0, 2.0)
    )

    private fun resultBuffer(status: Int, revision: Long): ByteBuffer =
        EnvironmentFieldPreviewUpdateResultLayout.allocateBuffer().apply {
            order(ByteOrder.LITTLE_ENDIAN)
            putInt(0, EnvironmentFieldPreviewUpdateResultLayout.MAGIC)
            putShort(4, EnvironmentFieldPreviewUpdateResultLayout.VERSION.toShort())
            putShort(6, EnvironmentFieldPreviewUpdateResultLayout.SIZE_BYTES.toShort())
            putInt(8, status)
            putInt(12, 0)
            putLong(16, revision)
        }
}
