package com.cytarch.cellularity.platform.simulation

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MicroscopeRuntimeTelemetryTest {
    @Test
    fun versionedNativePayloadPreservesRateBatchServiceAndDropTelemetry() {
        val values = validTelemetry().apply {
            this[1] = SimulationRatePolicy.TARGET_RATE.ordinal.toLong()
            this[2] = 1
            this[3] = 60.0.toBits()
            this[4] = 7
            this[5] = 1
            this[6] = 59.75.toBits()
            this[7] = 1.195.toBits()
            this[8] = 500_000_000
            this[9] = 30
            this[10] = 8
            this[11] = 2
            this[12] = 100
            this[13] = 90
            this[14] = 450
            this[15] = 40_000_000
            this[16] = 900_000
            this[17] = 6
            this[18] = 9
            this[19] = 600_000
            this[20] = 12
            this[21] = 3
            this[22] = 1
            this[23] = 90_000
            this[24] = 110_000
            this[25] = 137_500
            this[26] = 1
            this[27] = 2
            this[28] = 250_000
            this[29] = SimulationBatchStopReason.SERVICE_REQUESTED.ordinal.toLong()
            this[30] = 180
            this[31] = 650_000
            this[32] = 4_100_000
            this[33] = 21
            this[34] = 3
            this[35] = 19
            this[36] = 4
            this[37] = 12
            this[38] = 5
            this[39] = 6
            this[40] = 1
            this[41] = 0
        }

        val telemetry = NativeMicroscopeRuntimeTelemetryCodec.decode(values)!!

        assertEquals(SimulationRatePolicy.TARGET_RATE, telemetry.ratePolicy)
        assertEquals(60.0, telemetry.requestedTargetUps)
        assertEquals(59.75, telemetry.actualCompletedUps, 0.0)
        assertEquals(1.195, telemetry.actualSimulationHoursPerSecond, 0.0)
        assertEquals(500_000_000, telemetry.actualRateWindowNanos)
        assertEquals(90, telemetry.observedBatchCount)
        assertEquals(40_000_000, telemetry.totalObservedBatchNanos)
        assertEquals(9, telemetry.maximumBatchUpdateCount)
        assertEquals(137_500, telemetry.estimatedUpdateCostNanos)
        assertEquals(2, telemetry.deadlineOverrunCount)
        assertEquals(4_100_000, telemetry.maximumOwnerServiceIntervalNanos)
        assertEquals(3, telemetry.droppedSourceFrameCount)
        assertEquals(4, telemetry.droppedPreparedFrameCount)
        assertEquals(6, telemetry.statusProducerReplacedCount)
        assertEquals(SimulationBatchStopReason.SERVICE_REQUESTED, telemetry.lastStopReason)
    }

    @Test
    fun maxThroughputCannotAcquireANumericRequestedTarget() {
        val maximum = validTelemetry().apply {
            this[1] = SimulationRatePolicy.MAX_THROUGHPUT.ordinal.toLong()
        }
        val invalidMaximum = maximum.copyOf().apply {
            this[2] = 1
            this[3] = 1_000.0.toBits()
        }
        val invalidTarget = maximum.copyOf().apply {
            this[1] = SimulationRatePolicy.TARGET_RATE.ordinal.toLong()
        }

        assertNull(NativeMicroscopeRuntimeTelemetryCodec.decode(maximum)?.requestedTargetUps)
        assertNull(NativeMicroscopeRuntimeTelemetryCodec.decode(invalidMaximum))
        assertNull(NativeMicroscopeRuntimeTelemetryCodec.decode(invalidTarget))
    }

    @Test
    fun malformedTelemetryIsRejectedAsOneSnapshot() {
        val wrongSchema = validTelemetry().apply { this[0] = 2 }
        val negativeCounter = validTelemetry().apply { this[34] = -1 }
        val nonFiniteRate = validTelemetry().apply {
            this[6] = Double.NaN.toBits()
        }

        assertNull(NativeMicroscopeRuntimeTelemetryCodec.decode(wrongSchema))
        assertNull(NativeMicroscopeRuntimeTelemetryCodec.decode(negativeCounter))
        assertNull(NativeMicroscopeRuntimeTelemetryCodec.decode(nonFiniteRate))
        assertNull(NativeMicroscopeRuntimeTelemetryCodec.decode(LongArray(41)))
    }

    private fun validTelemetry(): LongArray = LongArray(42).apply {
        this[0] = 1
        this[1] = SimulationRatePolicy.PAUSED.ordinal.toLong()
        this[5] = 1
        this[22] = 1
    }
}
