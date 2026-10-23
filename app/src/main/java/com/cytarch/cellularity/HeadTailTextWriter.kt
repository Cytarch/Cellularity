package com.cytarch.cellularity

import java.io.Writer

/**
 * Keeps the beginning and the end of a large text without allowing its in-memory
 * representation to grow without bound.
 */
internal class HeadTailTextWriter(
    private val characterLimit: Int,
    private val truncationMarker: String = DEFAULT_TRUNCATION_MARKER
) : Writer() {
    private val headLimit: Int
    private val tailLimit: Int
    private val head: StringBuilder
    private var tail: CharArray? = null
    private var tailStart = 0
    private var tailSize = 0
    private var totalCharacters = 0L

    init {
        require(characterLimit >= 2) { "Character limit must leave room for head and tail" }
        headLimit = characterLimit / 2
        tailLimit = characterLimit - headLimit
        head = StringBuilder(minOf(headLimit, INITIAL_CAPACITY))
    }

    override fun write(characters: CharArray, offset: Int, length: Int) {
        requireValidRange(characters.size, offset, length)
        if (length == 0) return

        totalCharacters += length.toLong()
        var sourceOffset = offset
        var remaining = length

        val headCharacters = minOf(remaining, headLimit - head.length)
        if (headCharacters > 0) {
            head.append(characters, sourceOffset, headCharacters)
            sourceOffset += headCharacters
            remaining -= headCharacters
        }

        repeat(remaining) {
            appendToTail(characters[sourceOffset++])
        }
    }

    override fun flush() = Unit

    override fun close() = Unit

    fun result(): String {
        val wasTruncated = totalCharacters > characterLimit.toLong()
        val capacity = head.length + tailSize + if (wasTruncated) truncationMarker.length else 0
        return buildString(capacity) {
            append(head)
            if (wasTruncated) append(truncationMarker)
            appendTailTo(this)
        }
    }

    private fun appendToTail(character: Char) {
        val tailCharacters = tail ?: CharArray(tailLimit).also { tail = it }
        if (tailSize < tailLimit) {
            tailCharacters[(tailStart + tailSize) % tailLimit] = character
            tailSize++
            return
        }
        tailCharacters[tailStart] = character
        tailStart = (tailStart + 1) % tailLimit
    }

    private fun appendTailTo(destination: StringBuilder) {
        val tailCharacters = tail ?: return
        repeat(tailSize) { index ->
            destination.append(tailCharacters[(tailStart + index) % tailLimit])
        }
    }

    private fun requireValidRange(arraySize: Int, offset: Int, length: Int) {
        if (offset < 0 || length < 0 || offset > arraySize - length) {
            throw IndexOutOfBoundsException(
                "Invalid write range: size=$arraySize, offset=$offset, length=$length"
            )
        }
    }

    private companion object {
        const val INITIAL_CAPACITY = 4 * 1024
        const val DEFAULT_TRUNCATION_MARKER =
            "\n[stack trace middle truncated; beginning and deepest cause preserved]\n"
    }
}
