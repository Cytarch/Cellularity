package com.cytarch.cellularity.platform.simulation

import java.util.concurrent.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async

/**
 * Retains exactly one closeable session across changing UI owners.
 * Cancelling an Activity waiter does not cancel creation owned by [scope].
 */
internal class RetainedSimulationOwner<T : AutoCloseable>(
    private val scope: CoroutineScope
) : AutoCloseable {
    private val lock = Any()
    private var closed = false
    private var current: T? = null
    private var opening: Deferred<T>? = null

    suspend fun getOrCreate(factory: suspend () -> T): T {
        val deferred = synchronized(lock) {
            check(!closed) { "Simulation owner is closed" }
            current?.let { return it }
            opening ?: createDeferred(factory).also { opening = it }
        }
        deferred.start()
        return deferred.await()
    }

    fun requireCurrent(): T = synchronized(lock) {
        checkNotNull(current) { "Simulation has not been created" }
    }

    fun hasSessionOrOpening(): Boolean = synchronized(lock) {
        current != null || opening != null
    }

    override fun close() {
        val (owned, pending) = synchronized(lock) {
            if (closed) return
            closed = true
            val values = current to opening
            current = null
            opening = null
            values
        }
        pending?.cancel(CancellationException("Simulation owner was cleared"))
        runCatching { owned?.close() }
    }

    private fun createDeferred(factory: suspend () -> T): Deferred<T> {
        lateinit var deferred: Deferred<T>
        deferred = scope.async(start = CoroutineStart.LAZY) {
            val candidate = factory()
            val accepted = synchronized(lock) {
                if (closed) {
                    false
                } else {
                    current = candidate
                    true
                }
            }
            if (!accepted) {
                runCatching(candidate::close)
                throw CancellationException("Simulation owner was cleared during creation")
            }
            candidate
        }
        deferred.invokeOnCompletion {
            synchronized(lock) {
                if (opening === deferred) opening = null
            }
        }
        return deferred
    }
}
