#include "formats/SnapshotCodecSupport.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "core/Environment.h"

namespace cellularity::snapshot_codec {

[[noreturn]] void failDecode(DecodeErrorCode code, const std::string& message) {
    throw DecodeError(code, message);
}

void validateSerializedCount(int count, int maximum, const char* name) {
    if (count < 0) {
        failDecode(DecodeErrorCode::InvalidCount,
                std::string("Negative ") + name + " count");
    }
    if (count > maximum) {
        failDecode(DecodeErrorCode::LimitExceeded,
                std::string(name) + " count exceeds the safety limit");
    }
}

void validateSerializedLength(int length, int maximum, const char* name) {
    if (length < 0) {
        failDecode(DecodeErrorCode::InvalidLength,
                std::string("Negative ") + name + " length");
    }
    if (length > maximum) {
        failDecode(DecodeErrorCode::LimitExceeded,
                std::string(name) + " length exceeds the safety limit");
    }
}

void requireFinite(double value, const char* name) {
    if (!std::isfinite(value)) {
        failDecode(DecodeErrorCode::InvalidValue,
                std::string(name) + " must be finite");
    }
}

void applySerializedMutationWeights(
        Environment& environment,
        const std::vector<double>& serializedWeights,
        MutationWeightLayout layout) {
    constexpr std::size_t LEGACY_STANDARD_WEIGHT_COUNT =
            Environment::MUTATION_GENOME_LENGTH_WEIGHT_OFFSET;
    constexpr std::size_t LEGACY_MUTATION_WEIGHT_COUNT =
            LEGACY_STANDARD_WEIGHT_COUNT + 8;
    static_assert(LEGACY_MUTATION_WEIGHT_COUNT == 106);

    if (layout == MutationWeightLayout::Dynamic) {
        const std::size_t copiedCount = std::min(
                serializedWeights.size(), environment.mMutationWeights.size());
        std::copy_n(
                serializedWeights.begin(),
                copiedCount,
                environment.mMutationWeights.begin());
        return;
    }

    // The frozen layout ends with eight retired weights. Dynamic-length
    // mutations did not exist, so those two current weights stay disabled.
    environment.mMutationWeights[Environment::MUTATION_INSERT_GENE_WEIGHT_INDEX] = 0.0;
    environment.mMutationWeights[Environment::MUTATION_DELETE_GENE_WEIGHT_INDEX] = 0.0;

    const std::size_t prefixCount = std::min(
            serializedWeights.size(), LEGACY_STANDARD_WEIGHT_COUNT);
    std::copy_n(
            serializedWeights.begin(),
            prefixCount,
            environment.mMutationWeights.begin());
}

} // namespace cellularity::snapshot_codec
