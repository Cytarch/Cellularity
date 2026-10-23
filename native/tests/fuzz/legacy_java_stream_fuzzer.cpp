#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "formats/LegacySubstrateCodec.h"
#include "io/DecodeError.h"
#include "io/JavaObjectStreamReader.h"

namespace {

constexpr size_t MAX_FUZZ_INPUT_BYTES = 64 * 1024;
constexpr size_t MAX_TOP_LEVEL_OBJECTS = 4096;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > MAX_FUZZ_INPUT_BYTES) {
        return 0;
    }
    try {
        std::vector<uint8_t> bytes(data, data + size);
        JavaObjectStreamReader reader(bytes);
        size_t objectCount = 0;
        while (reader.hasRemainingBytes() && objectCount < MAX_TOP_LEVEL_OBJECTS) {
            (void)reader.readObject();
            objectCount++;
        }
        if (reader.hasRemainingBytes()) {
            throw DecodeError(DecodeErrorCode::LimitExceeded,
                    "Java serialization top-level object count exceeds the safety limit");
        }
    } catch (const DecodeError&) {
        // Expected rejection path. Continue with the legacy substrate grammar below.
    } catch (...) {
        std::abort();
    }

    try {
        std::vector<uint8_t> bytes(data, data + size);
        Environment fallback = Environment::constructDefaultEnvironment(8, 8, 1.0);
        (void)cellularity::snapshot_codec::loadLegacySubstrateBytesDirect(
                std::move(bytes), false, fallback);
    } catch (const DecodeError&) {
        return 0;
    } catch (...) {
        std::abort();
    }
    return 0;
}
