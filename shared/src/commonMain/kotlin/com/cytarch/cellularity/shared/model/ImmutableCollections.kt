package com.cytarch.cellularity.shared.model

/** Creates a read-only snapshot that cannot expose the caller's mutable list. */
internal fun <T> immutableListCopy(source: Iterable<T>): List<T> =
    ImmutableList(source.toList())

private class ImmutableList<T>(
    private val elements: List<T>
) : AbstractList<T>() {
    override val size: Int
        get() = elements.size

    override fun get(index: Int): T = elements[index]
}
