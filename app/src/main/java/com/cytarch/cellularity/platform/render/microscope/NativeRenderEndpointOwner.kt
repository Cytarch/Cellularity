package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.NativeCellularityLibraryLoader

/** One-way managed owner for the durable native microscope render endpoint. */
internal class NativeRenderEndpointOwner private constructor(
    nativeHandle: Long
) : AutoCloseable {
    class Borrow internal constructor(
        private var owner: NativeRenderEndpointOwner?,
        val handle: Long
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
        require(nativeHandle != 0L) {
            "Native render endpoint handle must be nonzero"
        }
    }

    fun borrow(): Borrow? = synchronized(ingressLock) {
        val current = handle
        if (current == 0L) {
            null
        } else {
            activeBorrowCount += 1
            Borrow(this, current)
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
        if (ready != 0L) nativeCloseAndDestroyEndpointHandle(ready)
    }

    private fun releaseBorrow() {
        val ready = synchronized(ingressLock) {
            check(activeBorrowCount > 0) {
                "Native render endpoint borrow count underflow"
            }
            activeBorrowCount -= 1
            if (activeBorrowCount == 0 && pendingCloseHandle != 0L) {
                pendingCloseHandle.also { pendingCloseHandle = 0L }
            } else {
                0L
            }
        }
        if (ready != 0L) nativeCloseAndDestroyEndpointHandle(ready)
    }

    companion object {
        init {
            NativeCellularityLibraryLoader.requireLoaded()
        }

        @JvmStatic
        fun create(controllerHandle: Long): NativeRenderEndpointOwner {
            val endpointHandle = nativeCreateEndpointHandle(controllerHandle)
            check(endpointHandle != 0L) {
                "Native microscope render endpoint is unavailable"
            }
            return NativeRenderEndpointOwner(endpointHandle)
        }

        @JvmStatic
        private external fun nativeCreateEndpointHandle(controllerHandle: Long): Long

        @JvmStatic
        private external fun nativeCloseAndDestroyEndpointHandle(handle: Long)
    }
}
