package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI

class GenomeEditorAngleTest {
    @Test
    fun radiansArePresentedAsDegrees() {
        assertEquals(0.0, angleRadiansToDegrees(0.0), 0.0)
        assertEquals(90.0, angleRadiansToDegrees(PI / 2.0), 1e-12)
        assertEquals(180.0, angleRadiansToDegrees(PI), 1e-12)
        assertEquals(360.0, angleRadiansToDegrees(2.0 * PI), 1e-12)
    }

    @Test
    fun editedDegreesAreStoredAsRadians() {
        assertEquals(0.0, angleDegreesToRadians(0.0), 0.0)
        assertEquals(PI / 2.0, angleDegreesToRadians(90.0), 1e-12)
        assertEquals(PI, angleDegreesToRadians(180.0), 1e-12)
        assertEquals(2.0 * PI, angleDegreesToRadians(360.0), 1e-12)
    }

    @Test
    fun anglePresentationMakesTheUnitExplicit() {
        assertTrue(formatGenomeAngleValue(90.0).endsWith("°"))
        assertEquals("90.000000", formatGenomeAngleInput(90.0))
    }

    @Test
    fun floatBackedFullTurnIsFormattedAsValidDegrees() {
        val storedFullTurn = (2.0 * PI).toFloat().toDouble()

        assertEquals(
            "360.000000",
            formatGenomeAngleInput(angleRadiansToEditorDegrees(storedFullTurn))
        )
    }
}
