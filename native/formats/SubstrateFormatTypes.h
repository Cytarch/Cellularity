#pragma once

#include "core/Environment.h"
#include "io/DecodeError.h"
#include <cstdint>

enum class NativeSubstrateFormatFamily : std::int32_t {
    Unknown = 0,
    OriginalJava = 1,
    ReleaseClty = 2,
};

struct NativeSubstrateInspection {
    /** Family-local version; never compare product versions with CLTY schemas. */
    int familyLocalVersion = 0;
    double time = 0.0;
    int cellCount = 0;
    int foodCount = -1;
    double foodMass = -1.0;
    int knownOrganismCount = -1;
    bool organismIdentityComplete = false;
    std::int64_t createdAtUnixMillis = 0;
    Environment environment;
    std::uint16_t previewCodec = 0;
    std::uint32_t previewPixelWidth = 0;
    std::uint32_t previewPixelHeight = 0;
    std::uint64_t previewBytes = 0;
    std::uint32_t previewCrc32 = 0;
    bool previewPotentiallyUsable = false;
    NativeSubstrateFormatFamily formatFamily =
            NativeSubstrateFormatFamily::Unknown;
    std::uint32_t contentSchemaMajor = 0;
    std::uint32_t contentSchemaMinor = 0;
    bool loadSupported = true;
};

using NativeSnapshotDecodeErrorCode = DecodeErrorCode;
using NativeSnapshotDecodeError = DecodeError;
