#pragma once

#include <stdexcept>
#include <string>
#include <utility>

enum class DecodeErrorCode {
    IoFailure,
    InvalidHeader,
    TruncatedInput,
    InvalidVersion,
    NewerSnapshotVersion,
    NewerProductVersion,
    InvalidLength,
    InvalidCount,
    LimitExceeded,
    TrailingData,
    InvalidValue,
    UnsupportedFeature,
    InvalidCompression,
    OutOfMemory
};

inline const char* decodeErrorCodeName(DecodeErrorCode code) noexcept {
    switch (code) {
        case DecodeErrorCode::IoFailure:
            return "IO_FAILURE";
        case DecodeErrorCode::InvalidHeader:
            return "INVALID_HEADER";
        case DecodeErrorCode::TruncatedInput:
            return "TRUNCATED_INPUT";
        case DecodeErrorCode::InvalidVersion:
            return "INVALID_VERSION";
        case DecodeErrorCode::NewerSnapshotVersion:
            return "NEWER_SNAPSHOT_VERSION";
        case DecodeErrorCode::NewerProductVersion:
            return "NEWER_PRODUCT_VERSION";
        case DecodeErrorCode::InvalidLength:
            return "INVALID_LENGTH";
        case DecodeErrorCode::InvalidCount:
            return "INVALID_COUNT";
        case DecodeErrorCode::LimitExceeded:
            return "LIMIT_EXCEEDED";
        case DecodeErrorCode::TrailingData:
            return "TRAILING_DATA";
        case DecodeErrorCode::InvalidValue:
            return "INVALID_VALUE";
        case DecodeErrorCode::UnsupportedFeature:
            return "UNSUPPORTED_FEATURE";
        case DecodeErrorCode::InvalidCompression:
            return "INVALID_COMPRESSION";
        case DecodeErrorCode::OutOfMemory:
            return "OUT_OF_MEMORY";
    }
    return "INVALID_VALUE";
}

class DecodeError : public std::runtime_error {
public:
    DecodeError(DecodeErrorCode code, std::string message)
            : std::runtime_error(std::move(message)), mCode(code) {
    }

    DecodeErrorCode code() const noexcept {
        return mCode;
    }

private:
    DecodeErrorCode mCode;
};
