/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

/**
 * Places one cell in the middle of a new plate with the synthesizer, as a person would.
 *
 * A new plate starts empty, exactly as on Android (random seeding is off until the substrate
 * panel turns it on), so a test that needs a living world puts the first cell there itself.
 */
internal fun seedCellAtCenter(session: Long, width: Int, height: Int) {
    DesktopNativeRenderer.nativeSetToolOrdinal(session, SYNTHESIZER_TOOL_ORDINAL)
    check(
        DesktopNativeRenderer.nativeQueueTap(session, width * 0.5f, height * 0.5f) ==
            INPUT_QUEUED_STATUS
    ) { "the synthesizer tap was not accepted" }
}

private const val SYNTHESIZER_TOOL_ORDINAL = 0
private const val INPUT_QUEUED_STATUS = 1
