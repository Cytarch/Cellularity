package com.cytarch.cellularity.platform.simulation

import com.cytarch.cellularity.shared.model.SimulationStats
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Test

class MicroscopeRuntimeDiagnosticsTest {
    @After
    fun tearDown() {
        MicroscopeRuntimeDiagnostics.resetProfile()
    }

    @Test
    fun exposesSchedulerAndFrameExchangeObservations() {
        MicroscopeRuntimeDiagnostics.update(
            MicroscopeRuntimeTelemetry(
                ratePolicy = SimulationRatePolicy.MAX_THROUGHPUT,
                actualRateValid = true,
                actualCompletedUps = 2_345.5,
                publishedSourceFrameCount = 120,
                droppedSourceFrameCount = 4,
                publishedPreparedFrameCount = 116,
                droppedPreparedFrameCount = 2
            ),
            SimulationStats(
                cellCount = 763,
                foodMass = 1_453.25,
                time = 1323.0,
                updateCount = 42
            )
        )

        val report = MicroscopeRuntimeDiagnostics.snapshot()
        assertTrue(report.contains("rate_policy=max_throughput"))
        assertTrue(report.contains("actual_completed_ups=2345.500"))
        assertTrue(report.contains("cell_count=763"))
        assertTrue(report.contains("food_mass=1453.250000"))
        assertTrue(report.contains("simulation_time=1323.000000"))
        assertTrue(report.contains("simulation_update_count=42"))
        assertTrue(report.contains("published_source_frame_count=120"))
        assertTrue(report.contains("dropped_source_frame_count=4"))
        assertTrue(report.contains("published_prepared_frame_count=116"))
        assertTrue(report.contains("dropped_prepared_frame_count=2"))
    }

    @Test
    fun reportsCounterDeltasForTheProfileWindow() {
        val baselineTelemetry = MicroscopeRuntimeTelemetry(
            observedUpdateCount = 100,
            publishedSourceFrameCount = 10,
            droppedSourceFrameCount = 2,
            publishedPreparedFrameCount = 8,
            droppedPreparedFrameCount = 3,
            statusProducerReplacedCount = 4,
            statusConsumerDiscardedCount = 5
        )
        MicroscopeRuntimeDiagnostics.update(
            baselineTelemetry,
            SimulationStats(cellCount = 20, updateCount = 1_000)
        )
        MicroscopeRuntimeDiagnostics.startProfile()
        MicroscopeRuntimeDiagnostics.update(
            baselineTelemetry.copy(
                observedUpdateCount = 175,
                publishedSourceFrameCount = 16,
                droppedSourceFrameCount = 3,
                publishedPreparedFrameCount = 13,
                droppedPreparedFrameCount = 5,
                statusProducerReplacedCount = 4,
                statusConsumerDiscardedCount = 9
            ),
            SimulationStats(cellCount = 18, updateCount = 1_075)
        )

        val report = MicroscopeRuntimeDiagnostics.snapshot()
        assertTrue(report.contains("profile_observed_update_count=75"))
        assertTrue(report.contains("profile_published_source_frame_count=6"))
        assertTrue(report.contains("profile_dropped_source_frame_count=1"))
        assertTrue(report.contains("profile_published_prepared_frame_count=5"))
        assertTrue(report.contains("profile_dropped_prepared_frame_count=2"))
        assertTrue(report.contains("profile_status_producer_replaced_count=0"))
        assertTrue(report.contains("profile_status_consumer_discarded_count=4"))
        assertTrue(report.contains("profile_simulation_update_count=75"))
        assertTrue(report.contains("profile_cell_count_change=-2"))

    }
}
