package com.cytarch.cellularity.platform.simulation

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class RetainedSimulationOwnerTest {
    @Test
    fun activityWaiterCancellationDoesNotCreateASecondSession() = runBlocking {
        val ownerScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val owner = RetainedSimulationOwner<CloseableSession>(ownerScope)
        val createStarted = CountDownLatch(1)
        val allowCreate = CountDownLatch(1)
        val createCount = AtomicInteger()
        val session = CloseableSession()
        assertFalse(owner.hasSessionOrOpening())

        val oldActivityWaiter = async(start = CoroutineStart.UNDISPATCHED) {
            owner.getOrCreate {
                createCount.incrementAndGet()
                createStarted.countDown()
                check(allowCreate.await(5, TimeUnit.SECONDS))
                session
            }
        }
        assertTrue(createStarted.await(5, TimeUnit.SECONDS))
        assertTrue(owner.hasSessionOrOpening())
        oldActivityWaiter.cancelAndJoin()

        val recreatedActivityWaiter = async(start = CoroutineStart.UNDISPATCHED) {
            owner.getOrCreate {
                error("A recreated Activity must not create a second session")
            }
        }
        allowCreate.countDown()

        assertSame(session, recreatedActivityWaiter.await())
        assertSame(session, owner.requireCurrent())
        assertEquals(1, createCount.get())

        owner.close()
        owner.close()
        assertEquals(1, session.closeCount)
        assertFalse(owner.hasSessionOrOpening())
        ownerScope.cancel()
    }
}

private class CloseableSession : AutoCloseable {
    var closeCount = 0

    override fun close() {
        closeCount += 1
    }
}
