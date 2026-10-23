package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.platform.render.preview.GenomePreviewAcknowledgment

/**
 * Keeps a newly attached Vulkan preview covered until its current native state
 * has reached the Android Surface. GLES keeps its established swap path.
 */
internal class GenomePreviewRevealCoordinator(
    private val endpointGeneration: Long,
    private val enabled: Boolean,
    private val maximumRetryCount: Int = DEFAULT_MAXIMUM_RETRY_COUNT
) {
    data class Transition(
        val coverVisible: Boolean,
        val readAcknowledgment: Boolean = false,
        val requestTrackedFrame: Boolean = false
    )

    private var released = false
    private var renderSurfaceAvailable = false
    private var platformContentCurrent = false
    private var targetContentRevision = 0L
    private var targetViewRevision = 0L
    private var targetInteractionToken = 0L
    private var retryCount = 0

    var coverVisible: Boolean = enabled
        private set

    init {
        require(endpointGeneration != 0L) {
            "Genome Preview endpoint generation must be non-zero"
        }
        require(maximumRetryCount > 0) {
            "Genome Preview reveal retry count must be positive"
        }
    }

    fun onSurfaceAvailable(): Transition {
        if (released) return transition()
        renderSurfaceAvailable = true
        platformContentCurrent = false
        retryCount = 0
        if (enabled) coverVisible = true
        return transition(
            requestTrackedFrame = enabled && targetsReady()
        )
    }

    fun onSurfaceChanged(): Transition {
        if (released) return transition()
        platformContentCurrent = false
        retryCount = 0
        if (enabled) coverVisible = true
        return transition()
    }

    fun onSurfaceUnavailable(): Transition {
        if (released) return transition()
        renderSurfaceAvailable = false
        platformContentCurrent = false
        retryCount = 0
        if (enabled) coverVisible = true
        return transition()
    }

    fun onContentTargetAccepted(revision: Long): Transition {
        require(revision != 0L) { "Content revision must be non-zero" }
        targetContentRevision = revision
        return targetChanged()
    }

    fun onViewTargetAccepted(revision: Long): Transition {
        require(revision != 0L) { "View revision must be non-zero" }
        targetViewRevision = revision
        return targetChanged()
    }

    fun onInteractionTargetAccepted(token: Long): Transition {
        require(token != 0L) { "Interaction token must be non-zero" }
        targetInteractionToken = token
        return targetChanged(trackWhileVisible = true)
    }

    fun onPlatformContentCurrent(current: Boolean): Transition {
        if (released || !enabled) return transition()
        platformContentCurrent = current
        if (!current) {
            // A pending interaction frame must not hide an already revealed preview.
            return transition()
        }
        return transition(
            readAcknowledgment = renderSurfaceAvailable && targetsReady()
        )
    }

    fun observeAcknowledgment(
        acknowledgment: GenomePreviewAcknowledgment
    ): Transition {
        if (
            released || !enabled || !renderSurfaceAvailable ||
            !platformContentCurrent || !targetsReady()
        ) {
            return transition()
        }
        val current = acknowledgment.endpointGeneration == endpointGeneration &&
            acknowledgment.attachmentGeneration != 0L &&
            unsignedAtLeast(
                acknowledgment.presentedContentRevision,
                targetContentRevision
            ) &&
            unsignedAtLeast(
                acknowledgment.presentedViewRevision,
                targetViewRevision
            ) &&
            unsignedAtLeast(
                acknowledgment.presentedInteractionToken,
                targetInteractionToken
            )
        if (current) {
            coverVisible = false
            retryCount = 0
            return transition()
        }

        // Surface lifecycle changes restore the cover; stale interaction results
        // only require another present while keeping the current image visible.
        platformContentCurrent = false
        val retry = retryCount < maximumRetryCount
        if (retry) retryCount += 1
        return transition(requestTrackedFrame = retry)
    }

    fun release(): Transition {
        if (released) return transition()
        released = true
        renderSurfaceAvailable = false
        platformContentCurrent = false
        coverVisible = enabled
        return transition()
    }

    private fun targetChanged(trackWhileVisible: Boolean = false): Transition {
        if (released || !enabled || (!coverVisible && !trackWhileVisible)) return transition()
        platformContentCurrent = false
        retryCount = 0
        return transition(
            requestTrackedFrame = renderSurfaceAvailable && targetsReady()
        )
    }

    private fun targetsReady(): Boolean =
        targetContentRevision != 0L && targetViewRevision != 0L

    private fun transition(
        readAcknowledgment: Boolean = false,
        requestTrackedFrame: Boolean = false
    ): Transition = Transition(
        coverVisible = coverVisible,
        readAcknowledgment = readAcknowledgment,
        requestTrackedFrame = requestTrackedFrame
    )

    private fun unsignedAtLeast(value: Long, target: Long): Boolean =
        java.lang.Long.compareUnsigned(value, target) >= 0

    private companion object {
        const val DEFAULT_MAXIMUM_RETRY_COUNT = 8
    }
}
