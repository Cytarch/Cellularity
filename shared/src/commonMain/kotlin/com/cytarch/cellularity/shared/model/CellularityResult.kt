package com.cytarch.cellularity.shared.model

enum class CellularityErrorCode {
    IO_FAILURE,
    INVALID_HEADER,
    TRUNCATED_INPUT,
    INVALID_VERSION,
    NEWER_SNAPSHOT_VERSION,
    NEWER_PRODUCT_VERSION,
    INVALID_LENGTH,
    INVALID_COUNT,
    LIMIT_EXCEEDED,
    TRAILING_DATA,
    INVALID_VALUE,
    UNSUPPORTED_FEATURE,
    INVALID_COMPRESSION,
    OUT_OF_MEMORY,
    ENVIRONMENT_CHANGE_REJECTED,
    SESSION_NOT_READY,
    SESSION_CLOSED,
    INTERNAL_FAILURE
}

data class CellularityError(
    val code: CellularityErrorCode,
    val message: String
)

sealed interface CellularityResult<out T> {
    data class Success<T>(val value: T) : CellularityResult<T>
    data class Failure(val error: CellularityError) : CellularityResult<Nothing>
}
