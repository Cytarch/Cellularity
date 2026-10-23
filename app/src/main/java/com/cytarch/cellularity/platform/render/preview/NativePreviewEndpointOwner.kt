package com.cytarch.cellularity.platform.render.preview

import com.cytarch.cellularity.NativeCellularityLibraryLoader

/** Managed one-way owner for a tagged native preview endpoint wrapper. */
class NativePreviewEndpointOwner private constructor(
    val kind: Kind,
    val generation: Long,
    nativeHandle: Long
) : AutoCloseable {
    enum class Kind(val wireValue: Int) {
        GENOME_PREVIEW(0),
        ENVIRONMENT_FIELD_PREVIEW(1)
    }

    class Borrow internal constructor(
        private var owner: NativePreviewEndpointOwner?,
        val handle: Long,
        val generation: Long
    ) : AutoCloseable {
        override fun close() {
            val current = owner ?: return
            owner = null
            current.releaseBorrow()
        }
    }

    private val ingressLock = Object()
    private var handle = nativeHandle
    private var activeBorrowCount = 0
    private var pendingCloseHandle = 0L

    init {
        require(generation != 0L && nativeHandle != 0L) {
            "Native preview endpoint metadata must be nonzero"
        }
    }

    fun borrow(): Borrow? = synchronized(ingressLock) {
        val current = handle
        if (current == 0L) {
            null
        } else {
            activeBorrowCount += 1
            Borrow(this, current, generation)
        }
    }

    override fun close() {
        val ready = synchronized(ingressLock) {
            val current = handle
            if (current == 0L) return
            handle = 0L
            if (activeBorrowCount == 0) {
                current
            } else {
                pendingCloseHandle = current
                0L
            }
        }
        if (ready != 0L) {
            nativeCloseAndDestroyEndpointHandle(ready)
        }
    }

    private fun releaseBorrow() {
        val ready = synchronized(ingressLock) {
            check(activeBorrowCount > 0) {
                "Native preview endpoint borrow count underflow"
            }
            activeBorrowCount -= 1
            if (activeBorrowCount == 0 && pendingCloseHandle != 0L) {
                pendingCloseHandle.also { pendingCloseHandle = 0L }
            } else {
                0L
            }
        }
        if (ready != 0L) {
            nativeCloseAndDestroyEndpointHandle(ready)
        }
    }

    companion object {
        init {
            NativeCellularityLibraryLoader.requireLoaded()
        }

        @JvmStatic
        fun create(controllerHandle: Long, kind: Kind): NativePreviewEndpointOwner {
            val descriptor = nativeCreateEndpointHandle(
                controllerHandle, kind.wireValue
            )
            check(descriptor != null && descriptor.size == 2 &&
                descriptor[0] != 0L && descriptor[1] != 0L
            ) {
                "Native preview endpoint is unavailable"
            }
            return NativePreviewEndpointOwner(
                kind, descriptor[1], descriptor[0]
            )
        }

        @JvmStatic
        private external fun nativeCreateEndpointHandle(
            controllerHandle: Long,
            kind: Int
        ): LongArray?

        @JvmStatic
        private external fun nativeCloseAndDestroyEndpointHandle(handle: Long)
    }
}
