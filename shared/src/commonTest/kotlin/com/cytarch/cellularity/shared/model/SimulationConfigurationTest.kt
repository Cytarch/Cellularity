package com.cytarch.cellularity.shared.model

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class SimulationConfigurationTest {
    @Test
    fun commonContractChecksShapeWithoutDuplicatingNativeMemoryPolicy() {
        assertTrue(SimulationContract.isConfigurationValid(1_000, 3_500, 1.0))
        assertTrue(SimulationContract.isConfigurationValid(1, 0, 1.0))
        assertTrue(SimulationContract.isConfigurationValid(1_000_000, 0, 1.0))
        assertFalse(SimulationContract.isConfigurationValid(0, 0, 1.0))
        assertFalse(SimulationContract.isConfigurationValid(1, -1, 1.0))
        assertFalse(SimulationContract.isConfigurationValid(1, 0, Double.NaN))
        assertFalse(SimulationContract.isConfigurationValid(Int.MAX_VALUE, 0, 1.0))
        assertTrue(SimulationContract.isConfigurationValid(1_000, 3_500, 1.0, 0.5))
        listOf(-0.1, 0.01, 0.03, 0.94, 1.0, Double.NaN, Double.POSITIVE_INFINITY).forEach { inner ->
            assertFalse(SimulationContract.isConfigurationValid(1_000, 3_500, 1.0, inner))
        }
    }
}
