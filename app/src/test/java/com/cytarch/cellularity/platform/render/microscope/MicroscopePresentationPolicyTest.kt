package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class MicroscopePresentationPolicyTest {
    @Test
    fun hiddenTabDisablesEveryRecurringRenderCadenceWithoutChangingTemperature() {
        MicroscopeTemperature.entries.forEach { temperature ->
            val plan = MicroscopePresentationPolicy.plan(
                temperature = temperature,
                presentationVisible = false,
                publicationIntervalMillis = TEST_PUBLICATION_INTERVAL_MILLIS
            )

            assertEquals(MicroscopeRenderCadence.ON_DEMAND, plan.cadence)
            assertFalse(plan.requestInitialFrame)
            assertNull(plan.frameIntervalMillis)
            assertEquals(
                MicroscopePresentationPolicy.NO_RENDER_STATUS_INTERVAL_MILLIS,
                plan.statusPollIntervalMillis
            )
        }
    }

    @Test
    fun visibleIncubateRequestsACompleteFrameAndThenUsesStaticCadence() {
        val plan = MicroscopePresentationPolicy.plan(
            temperature = MicroscopeTemperature.INCUBATE,
            presentationVisible = true,
            publicationIntervalMillis = TEST_PUBLICATION_INTERVAL_MILLIS
        )

        assertEquals(MicroscopeRenderCadence.ON_DEMAND, plan.cadence)
        assertTrue(plan.requestInitialFrame)
        assertEquals(
            MicroscopePresentationPolicy.INCUBATE_FRAME_INTERVAL_MILLIS,
            plan.frameIntervalMillis
        )
        assertEquals(TEST_PUBLICATION_INTERVAL_MILLIS, plan.statusPollIntervalMillis)
    }

    @Test
    fun visibleNoRenderRequestsOneClearAndOnlyPollsStatusAfterward() {
        val plan = MicroscopePresentationPolicy.plan(
            temperature = MicroscopeTemperature.INCUBATE_NO_RENDER,
            presentationVisible = true,
            publicationIntervalMillis = TEST_PUBLICATION_INTERVAL_MILLIS
        )

        assertEquals(MicroscopeRenderCadence.ON_DEMAND, plan.cadence)
        assertTrue(plan.requestInitialFrame)
        assertNull(plan.frameIntervalMillis)
        assertEquals(
            MicroscopePresentationPolicy.NO_RENDER_STATUS_INTERVAL_MILLIS,
            plan.statusPollIntervalMillis
        )
    }

    @Test
    fun visibleInteractiveModesKeepContinuousPresentation() {
        val interactiveModes = MicroscopeTemperature.entries.filter {
            it != MicroscopeTemperature.INCUBATE &&
                it != MicroscopeTemperature.INCUBATE_NO_RENDER
        }

        interactiveModes.forEach { temperature ->
            val plan = MicroscopePresentationPolicy.plan(
                temperature = temperature,
                presentationVisible = true,
                publicationIntervalMillis = TEST_PUBLICATION_INTERVAL_MILLIS
            )

            assertEquals(MicroscopeRenderCadence.CONTINUOUS, plan.cadence)
            assertFalse(plan.requestInitialFrame)
            assertNull(plan.frameIntervalMillis)
            assertEquals(TEST_PUBLICATION_INTERVAL_MILLIS, plan.statusPollIntervalMillis)
        }
    }

    private companion object {
        const val TEST_PUBLICATION_INTERVAL_MILLIS = 47L
    }
}
