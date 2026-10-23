package com.cytarch.cellularity.platform.files

import java.io.FilterOutputStream
import java.io.InputStream
import java.io.OutputStream

internal fun requireDeclaredSizeWithinLimit(
    declaredBytes: Long?,
    maximumBytes: Long
) {
    require(maximumBytes >= 0L) { "File size limit cannot be negative" }
    if (declaredBytes != null && declaredBytes >= 0L && declaredBytes > maximumBytes) {
        throw FileSizeLimitExceededException()
    }
}

internal fun copyWithLimit(
    input: InputStream,
    output: OutputStream,
    maximumBytes: Long,
    checkCancellation: () -> Unit = ::throwIfThreadInterrupted
): Long {
    require(maximumBytes >= 0L) { "File size limit cannot be negative" }
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var copied = 0L
    while (true) {
        checkCancellation()
        val count = input.read(buffer)
        if (count < 0) break
        if (count == 0) continue
        if (count.toLong() > maximumBytes - copied) {
            throw FileSizeLimitExceededException()
        }
        output.write(buffer, 0, count)
        copied += count.toLong()
    }
    return copied
}

internal class LimitedOutputStream(
    output: OutputStream,
    private val maximumBytes: Long
) : FilterOutputStream(output) {
    private var written = 0L

    override fun write(value: Int) {
        requireCapacity(1)
        out.write(value)
        written++
    }

    override fun write(bytes: ByteArray, offset: Int, length: Int) {
        requireCapacity(length)
        out.write(bytes, offset, length)
        written += length.toLong()
    }

    private fun requireCapacity(count: Int) {
        if (count < 0 || count.toLong() > maximumBytes - written) {
            throw FileSizeLimitExceededException()
        }
    }
}
