package com.cytarch.cellularity

import java.util.concurrent.atomic.AtomicBoolean

internal class CrashExceptionHandler(
    private val previousHandler: Thread.UncaughtExceptionHandler?,
    private val reportWriter: (Thread, Throwable) -> Unit,
    private val terminateProcess: (Int) -> Unit
) : Thread.UncaughtExceptionHandler {
    private val handlingException = AtomicBoolean(false)

    override fun uncaughtException(thread: Thread, throwable: Throwable) {
        if (!handlingException.compareAndSet(false, true)) {
            terminateProcess(10)
            return
        }
        try {
            reportWriter(thread, throwable)
        } catch (_: Throwable) {
            // A failing diagnostics write must never re-enter this handler.
        }

        val delegate = previousHandler
        if (delegate != null && delegate !== this) {
            try {
                delegate.uncaughtException(thread, throwable)
                return
            } catch (_: Throwable) {
                // Fall through to deterministic process termination.
            }
        }
        terminateProcess(10)
    }
}
