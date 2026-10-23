package com.cytarch.cellularity.shared.ui.play.substrate_info

import kotlin.math.PI
import kotlin.test.Test
import kotlin.test.assertEquals

class SubstrateFieldAnglePresentationTest {
    @Test
    fun environmentAnglesRoundTripBetweenRadiansAndDegrees() {
        assertEquals(90.0, environmentAngleRadiansToDegrees(PI / 2.0), 1e-12)
        assertEquals(PI / 2.0, environmentAngleDegreesToRadians(90.0), 1e-12)
        assertEquals(-180.0, environmentAngleRadiansToDegrees(-PI), 1e-12)
    }

    @Test
    fun floatBackedRangeBoundariesStayInsideEditorRange() {
        assertEquals(
            180.0,
            environmentAngleRadiansToEditorDegrees(PI.toFloat().toDouble()),
            0.0
        )
        assertEquals(
            -180.0,
            environmentAngleRadiansToEditorDegrees((-PI).toFloat().toDouble()),
            0.0
        )
    }
}
