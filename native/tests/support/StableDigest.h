#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cellularity::test_support {

std::string stableDigest(const std::vector<uint8_t>& bytes);

} // namespace cellularity::test_support
