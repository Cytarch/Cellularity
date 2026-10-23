#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/SimulationWorld.h"
#include "formats/release/ReleaseSubstrateCodec.h"
#include "io/DecodeError.h"

namespace {

constexpr std::size_t kMaximumFuzzInputBytes = 64U * 1024U;

bool hasReleaseMagic(const std::uint8_t* data, std::size_t size) {
    return size >= 4U
            && data[0] == 'C'
            && data[1] == 'L'
            && data[2] == 'T'
            && data[3] == 'Y';
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(
        const std::uint8_t* data,
        std::size_t size) {
    if (size > kMaximumFuzzInputBytes || !hasReleaseMagic(data, size)) {
        return 0;
    }
    try {
        const std::vector<std::uint8_t> bytes(data, data + size);
        (void)cellularity::save_format::release::decodeReleaseSubstrate(
                bytes, false);
    } catch (const DecodeError&) {
        return 0;
    } catch (...) {
        std::abort();
    }
    return 0;
}
