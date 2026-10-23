#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "io/DecodeError.h"

class ByteReader;
class Environment;

namespace cellularity::snapshot_codec {

enum class MutationWeightLayout {
    Legacy,
    Dynamic,
};

[[noreturn]] void failDecode(DecodeErrorCode code, const std::string& message);
void validateSerializedCount(int count, int maximum, const char* name);
void validateSerializedLength(int length, int maximum, const char* name);
void requireFinite(double value, const char* name);
void applySerializedMutationWeights(
        Environment& environment,
        const std::vector<double>& serializedWeights,
        MutationWeightLayout layout);
} // namespace cellularity::snapshot_codec
