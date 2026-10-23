package com.cytarch.cellularity.shared.ui.state

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow

/**
 * Minimal feature boundary proven by Settings and Freezer.
 *
 * [effects] is a single-consumer stream: each emitted value is handled by one collector and is
 * never replayed as state. Implementations own their delivery buffer; callers own coroutine scope.
 */
interface Store<out State, in Action, out Effect> {
    val state: StateFlow<State>
    val effects: Flow<Effect>

    suspend fun dispatch(action: Action)
}
