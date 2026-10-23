#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "verification/SemanticTrace.h"

namespace cellularity::semantic_tests {

struct FixtureResult {
    std::string name;
    verification::SemanticTraceResult trace;
    std::string normalizedStateDigest;
    std::size_t normalizedStateBytes = 0;
};

[[nodiscard]] std::vector<std::string> semanticFixtureNames();
[[nodiscard]] FixtureResult runSemanticFixture(const std::string& name);
void verifySaveLoadTieOrderContract();

} // namespace cellularity::semantic_tests
