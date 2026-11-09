/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import kotlin.test.Test
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * That the launcher can ask what a world costs before starting it.
 *
 * The freezer asks this question for every plate a person might start, and the answer is
 * what enables or disables the start button. Until the bridge could answer, the launcher
 * passed a hardcoded "no estimate", which left every plate unstartable from the freezer --
 * a screen that lists experiments and refuses to open any of them. That is why this is
 * checked against the real library rather than a stand-in: the part that was missing is
 * precisely the part a stand-in would supply.
 *
 * Two answers are asserted, and they are different questions. A world that fits has a size,
 * which is what the person is shown. A world that does not fit is refused, and must be
 * refused as "no estimate" rather than as zero, because zero would read as "this costs
 * nothing" and would enable a start button that leads to a runtime rejection.
 */
class DesktopWorldAllocationEstimateTest {

    @Test
    fun `a plate that fits has a size`() {
        val bytes = DesktopNativeRenderer.estimateWorldAllocationBytes(
            maxCellCount = 2_000,
            maxFoodCount = 20_000,
            radius = 1_000.0
        )

        assertNotNull(
            bytes,
            "a modest plate must have an estimate; without one the freezer cannot start it"
        )
        assertTrue(
            bytes > 0L,
            "an estimate of $bytes bytes is not a size, and the screen would show it as free"
        )
    }

    @Test
    fun `a plate that cannot fit is refused rather than reported as free`() {
        // Deliberately beyond any budget: the point is the refusal, and a configuration
        // small enough to fit anywhere would not test it. Integer overflow inside the
        // estimate is a refusal too, which is the other half of what this checks.
        val bytes = DesktopNativeRenderer.estimateWorldAllocationBytes(
            maxCellCount = Int.MAX_VALUE,
            maxFoodCount = Int.MAX_VALUE,
            radius = Double.MAX_VALUE
        )

        assertNull(
            bytes,
            "a world the runtime would reject must not be offered as costing $bytes bytes"
        )
    }

    @Test
    fun `the estimate grows with the world`() {
        val small = DesktopNativeRenderer.estimateWorldAllocationBytes(1_000, 10_000, 1_000.0)
        val large = DesktopNativeRenderer.estimateWorldAllocationBytes(2_000, 20_000, 1_000.0)

        assertNotNull(small, "the smaller plate must have an estimate")
        assertNotNull(large, "the larger plate must have an estimate")
        assertTrue(
            large > small,
            "a larger world cannot cost less: $small then $large"
        )
    }
}
