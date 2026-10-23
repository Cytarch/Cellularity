#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/SimulationMemoryPolicy.h"
#include "formats/DecodedWorldValidation.h"
#include "formats/FormatLimits.h"
#include "formats/LegacyGeneCodec.h"
#include "formats/LegacySubstrateCodec.h"
#include "formats/NativeSubstrateLoader.h"
#include "formats/SnapshotFileIO.h"
#include "formats/SubstrateFileCodec.h"
#include "formats/release/ReleaseGenomeCodec.h"
#include "formats/release/ReleaseSubstrateCodec.h"
#include "io/DecodeError.h"
#include "io/JavaObjectStreamReader.h"
#include "tests/fixtures/FormatFixtureCorpus.h"

namespace {

const std::filesystem::path CORPUS_DIRECTORY = CELLULARITY_FIXTURE_CORPUS_DIR;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void assertTrue(bool condition, const std::string& context) {
    if (!condition) {
        fail(context);
    }
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        fail("Cannot open fixture " + path.string());
    }
    const std::streamoff length = input.tellg();
    if (length < 0) {
        fail("Cannot determine fixture size " + path.string());
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        fail("Cannot read fixture " + path.string());
    }
    return bytes;
}

std::vector<uint8_t> fixtureBytes(const std::string& id) {
    return readFile(CORPUS_DIRECTORY / (id + ".bin"));
}

Environment fallbackEnvironment() {
    return Environment::constructDefaultEnvironment(8, 8, 1.0);
}

template <typename Function>
DecodeErrorCode expectTypedFailure(Function&& function, const std::string& context) {
    try {
        function();
    } catch (const DecodeError& error) {
        return error.code();
    } catch (const std::exception& error) {
        fail(context + ": untyped std::exception: " + error.what());
    } catch (...) {
        fail(context + ": non-standard C++ exception");
    }
    fail(context + ": malformed input was accepted");
}

template <typename Function>
void expectCode(Function&& function,
        DecodeErrorCode expected,
        const std::string& context) {
    const DecodeErrorCode actual = expectTypedFailure(
            std::forward<Function>(function), context);
    if (actual != expected) {
        fail(context + ": expected " + decodeErrorCodeName(expected)
                + ", got " + decodeErrorCodeName(actual));
    }
}

void decodeSubstrate(std::vector<uint8_t> bytes) {
    const bool isRelease = bytes.size() >= 4
            && bytes[0] == 'C' && bytes[1] == 'L'
            && bytes[2] == 'T' && bytes[3] == 'Y';
    if (isRelease) {
        (void)cellularity::save_format::release::decodeReleaseSubstrate(
                bytes, false);
        return;
    }
    (void)cellularity::snapshot_codec::loadLegacySubstrateBytesDirect(
            std::move(bytes), false, fallbackEnvironment());
}

void decodeFixture(const cellularity::test_fixtures::FixtureSpec& spec,
        std::vector<uint8_t> bytes) {
    if (spec.family == "legacy-genome") {
        (void)cellularity::snapshot_codec::decodeLegacyGenomeBytes(
                std::move(bytes));
    } else if (spec.family == "legacy-substrate") {
        decodeSubstrate(std::move(bytes));
    } else if (spec.family == "java-serialization") {
        JavaObjectStreamReader reader(std::move(bytes));
        (void)reader.readObject();
    } else {
        fail("Unsupported malformed fixture family " + spec.family);
    }
}

void testRejectedCorpusUsesTypedFailures() {
    size_t tested = 0;
    for (const auto& spec : cellularity::test_fixtures::formatFixtureSpecs()) {
        if (spec.expected != "reject"
                || (spec.family != "legacy-genome"
                        && spec.family != "legacy-substrate"
                        && spec.family != "java-serialization")) {
            continue;
        }
        std::vector<uint8_t> bytes = fixtureBytes(spec.id);
        expectTypedFailure([&] {
            decodeFixture(spec, bytes);
        },
                "rejected corpus " + spec.id);
        tested++;
    }
    assertTrue(tested >= 12, "Too few rejected corpus fixtures were exercised");
}

std::vector<size_t> truncationOffsets(size_t size) {
    std::vector<size_t> offsets;
    if (size == 0) {
        return offsets;
    }
    const size_t step = std::max<size_t>(1, size / 128);
    for (size_t offset = 0; offset < size; offset += step) {
        offsets.push_back(offset);
    }
    if (offsets.back() != size - 1) {
        offsets.push_back(size - 1);
    }
    return offsets;
}

void testTruncatedHeadersAndBodies() {
    const std::array<std::string, 2> ids{
            "legacy-genome-v103",
            "legacy-substrate-v103"
    };
    const auto& specs = cellularity::test_fixtures::formatFixtureSpecs();
    for (const std::string& id : ids) {
        const auto spec = std::find_if(specs.begin(), specs.end(), [&](const auto& candidate) {
            return candidate.id == id;
        });
        if (spec == specs.end()) {
            fail("Missing truncation fixture " + id);
        }
        const std::vector<uint8_t> complete = fixtureBytes(id);
        for (size_t length : truncationOffsets(complete.size())) {
            std::vector<uint8_t> truncated(complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(length));
            expectTypedFailure([&] {
                decodeFixture(*spec, truncated);
            },
                    id + " truncated at " + std::to_string(length));
        }
    }

    const auto legacyGenome =
            cellularity::snapshot_codec::decodeLegacyGenomeBytes(
                    fixtureBytes("legacy-genome-v103"));
    const cellularity::GenomeData releaseGenome(
            legacyGenome.begin(), legacyGenome.end());
    const std::vector<std::uint8_t> releaseGenomeBytes =
            cellularity::save_format::release::encodeReleaseGenome(
                    releaseGenome);
    for (size_t length : truncationOffsets(releaseGenomeBytes.size())) {
        const std::vector<std::uint8_t> truncated(
                releaseGenomeBytes.begin(),
                releaseGenomeBytes.begin()
                        + static_cast<std::ptrdiff_t>(length));
        expectTypedFailure([&] {
            (void)cellularity::save_format::release::decodeReleaseGenome(
                    truncated);
        },
                "release Genome truncated at " + std::to_string(length));
    }

    Environment environment =
            Environment::constructDefaultEnvironment(4, 4, 1.0);
    SimulationWorld world(false, environment);
    const std::vector<std::uint8_t> releaseSubstrateBytes =
            cellularity::save_format::release::encodeReleaseSubstrate(world)
                    .bytes;
    for (size_t length : truncationOffsets(releaseSubstrateBytes.size())) {
        const std::vector<std::uint8_t> truncated(
                releaseSubstrateBytes.begin(),
                releaseSubstrateBytes.begin()
                        + static_cast<std::ptrdiff_t>(length));
        expectTypedFailure([&] {
            (void)cellularity::save_format::release::decodeReleaseSubstrate(
                    truncated, false);
        },
                "release substrate truncated at " + std::to_string(length));
    }

    const std::vector<uint8_t> objectGraph = fixtureBytes("jos-object-graph");
    for (size_t length : truncationOffsets(objectGraph.size())) {
        std::vector<uint8_t> truncated(objectGraph.begin(), objectGraph.begin() + static_cast<std::ptrdiff_t>(length));
        expectTypedFailure([&] {
            JavaObjectStreamReader reader(truncated);
            (void)reader.readObject();
        },
                "Java object graph truncated at " + std::to_string(length));
    }
}

void testEnvironmentAllocationRejectsOverflow() {
    Environment excessive =
            Environment::constructDefaultEnvironment(4, 4, 1.0);
    excessive.maxCellCount = std::numeric_limits<std::int32_t>::max();
    expectCode([&] {
        cellularity::snapshot_codec::validateEnvironmentForAllocation(
                excessive);
    },
            DecodeErrorCode::LimitExceeded, "overflow-safe Environment allocation estimate");
}

void appendU2(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void appendU4(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void appendU8(std::vector<uint8_t>& bytes, uint64_t value) {
    appendU4(bytes, static_cast<uint32_t>(value >> 32));
    appendU4(bytes, static_cast<uint32_t>(value));
}

std::vector<uint8_t> javaHeader() {
    return {0xac, 0xed, 0x00, 0x05};
}

void testJavaSerializationLimits() {
    std::vector<uint8_t> handles = javaHeader();
    for (size_t i = 0; i <= JavaObjectStreamReader::MAX_HANDLE_COUNT; i++) {
        handles.push_back(0x74); // TC_STRING
        appendU2(handles, 0);
    }
    expectCode([&] {
        JavaObjectStreamReader reader(handles);
        while (reader.hasRemainingBytes()) {
            (void)reader.readObject();
        }
    },
            DecodeErrorCode::LimitExceeded, "Java serialization handle limit");

    std::vector<uint8_t> nesting = javaHeader();
    nesting.insert(nesting.end(), JavaObjectStreamReader::MAX_NESTING_DEPTH + 1, 0x79);
    nesting.push_back(0x70);
    expectCode([&] {
        JavaObjectStreamReader reader(nesting);
        (void)reader.readObject();
    },
            DecodeErrorCode::LimitExceeded, "Java serialization nesting limit");

    std::vector<uint8_t> longString = javaHeader();
    longString.push_back(0x7c); // TC_LONGSTRING
    appendU8(longString, JavaObjectStreamReader::MAX_STRING_BYTES + 1);
    expectCode([&] {
        JavaObjectStreamReader reader(longString);
        (void)reader.readObject();
    },
            DecodeErrorCode::LimitExceeded, "Java serialization string limit");

    std::vector<uint8_t> primitiveArray = javaHeader();
    primitiveArray.push_back(0x75); // TC_ARRAY
    primitiveArray.push_back(0x72); // TC_CLASSDESC
    appendU2(primitiveArray, 2);
    primitiveArray.push_back('[');
    primitiveArray.push_back('I');
    appendU8(primitiveArray, 0);
    primitiveArray.push_back(0x02); // SC_SERIALIZABLE
    appendU2(primitiveArray, 0);
    primitiveArray.push_back(0x78); // TC_ENDBLOCKDATA
    primitiveArray.push_back(0x70); // null superclass
    appendU4(primitiveArray, JavaObjectStreamReader::MAX_ARRAY_ENTRIES + 1);
    expectCode([&] {
        JavaObjectStreamReader reader(primitiveArray);
        (void)reader.readObject();
    },
            DecodeErrorCode::LimitExceeded, "Java serialization array limit");

    std::vector<uint8_t> fields = javaHeader();
    fields.push_back(0x72);
    appendU2(fields, 1);
    fields.push_back('V');
    appendU8(fields, 0);
    fields.push_back(0x02);
    appendU2(fields, JavaObjectStreamReader::MAX_FIELD_COUNT + 1);
    expectCode([&] {
        JavaObjectStreamReader reader(fields);
        (void)reader.readObject();
    },
            DecodeErrorCode::LimitExceeded, "Java serialization field limit");

    std::vector<uint8_t> block = javaHeader();
    block.push_back(0x7a); // TC_BLOCKDATALONG
    appendU4(block, JavaObjectStreamReader::MAX_BLOCK_DATA_BYTES + 1);
    expectCode([&] {
        JavaObjectStreamReader reader(block);
        (void)reader.readInt();
    },
            DecodeErrorCode::LimitExceeded, "Java serialization block-data limit");

    std::vector<uint8_t> annotations = javaHeader();
    annotations.push_back(0x72);
    appendU2(annotations, 1);
    annotations.push_back('V');
    appendU8(annotations, 0);
    annotations.push_back(0x02);
    appendU2(annotations, 0);
    for (size_t i = 0; i <= JavaObjectStreamReader::MAX_ANNOTATION_ENTRIES; i++) {
        annotations.push_back(0x77);
        annotations.push_back(0);
    }
    annotations.push_back(0x78);
    annotations.push_back(0x70);
    expectCode([&] {
        JavaObjectStreamReader reader(annotations);
        (void)reader.readObject();
    },
            DecodeErrorCode::LimitExceeded, "Java serialization annotation entry limit");

    std::vector<uint8_t> annotationBytes = javaHeader();
    annotationBytes.push_back(0x72); // TC_CLASSDESC
    appendU2(annotationBytes, 1);
    annotationBytes.push_back('V');
    appendU8(annotationBytes, 0);
    annotationBytes.push_back(0x02); // SC_SERIALIZABLE
    appendU2(annotationBytes, 0);
    annotationBytes.push_back(0x7a); // TC_BLOCKDATALONG
    appendU4(annotationBytes, JavaObjectStreamReader::MAX_ANNOTATION_BYTES + 1);
    expectCode([&] {
        JavaObjectStreamReader reader(annotationBytes);
        (void)reader.readObject();
    },
            DecodeErrorCode::LimitExceeded, "Java serialization annotation byte limit");
}

std::vector<uint8_t> gzip(const std::vector<uint8_t>& input) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, 16 + MAX_WBITS,
                8, Z_DEFAULT_STRATEGY)
            != Z_OK) {
        fail("Could not initialize gzip test encoder");
    }

    struct DeflateEndGuard {
        z_stream* stream;

        ~DeflateEndGuard() {
            deflateEnd(stream);
        }
    } guard{&stream};

    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    std::vector<uint8_t> output;
    std::array<uint8_t, 256> buffer{};
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = deflate(&stream, Z_FINISH);
        if (status != Z_OK && status != Z_STREAM_END) {
            fail("Could not encode gzip test data");
        }
        output.insert(output.end(), buffer.begin(),
                buffer.begin() + static_cast<std::ptrdiff_t>(buffer.size() - stream.avail_out));
    }
    return output;
}

void testGzipLimitsAndCorruption() {
    const std::vector<uint8_t> plain(1025, 0x42);
    const std::vector<uint8_t> compressed = gzip(plain);
    expectCode([&] {
        (void)inflateGzipBytes(compressed, 1024);
    },
            DecodeErrorCode::LimitExceeded, "gzip inflated output limit");

    std::vector<uint8_t> truncated = compressed;
    truncated.pop_back();
    expectCode([&] {
        (void)inflateGzipBytes(truncated, 2048);
    },
            DecodeErrorCode::InvalidCompression, "truncated gzip stream");

    std::vector<uint8_t> trailing = compressed;
    trailing.push_back(0x00);
    expectCode([&] {
        (void)inflateGzipBytes(trailing, 2048);
    },
            DecodeErrorCode::TrailingData, "trailing gzip data");

    expectCode([&] {
        (void)inflateGzipBytes({0x00, 0x01, 0x02}, 2048);
    },
            DecodeErrorCode::InvalidCompression, "invalid gzip stream");

    std::string compressedText(
            reinterpret_cast<const char*>(compressed.data()), compressed.size());
    std::istringstream exactInput(compressedText, std::ios::binary);
    std::ostringstream exactOutput(std::ios::binary);
    assertTrue(inflateGzipStream(exactInput, exactOutput, plain.size()) == plain.size(),
            "streaming gzip exact-limit arithmetic was rejected");
    assertTrue(exactOutput.str().size() == plain.size(),
            "streaming gzip output size changed");

    std::istringstream limitedInput(compressedText, std::ios::binary);
    std::ostringstream limitedOutput(std::ios::binary);
    expectCode([&] {
        (void)inflateGzipStream(limitedInput, limitedOutput, plain.size() - 1);
    },
            DecodeErrorCode::LimitExceeded, "streaming gzip expansion limit");
}

void testRuntimeFileAndAllocationPolicy() {
    using namespace cellularity;
    constexpr std::uint64_t twoGib = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    const memory::RuntimeLimits limits64 = memory::runtimeLimitsForAddressBits(64);
    const memory::RuntimeLimits limits32 = memory::runtimeLimitsForAddressBits(32);

    assertTrue(limits64.absoluteSubstrateFileBytes == twoGib,
            "canonical substrate ceiling is not exactly 2 GiB");
    assertTrue(limits64.operationalSubstrateFileBytes == twoGib,
            "64-bit substrate limit diverges from the absolute ceiling");
    assertTrue(limits64.worldAllocationBudgetBytes
                    < limits64.absoluteSubstrateFileBytes,
            "64-bit world allocation budget incorrectly promises the file ceiling");
    assertTrue(limits32.operationalSubstrateFileBytes
                    < limits32.absoluteSubstrateFileBytes,
            "32-bit ABI does not have a lower operational file limit");
    assertTrue(limits32.worldAllocationBudgetBytes
                    < limits64.worldAllocationBudgetBytes,
            "32-bit ABI does not have a lower world allocation budget");

    const memory::RuntimeLimits twoGibDevice64 =
            memory::runtimeLimitsForDevice(64, twoGib);
    const memory::RuntimeLimits oneGibDevice32 =
            memory::runtimeLimitsForDevice(32, twoGib / 2);
    assertTrue(twoGibDevice64.worldAllocationBudgetBytes == twoGib / 4,
            "64-bit world budget does not account for device RAM");
    assertTrue(oneGibDevice32.worldAllocationBudgetBytes == twoGib / 8,
            "32-bit world budget does not combine RAM and address-space limits");

    memory::validateSubstrateFileSize(twoGib, limits64);
    expectCode([&] {
        memory::validateSubstrateFileSize(twoGib + 1, limits64);
    },
            DecodeErrorCode::LimitExceeded, "absolute substrate limit plus one");
    expectCode([&] {
        memory::validateSubstrateFileSize(
                limits32.operationalSubstrateFileBytes + 1, limits32);
    },
            DecodeErrorCode::LimitExceeded, "32-bit operational substrate limit plus one");

    const Environment ordinary = Environment::constructDefaultEnvironment(1'000, 3'500, 1.0);
    const memory::WorldAllocationEstimate ordinary64 =
            memory::estimateWorldAllocation(ordinary, 64);
    const memory::WorldAllocationEstimate ordinary32 =
            memory::estimateWorldAllocation(ordinary, 32);
    assertTrue(ordinary64.bytes > 0 && ordinary64.bytes <= ordinary64.budgetBytes,
            "ordinary 64-bit world is outside its allocation policy");
    assertTrue(ordinary32.bytes > 0 && ordinary32.bytes <= ordinary32.budgetBytes,
            "ordinary 32-bit world is outside its allocation policy");

    Environment excessive = ordinary;
    excessive.maxCellCount = std::numeric_limits<int>::max();
    expectCode([&] {
        (void)memory::estimateWorldAllocation(excessive, 64);
    },
            DecodeErrorCode::LimitExceeded, "checked world allocation arithmetic");
    expectCode([&] {
        SimulationWorld world(false, excessive);
        (void)world;
    },
            DecodeErrorCode::LimitExceeded, "world constructor validates before allocation");

    bool foundAbiBoundary = false;
    for (int cellCapacity = 2'000; cellCapacity <= 100'000; cellCapacity += 1'000) {
        Environment candidate = Environment::constructDefaultEnvironment(
                cellCapacity, 3'500, 1.0);
        const memory::WorldAllocationEstimate estimate32 =
                memory::estimateWorldAllocation(candidate, 32);
        const memory::WorldAllocationEstimate estimate64 =
                memory::estimateWorldAllocation(candidate, 64);
        if (estimate32.bytes > estimate32.budgetBytes
                && estimate64.bytes <= estimate64.budgetBytes) {
            foundAbiBoundary = true;
            break;
        }
    }
    assertTrue(foundAbiBoundary,
            "allocation estimator does not enforce a distinct 32-bit boundary");
}

void testPathLoaderStreamsReleaseAndLegacySubstrates() {
    Environment environment =
            Environment::constructDefaultEnvironment(4, 4, 1.0);
    SimulationWorld releaseWorld(false, environment);
    const std::array<std::pair<std::string, std::vector<std::uint8_t>>, 2>
            cases{{{"release",
                           cellularity::save_format::release::
                                   encodeReleaseSubstrate(releaseWorld)
                                           .bytes},
                    {"legacy", fixtureBytes("legacy-substrate-v103")}}};
    for (const auto& [name, bytes] : cases) {
        cellularity::snapshot_codec::TemporarySnapshotFile temporary(
                (std::filesystem::temp_directory_path()
                        / "cellularity-test-input")
                        .string());
        {
            std::ofstream output(
                    temporary.path(),
                    std::ios::binary | std::ios::trunc);
            output.write(
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            if (!output) {
                fail("Could not write path-loader fixture " + name);
            }
        }
        NativeSubstrateLoadResult loaded = loadNativeSubstrateFromPath(
                temporary.path(), false, fallbackEnvironment());
        assertTrue(loaded.world != nullptr,
                "path loader returned no world for " + name);
        assertTrue(
                loaded.formatFamily != NativeSubstrateFormatFamily::Unknown
                        && loaded.familyLocalVersion > 0,
                "path loader returned no format identity for " + name);
        validateNativeSubstratePath(
                temporary.path(), false, fallbackEnvironment());
    }
}

void testSubstrateInspectionPrefixLimit() {
    std::vector<uint8_t> oversizedPrefix(
            cellularity::format_limits::
                            kMaximumSubstrateInspectionPrefixBytes
                    + 1,
            0);
    expectCode([&] {
        (void)inspectSubstrateHeader(
                std::move(oversizedPrefix), false, fallbackEnvironment());
    },
            DecodeErrorCode::LimitExceeded, "oversized substrate inspection prefix");
}

} // namespace

int main() {
    try {
        cellularity::test_fixtures::verifyFormatFixtureCorpus(CORPUS_DIRECTORY);
        testRejectedCorpusUsesTypedFailures();
        testTruncatedHeadersAndBodies();
        testEnvironmentAllocationRejectsOverflow();
        testJavaSerializationLimits();
        testGzipLimitsAndCorruption();
        testRuntimeFileAndAllocationPolicy();
        testPathLoaderStreamsReleaseAndLegacySubstrates();
        testSubstrateInspectionPrefixLimit();
        std::cout << "format malformed tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "format malformed tests failed: " << error.what() << '\n';
        return 1;
    }
}
