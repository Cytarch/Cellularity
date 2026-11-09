/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import com.cytarch.cellularity.shared.files.genome.ReleaseGenomeCodecBridge
import com.cytarch.cellularity.shared.files.storage.CellularityNativeFiles
import com.cytarch.cellularity.shared.model.SimulationConfiguration

/** The desktop's JNI route to the shared file store's native operations; Android has the twin. */
object DesktopNativeFiles : CellularityNativeFiles {
    private const val OPERATIONAL_SUBSTRATE_LIMIT_INDEX = 1

    override val releaseGenomeCodec: ReleaseGenomeCodecBridge = DesktopReleaseGenomeCodecBridge

    override fun validateGenomeDraft(genomeDraftWire: ByteArray) {
        DesktopNativeRenderer.requireLoaded()
        DesktopNativeRenderer.nativeValidateGenomeDraft(genomeDraftWire)
    }

    override fun operationalSubstrateFileLimitBytes(): Long {
        DesktopNativeRenderer.requireLoaded()
        val limits = DesktopNativeRenderer.nativeRuntimeLimits()
        check(limits.size == 4) { "Native runtime limits are unavailable" }
        return limits[OPERATIONAL_SUBSTRATE_LIMIT_INDEX]
    }

    override fun inspectSubstratePath(
        path: String,
        previewMode: Boolean,
        fallback: SimulationConfiguration
    ): ByteArray {
        DesktopNativeRenderer.requireLoaded()
        return DesktopNativeRenderer.nativeInspectSubstratePath(
            path, previewMode, fallback.maxCellCount, fallback.maxFoodCount, fallback.radius
        )
    }

    override fun validateSubstratePath(
        path: String,
        previewMode: Boolean,
        fallback: SimulationConfiguration
    ) {
        DesktopNativeRenderer.requireLoaded()
        DesktopNativeRenderer.nativeValidateSubstratePath(
            path, previewMode, fallback.maxCellCount, fallback.maxFoodCount, fallback.radius
        )
    }

    override fun readSubstratePreviewPath(path: String): ByteArray {
        DesktopNativeRenderer.requireLoaded()
        return DesktopNativeRenderer.nativeReadSubstratePreviewPath(path)
    }
}
