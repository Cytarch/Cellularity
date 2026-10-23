#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/Environment.h"

namespace EnvironmentBatch {

constexpr int32_t SCHEMA_VERSION = 1;
constexpr int32_t STATE_MAGIC = 0x43455653; // CEVS
constexpr int32_t PATCH_MAGIC = 0x43455650; // CEVP
constexpr int32_t RESULT_MAGIC = 0x43455652; // CEVR
constexpr int32_t MAX_PATCH_CHANGES = 256;
constexpr size_t MAX_WIRE_BYTES = 64 * 1024;

enum class RejectionReason : int32_t {
    RevisionConflict = 1,
    SchemaMismatch = 2,
    MalformedPatch = 3,
    InvalidChangeKind = 4,
    InvalidProperty = 5,
    InvalidFieldType = 6,
    InvalidFieldComponent = 7,
    NonFiniteValue = 8,
    OutOfRange = 9,
    InvalidIndex = 10,
    InvalidCount = 11,
    EmptyGenePool = 12,
};

struct Rejection {
    int32_t changeIndex = -1;
    RejectionReason reason = RejectionReason::MalformedPatch;
};

struct ApplyResult {
    bool applied = false;
    int64_t revision = 0;
    std::vector<Rejection> rejections;
};

std::vector<uint8_t> writeState(const Environment& environment, int64_t revision);
ApplyResult applyPatch(Environment& environment,
        int64_t currentRevision,
        std::vector<uint8_t> patchBytes);
std::vector<uint8_t> writeApplyResult(const ApplyResult& result);

} // namespace EnvironmentBatch
