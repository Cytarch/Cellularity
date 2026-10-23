package com.cytarch.cellularity.platform.render

import android.content.res.AssetManager
import com.cytarch.cellularity.NativeCellularityControllerBridge
import com.cytarch.cellularity.NativeCellularityLibraryLoader

/** Process-local owner of the Java/global AssetManager lifetime used by native rendering. */
class AndroidRenderAssetsOwner(assetManager: AssetManager) : AutoCloseable {
    private val ingressLock = Any()
    private var nativeHandle = nativeCreateLeaseHandle(assetManager)

    init {
        check(nativeHandle != 0L) {
            "Native render assets lease is unavailable"
        }
    }

    fun attachToController(controller: NativeCellularityControllerBridge) {
        synchronized(ingressLock) {
            val handle = nativeHandle
            check(handle != 0L) { "AndroidRenderAssetsOwner is closed" }
            controller.attachRenderAssetsLease(handle)
        }
    }

    fun attachToPreparedController(
        controller: NativeCellularityControllerBridge.PreparedControllerReplacement
    ) {
        synchronized(ingressLock) {
            val handle = nativeHandle
            check(handle != 0L) { "AndroidRenderAssetsOwner is closed" }
            controller.attachRenderAssetsLease(handle)
        }
    }

    fun requireNativeLeaseHandle(): Long = synchronized(ingressLock) {
        nativeHandle.also {
            check(it != 0L) { "AndroidRenderAssetsOwner is closed" }
        }
    }

    override fun close() {
        val handle = synchronized(ingressLock) {
            val current = nativeHandle
            nativeHandle = 0L
            current
        }
        if (handle != 0L) {
            nativeCloseAndDestroyLeaseHandle(handle)
        }
    }

    private companion object {
        init {
            NativeCellularityLibraryLoader.requireLoaded()
        }

        @JvmStatic
        private external fun nativeCreateLeaseHandle(assetManager: AssetManager): Long

        @JvmStatic
        private external fun nativeCloseAndDestroyLeaseHandle(handle: Long)
    }
}
