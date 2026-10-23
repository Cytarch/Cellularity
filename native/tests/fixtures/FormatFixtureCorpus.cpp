#include "tests/fixtures/FormatFixtureCorpus.h"
#include "tests/support/StableDigest.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

#include <zlib.h>

namespace cellularity::test_fixtures {
namespace {

constexpr int PRODUCT_VERSION = 103;
constexpr int PARAMETRIC_VALUE_COUNT = 11;
constexpr int INTEGER_VALUE_COUNT = 11;
constexpr int MAX_GENES = 40;
constexpr int SIGNAL_COUNT = 4;

class Buffer {
public:
    void byte(uint8_t value) {
        data_.push_back(value);
    }

    void boolean(bool value) {
        byte(value ? 1 : 0);
    }

    void shortValue(int16_t value) {
        const auto bits = static_cast<uint16_t>(value);
        byte(static_cast<uint8_t>(bits >> 8));
        byte(static_cast<uint8_t>(bits));
    }

    void intValue(int32_t value) {
        const auto bits = static_cast<uint32_t>(value);
        byte(static_cast<uint8_t>(bits >> 24));
        byte(static_cast<uint8_t>(bits >> 16));
        byte(static_cast<uint8_t>(bits >> 8));
        byte(static_cast<uint8_t>(bits));
    }

    void longValue(int64_t value) {
        const auto bits = static_cast<uint64_t>(value);
        for (int shift = 56; shift >= 0; shift -= 8) {
            byte(static_cast<uint8_t>(bits >> shift));
        }
    }

    void floatValue(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        intValue(static_cast<int32_t>(bits));
    }

    void doubleValue(double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        longValue(static_cast<int64_t>(bits));
    }

    void append(const std::vector<uint8_t>& bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    void utf(std::string_view value) {
        if (value.size() > 0xffff) {
            throw std::runtime_error("Synthetic UTF value is too long");
        }
        shortValue(static_cast<int16_t>(value.size()));
        data_.insert(data_.end(), value.begin(), value.end());
    }

    const std::vector<uint8_t>& data() const {
        return data_;
    }

private:
    std::vector<uint8_t> data_;
};

std::vector<uint8_t> javaPrimitiveStream(const std::vector<uint8_t>& payload) {
    Buffer stream;
    stream.shortValue(static_cast<int16_t>(0xaced));
    stream.shortValue(5);
    if (payload.size() <= 0xff) {
        stream.byte(0x77);
        stream.byte(static_cast<uint8_t>(payload.size()));
    } else {
        stream.byte(0x7a);
        stream.intValue(static_cast<int32_t>(payload.size()));
    }
    stream.append(payload);
    return stream.data();
}

std::vector<uint8_t> gzip(const std::vector<uint8_t>& input) {
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY)
            != Z_OK) {
        throw std::runtime_error("Could not initialize deterministic fixture gzip");
    }
    std::vector<uint8_t> output;
    std::array<uint8_t, 4096> block{};
    int status;
    do {
        stream.next_out = block.data();
        stream.avail_out = static_cast<uInt>(block.size());
        status = deflate(&stream, Z_FINISH);
        if (status != Z_OK && status != Z_STREAM_END) {
            deflateEnd(&stream);
            throw std::runtime_error("Could not compress deterministic fixture");
        }
        output.insert(output.end(), block.begin(),
                block.begin() + static_cast<std::ptrdiff_t>(block.size() - stream.avail_out));
    } while (status != Z_STREAM_END);
    deflateEnd(&stream);
    return output;
}

std::vector<uint8_t> gunzipFixtureMember(const std::vector<uint8_t>& input,
        std::size_t offset) {
    if (offset >= input.size()
            || input.size() - offset > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
        throw std::runtime_error("Invalid deterministic fixture gzip member");
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(
            reinterpret_cast<const Bytef*>(input.data() + offset));
    stream.avail_in = static_cast<uInt>(input.size() - offset);
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        throw std::runtime_error("Could not initialize deterministic fixture gunzip");
    }

    constexpr std::size_t MAX_FIXTURE_PAYLOAD_SIZE = 16 * 1024 * 1024;
    std::vector<uint8_t> output;
    std::array<uint8_t, 4096> block{};
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        stream.next_out = block.data();
        stream.avail_out = static_cast<uInt>(block.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            inflateEnd(&stream);
            throw std::runtime_error("Could not decompress deterministic fixture gzip");
        }
        output.insert(output.end(), block.begin(),
                block.begin() + static_cast<std::ptrdiff_t>(block.size() - stream.avail_out));
        if (output.size() > MAX_FIXTURE_PAYLOAD_SIZE) {
            inflateEnd(&stream);
            throw std::runtime_error("Deterministic fixture gzip exceeds its test bound");
        }
    }
    const bool consumedEntireMember = stream.avail_in == 0;
    inflateEnd(&stream);
    if (!consumedEntireMember) {
        throw std::runtime_error("Deterministic fixture gzip has trailing bytes");
    }
    return output;
}

bool fixtureMatchesSpec(const std::vector<uint8_t>& actual,
        const std::vector<uint8_t>& generated,
        const FixtureSpec& spec) {
    if (spec.compression != "split-java-gzip") {
        return actual == generated;
    }

    constexpr std::array<uint8_t, 3> GZIP_MAGIC{0x1f, 0x8b, 0x08};
    const auto actualGzip = std::search(
            actual.begin(), actual.end(), GZIP_MAGIC.begin(), GZIP_MAGIC.end());
    const auto generatedGzip = std::search(
            generated.begin(), generated.end(), GZIP_MAGIC.begin(), GZIP_MAGIC.end());
    if (actualGzip == actual.end() || generatedGzip == generated.end()) {
        return false;
    }
    const auto actualOffset = static_cast<std::size_t>(actualGzip - actual.begin());
    const auto generatedOffset = static_cast<std::size_t>(generatedGzip - generated.begin());
    if (actualOffset != generatedOffset
            || !std::equal(actual.begin(), actualGzip, generated.begin(), generatedGzip)) {
        return false;
    }
    return gunzipFixtureMember(actual, actualOffset)
            == gunzipFixtureMember(generated, generatedOffset);
}

void writeLegacyEnvironment(Buffer& out, int version, std::string_view id) {
    out.intValue(version);
    if (version < 1 || version > PRODUCT_VERSION) {
        return;
    }
    out.doubleValue(0.2);
    if (version >= 2) {
        out.doubleValue(0.125);
    }
    out.doubleValue(0.01);
    out.doubleValue(0.8);
    out.doubleValue(0.02);
    out.doubleValue(0.4);
    if (version <= 23) {
        out.doubleValue(0.375);
    }
    out.doubleValue(60.0);
    out.intValue(2);
    out.boolean(true);
    out.boolean(false);
    out.boolean(false);
    out.doubleValue(0.05);
    out.doubleValue(0.1);
    out.doubleValue(-0.2);
    out.boolean(false);
    if (version >= 21) {
        out.doubleValue(0.75);
    } else {
        out.boolean(true);
    }
    if (version >= 3) {
        out.intValue(8);
        out.intValue(8);
        out.doubleValue(1.0);
    }
    if (version >= 9) {
        out.doubleValue(0.3);
        out.doubleValue(0.4);
        out.boolean(true);
    }
    if (version >= 97) {
        out.boolean(true);
    }
    if (version >= 98) {
        int count = 3;
        if (id.find("weights-short") != std::string_view::npos) {
            count = 1;
        } else if (id.find("weights-excess") != std::string_view::npos) {
            count = 128;
        }
        out.intValue(count);
        for (int i = 0; i < count; i++) {
            out.doubleValue(1.0 + static_cast<double>(i) / 100.0);
        }
    }
    if (version >= 99) {
        if (version >= 101) {
            out.intValue(2);
            out.doubleValue(0.07);
            out.doubleValue(0.4);
        }
        out.intValue(2);
        out.doubleValue(0.15);
        out.doubleValue(-0.25);
        out.doubleValue(1.1);
        out.doubleValue(0.2);
        out.doubleValue(0.3);
    }
    if (version >= 10) {
        out.floatValue(0.25f);
    }
    if (version >= 14) {
        out.boolean(true);
        out.doubleValue(0.2);
        out.doubleValue(0.03);
    }
    if (version >= 22) {
        out.boolean(true);
        out.floatValue(0.35f);
    }
    if (version >= 100) {
        out.intValue(1);
        out.doubleValue(0.26);
        out.doubleValue(0.05);
        out.doubleValue(0.7);
        out.doubleValue(-0.1);
        out.doubleValue(0.2);
        out.intValue(2);
        out.doubleValue(0.36);
        out.doubleValue(-0.06);
        out.doubleValue(-0.7);
        out.doubleValue(0.15);
        out.doubleValue(0.25);
    }
    if (version >= 102) {
        out.doubleValue(0.9);
        out.doubleValue(1.1);
        out.doubleValue(1.2);
    }
}

void writeLegacyGene(Buffer& out, int cellVersion, int version) {
    if (cellVersion >= 3) {
        out.intValue(version);
    }
    out.floatValue(0.2f);
    out.floatValue(0.4f);
    out.floatValue(0.6f);
    if (version >= 7) {
        out.floatValue(0.12f);
        out.floatValue(0.4f);
        out.floatValue(0.1f);
        out.floatValue(0.2f);
        out.floatValue(0.3f);
        out.floatValue(1.0f);
        out.intValue(0);
        out.intValue(0);
        if (version >= 103) {
            for (int value = 0; value < 8; value++) {
                out.intValue(value % 3);
            }
        }
        out.boolean(true);
        out.boolean(false);
        out.boolean(true);
        out.intValue(0);
        out.intValue(0);
        out.boolean(true);
        if (version < 9) {
            out.floatValue(0.15f);
            out.intValue(0);
            out.intValue(0);
        }
        out.boolean(true);
        out.boolean(false);
        out.boolean(true);
        out.floatValue(version >= 16 ? 7.0f : 0.2f);
    } else {
        out.doubleValue(0.12);
        out.doubleValue(0.4);
        out.doubleValue(0.1);
        out.doubleValue(0.2);
        out.doubleValue(0.3);
        out.doubleValue(1.0);
        out.intValue(0);
        out.intValue(0);
        out.boolean(true);
        out.boolean(false);
        out.boolean(true);
        out.intValue(0);
        out.intValue(0);
        out.boolean(true);
        out.doubleValue(0.15);
        if (version >= 3) {
            out.intValue(0);
            out.intValue(0);
        }
        if (version >= 4) {
            out.boolean(true);
            out.boolean(false);
            out.boolean(true);
            out.doubleValue(7.0);
        }
    }
    if (version == 8) {
        out.floatValue(0.45f);
    }
    if (version >= 9) {
        for (int i = 0; i < PARAMETRIC_VALUE_COUNT; i++) {
            out.shortValue(static_cast<int16_t>(i % 4));
            if (version >= 17) {
                out.shortValue(static_cast<int16_t>(i % 3));
            }
            out.floatValue(0.01f * static_cast<float>(i));
            out.floatValue(0.02f * static_cast<float>(i));
            out.floatValue(0.03f * static_cast<float>(i));
        }
        for (int i = 0; i < INTEGER_VALUE_COUNT; i++) {
            if (version == 10 && i == 4) {
                continue;
            }
            if (version < 15 && i == 9) {
                continue;
            }
            if (version < 20 && i == 10) {
                continue;
            }
            out.intValue(i == 9 ? 5 : 0);
        }
        for (int i = 0; i < 5; i++) {
            out.floatValue(0.1f * static_cast<float>(i + 1));
        }
        if (version >= 16) {
            out.floatValue(0.02f);
        }
        if (version >= 14) {
            out.floatValue(1.25f);
        }
    }
}

void writeLegacyLink(Buffer& out, int version) {
    out.intValue(-1);
    out.doubleValue(0.2);
    out.doubleValue(0.0);
    out.boolean(false);
    out.doubleValue(0.5);
    out.doubleValue(-0.25);
    if (version >= 4) {
        if (version >= 7) {
            out.floatValue(0.5f);
        } else {
            out.doubleValue(0.5);
        }
    }
    if (version >= 16) {
        out.floatValue(0.02f);
    }
}

void writeLegacyCell(Buffer& out, int cellVersion, int geneVersion,
        int geneCount, int linkCount) {
    out.intValue(cellVersion);
    if (cellVersion < 2 || cellVersion > PRODUCT_VERSION) {
        return;
    }
    out.doubleValue(0.1);
    out.doubleValue(-0.1);
    out.doubleValue(0.2);
    out.doubleValue(0.3);
    out.doubleValue(0.01);
    out.doubleValue(-0.02);
    out.doubleValue(0.04);
    out.doubleValue(-0.05);
    out.doubleValue(0.03);
    out.doubleValue(0.2);
    out.doubleValue(1.5);
    out.intValue(linkCount);
    for (int i = 0; i < linkCount; i++) {
        writeLegacyLink(out, cellVersion);
    }
    out.intValue(linkCount);
    out.boolean(false);
    out.floatValue(0.2f);
    out.floatValue(0.4f);
    out.floatValue(0.6f);
    out.intValue(geneCount);
    for (int i = 0; i < geneCount; i++) {
        writeLegacyGene(out, cellVersion, geneVersion);
    }
    out.intValue(0);
    if (cellVersion >= 103) {
        out.intValue(7);
    }
    out.intValue(11);
    out.intValue(12);
    out.doubleValue(0.6);
    out.doubleValue(0.01);
    out.doubleValue(0.02);
    out.doubleValue(0.8);
    if (cellVersion >= 4) {
        out.boolean(true);
    }
    if (cellVersion >= 9) {
        for (int i = 0; i < SIGNAL_COUNT; i++) {
            out.floatValue(0.1f * static_cast<float>(i + 1));
            out.floatValue(0.2f * static_cast<float>(i + 1));
        }
        out.floatValue(0.1f);
        out.floatValue(0.2f);
        out.floatValue(0.3f);
        out.floatValue(0.4f);
        out.floatValue(0.5f);
        if (cellVersion >= 12) {
            out.floatValue(0.06f);
        }
    }
    if (cellVersion >= 15) {
        out.intValue(3);
    }
    if (cellVersion >= 19) {
        out.intValue(17);
    }
    if (cellVersion >= 23) {
        out.doubleValue(0.005);
    }
}

void writeLegacyFood(Buffer& out, int version, int count) {
    out.intValue(count);
    for (int i = 0; i < count; i++) {
        if (version >= 8) {
            out.floatValue(0.2f);
            out.floatValue(-0.3f);
            out.floatValue(0.04f);
        } else {
            out.doubleValue(0.2);
            out.doubleValue(-0.3);
            out.doubleValue(0.04);
        }
        if (version >= 22) {
            out.floatValue(0.01f);
            out.floatValue(-0.02f);
            out.floatValue(0.3f);
        }
    }
}

std::vector<uint8_t> makeLegacySubstrate(const FixtureSpec& spec) {
    Buffer header;
    header.intValue(spec.outerVersion);
    if (spec.outerVersion < 1 || spec.outerVersion > PRODUCT_VERSION) {
        return javaPrimitiveStream(header.data());
    }
    header.doubleValue(12.5);
    header.intValue(spec.cellCount);
    writeLegacyEnvironment(header, spec.environmentVersion, spec.id);
    if (spec.environmentVersion < 1 || spec.environmentVersion > PRODUCT_VERSION) {
        return javaPrimitiveStream(header.data());
    }

    Buffer body;
    if (spec.outerVersion >= 24) {
        body.doubleValue(0.25);
    }
    if (spec.outerVersion >= 96) {
        body.longValue(0x0102030405060708LL);
    }
    for (int i = 0; i < spec.cellCount; i++) {
        writeLegacyCell(body, spec.cellVersion, spec.geneVersion,
                spec.geneCount, spec.linkCount);
    }
    writeLegacyFood(body, spec.outerVersion, spec.foodCount);

    if (spec.outerVersion < 13) {
        Buffer complete;
        complete.append(header.data());
        complete.append(body.data());
        return javaPrimitiveStream(complete.data());
    }
    std::vector<uint8_t> result = javaPrimitiveStream(header.data());
    const std::vector<uint8_t> compressed = gzip(javaPrimitiveStream(body.data()));
    result.insert(result.end(), compressed.begin(), compressed.end());
    return result;
}

int standaloneGeneCount(int version) {
    if (version <= 3) {
        return 15;
    }
    if (version <= 17) {
        return 20;
    }
    return MAX_GENES;
}

std::vector<uint8_t> makeLegacyGenome(const FixtureSpec& spec) {
    Buffer payload;
    payload.intValue(spec.productVersion);
    const int count = standaloneGeneCount(spec.productVersion);
    const int geneVersion = spec.productVersion < 3 ? 1 : spec.geneVersion;
    for (int i = 0; i < count; i++) {
        writeLegacyGene(payload, spec.productVersion, geneVersion);
    }
    return javaPrimitiveStream(payload.data());
}

std::vector<uint8_t> makeJavaSerializationFixture(const FixtureSpec& spec) {
    Buffer out;
    out.shortValue(static_cast<int16_t>(0xaced));
    out.shortValue(5);
    if (spec.id == "jos-header-invalid") {
        std::vector<uint8_t> bytes = out.data();
        bytes[0] = 0;
        return bytes;
    }
    if (spec.id == "jos-blockdata") {
        out.byte(0x77);
        out.byte(4);
        out.intValue(0x01020304);
    } else if (spec.id == "jos-blockdatalong") {
        out.byte(0x7a);
        out.intValue(4);
        out.intValue(0x01020304);
    } else if (spec.id == "jos-reset") {
        out.byte(0x74);
        out.utf("A");
        out.byte(0x79);
        out.byte(0x74);
        out.utf("B");
    } else if (spec.id == "jos-string-reference") {
        out.byte(0x74);
        out.utf("Cellularity");
        out.byte(0x71);
        out.intValue(0x007e0000);
    } else if (spec.id == "jos-longstring") {
        out.byte(0x7c);
        out.longValue(4);
        for (char value : std::string("Long")) {
            out.byte(static_cast<uint8_t>(value));
        }
    } else if (spec.id == "jos-null") {
        out.byte(0x70);
    } else if (spec.id == "jos-class-desc") {
        out.byte(0x72);
        out.utf("Value");
        out.longValue(1);
        out.byte(0x02);
        out.shortValue(0);
        out.byte(0x78);
        out.byte(0x70);
    } else if (spec.id == "jos-object-graph") {
        const std::vector<uint8_t> bytes{
                0x73, 0x72, 0x00, 0x05, 'V', 'a', 'l', 'u', 'e',
                0, 0, 0, 0, 0, 0, 0, 1,
                0x02, 0x00, 0x01,
                'I', 0x00, 0x01, 'v',
                0x78, 0x70,
                0x00, 0x00, 0x00, 0x07
        };
        out.append(bytes);
    } else if (spec.id == "jos-primitive-array" || spec.id == "jos-object-array") {
        out.byte(0x75);
        out.byte(0x72);
        const bool objects = spec.id == "jos-object-array";
        out.utf(objects ? "[Ljava.lang.String;" : "[I");
        out.longValue(1);
        out.byte(0x02);
        out.shortValue(0);
        out.byte(0x78);
        out.byte(0x70);
        out.intValue(2);
        if (objects) {
            out.byte(0x74);
            out.utf("A");
            out.byte(0x70);
        } else {
            out.intValue(1);
            out.intValue(2);
        }
    } else if (spec.id == "jos-proxy-reject") {
        out.byte(0x7d);
    } else if (spec.id == "jos-externalizable-v1-reject") {
        out.byte(0x73);
        out.byte(0x72);
        out.utf("External");
        out.longValue(1);
        out.byte(0x04);
        out.shortValue(0);
        out.byte(0x78);
        out.byte(0x70);
    } else if (spec.id == "jos-malformed-tag") {
        out.byte(0x01);
    } else {
        throw std::runtime_error("Unknown Java serialization fixture: " + spec.id);
    }
    return out.data();
}

std::vector<uint8_t> generateFixture(const FixtureSpec& spec) {
    if (spec.family == "legacy-substrate")
        return makeLegacySubstrate(spec);
    if (spec.family == "legacy-genome")
        return makeLegacyGenome(spec);
    if (spec.family == "java-serialization")
        return makeJavaSerializationFixture(spec);
    throw std::runtime_error("Unknown fixture family: " + spec.family);
}

FixtureSpec fixture(std::string id,
        std::string family,
        int outerVersion,
        int environmentVersion,
        int cellVersion,
        int geneVersion,
        int snapshotVersion,
        int productVersion,
        std::string compression,
        int cellCount,
        int geneCount,
        int foodCount,
        int linkCount,
        std::string expected,
        std::string semanticValues,
        std::string covers) {
    return {
            std::move(id), std::move(family), outerVersion, environmentVersion,
            cellVersion, geneVersion, snapshotVersion, productVersion,
            std::move(compression), cellCount, geneCount, foodCount, linkCount,
            std::move(expected), std::move(semanticValues), std::move(covers)
    };
}

std::string legacyCoverage(int version) {
    switch (version) {
        case 1:
            return "LEG-ENV-001;LEG-ENV-001-FALLBACK;LEG-OUTER-001-ZERO-CELL";
        case 2:
            return "LEG-ENV-002;LEG-ENV-002-FALLBACK;LEG-CELL-002;LEG-GENE-001;LEG-GENE-002-NO-VIRUS;LEG-GENE-PREFIX-002/003";
        case 3:
            return "LEG-ENV-003;LEG-GENE-PREFIX-003;LEG-GENE-003-VIRUS;LEG-GENE-003-NO-MIRROR;LEG-CELL-003-MIRROR-DEFAULT;LEG-LINK-003-DEFAULT";
        case 4:
            return "LEG-GENE-004;LEG-LINK-004;LEG-CELL-004";
        case 5:
            return "LEG-GENE-005-ANGLE";
        case 6:
            return "LEG-GENE-006-ALIAS;LEG-LINK-006-DOUBLE";
        case 7:
            return "LEG-GENE-007-FLOAT;LEG-GENE-007-SWIM-DEFAULT;LEG-LINK-007-FLOAT;LEG-FOOD-007-DOUBLE";
        case 8:
            return "LEG-GENE-008-SWIM;LEG-GENE-008-LEGACY-TAIL;LEG-FOOD-008-FLOAT;LEG-CELL-008-SIGNALS-DEFAULT";
        case 9:
            return "LEG-ENV-009;LEG-GENE-009-PARAMETRIC;LEG-CELL-009-SIGNALS";
        case 10:
            return "LEG-ENV-010;LEG-GENE-010-SECRETE";
        case 11:
            return "LEG-GENE-011-ALIAS;LEG-CELL-011-LIPIDS-DEFAULT";
        case 12:
            return "LEG-CELL-012-LIPIDS;LEG-OUTER-012-PLAIN";
        case 13:
            return "LEG-GENE-013-CYTOSKELETON-DEFAULT;LEG-OUTER-013-GZIP";
        case 14:
            return "LEG-ENV-014;LEG-GENE-014-CYTOSKELETON;LEG-GENE-014-MAX-LINKS-DEFAULT;LEG-CELL-014-MUTATION-DEFAULT";
        case 15:
            return "LEG-GENE-015-MAX-LINKS;LEG-GENE-015-ADHESIN-DEFAULT;LEG-GENE-015-LINK-NORMALIZED;LEG-LINK-015-REST-DEFAULT;LEG-CELL-015-MUTATION";
        case 16:
            return "LEG-GENE-016-LINK;LEG-GENE-016-INFERRED-FORMULA;LEG-LINK-016-REST";
        case 17:
            return "LEG-GENE-017-FORMULA";
        case 18:
            return "LEG-CELL-018-TELOMERES-DEFAULT";
        case 19:
            return "LEG-GENE-019-TELOMERASE-DEFAULT;LEG-CELL-019-TELOMERES";
        case 20:
            return "LEG-GENE-020-TELOMERASE;LEG-ENV-020-NITRATES-BOOL";
        case 21:
            return "LEG-ENV-021-NITRATES-DOUBLE;LEG-FOOD-021-DEFAULT";
        case 22:
            return "LEG-ENV-022-MOBILE-FOOD;LEG-FOOD-022-VELOCITY;LEG-CELL-022-Z-DEFAULT";
        case 23:
            return "LEG-CELL-023-Z;LEG-OUTER-023-HEADER-ANGLE";
        case 24:
            return "LEG-OUTER-024-BODY-ANGLE";
        case 25:
            return "LEG-OUTER-025-ALIAS";
        case 95:
            return "LEG-OUTER-095-SEED-FALLBACK";
        case 96:
            return "LEG-OUTER-096-SEED";
        case 97:
            return "LEG-ENV-097-MUTATE-AFTER-SPLIT";
        case 98:
            return "LEG-ENV-098-WEIGHTS-SHORT/FULL/EXCESS";
        case 99:
            return "LEG-ENV-099-DENSITY-FIELD;LEG-ENV-099-FIELD-FALLBACK";
        case 100:
            return "LEG-ENV-100-SCALAR-FIELDS";
        case 101:
            return "LEG-ENV-101-GRAVITY-FIELD";
        case 102:
            return "LEG-ENV-102-TUNING;LEG-CELL-102-OBSOLETE-EXTENSION-DEFAULT";
        case 103:
            return "LEG-GENE-103-OBSOLETE-EXTENSION;LEG-CELL-103-OBSOLETE-EXTENSION";
        default:
            return "";
    }
}

std::string legacySemanticValues(int version, int cellCount, int foodCount) {
    std::ostringstream values;
    values << "time=12.5"
           << ";light_angle=" << (version >= 24 ? "0.25" : "0.75")
           << ";random_seed="
           << (version >= 96 ? "72623859790382856" : "95")
           << ";environment.food_speed=" << (version == 1 ? "0.03" : "0.2")
           << ";environment.food_size=" << (version >= 2 ? "0.125" : "0.333")
           << ";environment.nitrates=" << (version >= 21 ? "0.75" : "1.0")
           << ";environment.salinity=" << (version >= 10 ? "0.25" : "0.15")
           << ";environment.mobile_food=" << (version >= 22 ? "true" : "false")
           << ";cell_count=" << cellCount
           << ";food_count=" << foodCount
           << ";topology_boundary_count=" << (cellCount > 0 ? 1 : 0);
    if (cellCount > 0) {
        values << ";cell.position_x=0.1"
               << ";cell.position_y=-0.1"
               << ";cell.position_z=" << (version >= 23 ? "0.005" : "0.0")
               << ";cell.mass=0.2"
               << ";gene.color=0.2,0.4,0.6"
               << ";gene.initial_count=1";
    }
    if (foodCount > 0) {
        values << ";food.position=0.2,-0.3"
               << ";food.mass=0.04"
               << ";food.velocity=" << (version >= 22 ? "0.01,-0.02" : "0.0,0.0")
               << ";food.lipid_layer=" << (version >= 22 ? "0.3" : "0.0");
    }
    return values.str();
}

std::vector<FixtureSpec> createSpecs() {
    std::vector<FixtureSpec> specs;
    const std::array<int, 34> legacyVersions{
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
            18, 19, 20, 21, 22, 23, 24, 25, 95, 96, 97, 98, 99, 100, 101, 102, 103
    };
    for (int version : legacyVersions) {
        std::ostringstream id;
        id << "legacy-substrate-v" << std::setw(3) << std::setfill('0') << version;
        const int cells = version == 1 ? 0 : 1;
        const int genes = cells == 0 ? 0 : 1;
        const int geneVersion = version < 3 ? 1 : version;
        specs.push_back(fixture(
                id.str(), "legacy-substrate", version, version,
                cells == 0 ? -1 : std::max(version, 2), geneVersion, -1, -1,
                version >= 13 ? "split-java-gzip" : "none", cells, genes, 1, cells,
                "accept-native", legacySemanticValues(version, cells, 1),
                legacyCoverage(version)));
    }
    specs.push_back(fixture(
            "legacy-env-v098-weights-short", "legacy-substrate",
            98, 98, -1, -1, -1, -1, "split-java-gzip", 0, 0, 0, 0,
            "accept-native", legacySemanticValues(98, 0, 0) + ";mutation_weight_count=1",
            "LEG-ENV-098-WEIGHTS-SHORT/FULL/EXCESS"));
    specs.push_back(fixture(
            "legacy-env-v098-weights-excess", "legacy-substrate",
            98, 98, -1, -1, -1, -1, "split-java-gzip", 0, 0, 0, 0,
            "accept-native",
            legacySemanticValues(98, 0, 0) + ";mutation_weight_count=128;excess=ignored",
            "LEG-ENV-098-WEIGHTS-SHORT/FULL/EXCESS"));

    for (int version : {2, 3, 4, 17, 18, 103}) {
        std::ostringstream id;
        id << "legacy-genome-v" << std::setw(3) << std::setfill('0') << version;
        std::string covers;
        if (version == 2)
            covers = "LEG-GENOME-002-COUNT15-NO-GENE-VERSION";
        if (version == 3)
            covers = "LEG-GENOME-003-COUNT15;LEG-GENOME-003-COUNT15-WITH-GENE-VERSION";
        if (version == 4)
            covers = "LEG-GENOME-004-COUNT20";
        if (version == 17)
            covers = "LEG-GENOME-017-COUNT20";
        if (version == 18)
            covers = "LEG-GENOME-018-COUNT40";
        if (version == 103)
            covers = "LEG-GENOME-103-OBSOLETE-EXTENSION";
        specs.push_back(fixture(
                id.str(), "legacy-genome", -1, -1, version,
                version < 3 ? 1 : version, -1, version, "none",
                0, standaloneGeneCount(version), 0, 0, "accept-native",
                "genes=" + std::to_string(standaloneGeneCount(version))
                        + ";exactly_one_initial_after_load=true",
                covers));
    }

    for (int outerVersion : {0, 104}) {
        specs.push_back(fixture(
                "legacy-outer-v" + std::to_string(outerVersion) + "-invalid",
                "legacy-substrate", outerVersion, 103, -1, -1, -1, -1,
                "none", 0, 0, 0, 0, "reject",
                "invalid_outer_version=" + std::to_string(outerVersion),
                "LEG-OUTER-RANGE-INVALID"));
    }
    for (int environmentVersion : {0, 104}) {
        specs.push_back(fixture(
                "legacy-environment-v" + std::to_string(environmentVersion) + "-invalid",
                "legacy-substrate", 103, environmentVersion, -1, -1, -1, -1,
                "none", 0, 0, 0, 0, "reject",
                "invalid_environment_version=" + std::to_string(environmentVersion),
                "LEG-ENV-RANGE-INVALID"));
    }
    for (int cellVersion : {1, 104}) {
        specs.push_back(fixture(
                "legacy-cell-v" + std::to_string(cellVersion) + "-invalid",
                "legacy-substrate", 103, 103, cellVersion, 103, -1, -1,
                "split-java-gzip", 1, 1, 0, 0, "reject",
                "invalid_cell_version=" + std::to_string(cellVersion),
                "LEG-CELL-RANGE-INVALID"));
    }
    for (int geneVersion : {0, 104}) {
        specs.push_back(fixture(
                "legacy-gene-v" + std::to_string(geneVersion) + "-invalid",
                "legacy-substrate", 103, 103, 103, geneVersion, -1, -1,
                "split-java-gzip", 1, 1, 0, 0, "reject",
                "invalid_gene_version=" + std::to_string(geneVersion) + ";error=version",
                "LEG-GENE-VERSION-INVALID-LOW/HIGH"));
    }

    const auto javaFixture = [&specs](const std::string& id,
                                     const std::string& expected,
                                     const std::string& semanticValues,
                                     const std::string& covers) {
        specs.push_back(fixture(
                id, "java-serialization", -1, -1, -1, -1, -1, -1,
                "none", 0, 0, 0, 0, expected, semanticValues, covers));
    };
    javaFixture(
            "jos-blockdata", "accept-native", "primitive_int=16909060",
            "JOS-HEADER-VALID/INVALID;JOS-BLOCKDATA;JOS-PRIMITIVES");
    javaFixture(
            "jos-blockdatalong", "accept-native", "primitive_int=16909060",
            "JOS-BLOCKDATALONG");
    javaFixture("jos-reset", "accept-native", "strings=A,B", "JOS-RESET");
    javaFixture(
            "jos-string-reference", "accept-native",
            "string=Cellularity;reference_handle=0x7e0000",
            "JOS-STRING-REFERENCE");
    javaFixture("jos-longstring", "accept-native", "string=Long", "JOS-LONGSTRING");
    javaFixture("jos-null", "accept-native", "value=null", "JOS-NULL");
    javaFixture(
            "jos-class-desc", "accept-native", "class=Value;fields=0",
            "JOS-CLASS-DESC");
    javaFixture(
            "jos-object-graph", "accept-native", "class=Value;field_v=7",
            "JOS-OBJECT-GRAPH");
    javaFixture(
            "jos-primitive-array", "accept-native", "int_array=1,2",
            "JOS-PRIMITIVE-ARRAY");
    javaFixture(
            "jos-object-array", "accept-native", "string_array=A,null",
            "JOS-OBJECT-ARRAY");
    javaFixture(
            "jos-proxy-reject", "reject", "tag=TC_PROXYCLASSDESC",
            "JOS-PROXY-REJECT");
    javaFixture(
            "jos-externalizable-v1-reject", "reject",
            "flags=SC_EXTERNALIZABLE_without_block_data",
            "JOS-EXTERNALIZABLE-V1-REJECT");
    javaFixture(
            "jos-malformed-tag", "reject", "tag=0x01",
            "JOS-MALFORMED-*");
    javaFixture(
            "jos-header-invalid", "reject", "stream_magic=0x00ed",
            "JOS-HEADER-VALID/INVALID");

    return specs;
}

constexpr std::string_view MANIFEST_HEADER =
        "id\tfile\tfamily\touter_version\tenvironment_version\tcell_version\t"
        "gene_version\tsnapshot_version\tproduct_version\tcompression\t"
        "cell_count\tgene_count\tfood_count\tlink_count\texpected\t"
        "semantic_values\tcovers\tprovenance\tlicense\tsha256";
constexpr std::string_view PROVENANCE =
        "generated:cellularity-format-fixture-tool-v1";
constexpr std::string_view LICENSE = "MIT";

std::string integerOrDash(int value) {
    return value < 0 ? "-" : std::to_string(value);
}

int parseIntegerOrDash(const std::string& value, const std::string& fieldName) {
    if (value == "-") {
        return -1;
    }
    size_t parsed = 0;
    int result;
    try {
        result = std::stoi(value, &parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer in manifest field " + fieldName + ": " + value);
    }
    if (parsed != value.size()) {
        throw std::runtime_error("Invalid integer in manifest field " + fieldName + ": " + value);
    }
    return result;
}

std::string fixtureFileName(const FixtureSpec& spec) {
    return spec.id + ".bin";
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open fixture: " + path.string());
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("Cannot determine fixture size: " + path.string());
    }
    const size_t size = static_cast<size_t>(end);
    std::vector<uint8_t> bytes(size);
    input.seekg(0);
    if (size > 0) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    }
    if (!input) {
        throw std::runtime_error("Cannot read fixture: " + path.string());
    }
    return bytes;
}

void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create fixture: " + path.string());
    }
    if (!bytes.empty()) {
        output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw std::runtime_error("Cannot write fixture: " + path.string());
    }
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t separator = line.find('\t', start);
        if (separator == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, separator - start));
        start = separator + 1;
    }
}

void validateTextField(const std::string& value,
        const std::string& fieldName,
        const std::string& fixtureId) {
    if (value.empty()) {
        throw std::runtime_error("Empty " + fieldName + " for fixture " + fixtureId);
    }
    if (value.find_first_of("\t\r\n") != std::string::npos) {
        throw std::runtime_error("Unsafe " + fieldName + " for fixture " + fixtureId);
    }
}

void validateSpec(const FixtureSpec& spec) {
    validateTextField(spec.id, "id", spec.id);
    if (!std::all_of(spec.id.begin(), spec.id.end(), [](unsigned char value) {
            return std::islower(value) != 0 || std::isdigit(value) != 0 || value == '-';
        })) {
        throw std::runtime_error("Fixture id is not path-safe: " + spec.id);
    }
    validateTextField(spec.family, "family", spec.id);
    validateTextField(spec.compression, "compression", spec.id);
    validateTextField(spec.expected, "expected", spec.id);
    validateTextField(spec.semanticValues, "semantic_values", spec.id);
    validateTextField(spec.covers, "covers", spec.id);
    if ((spec.cellCount < 0 || spec.geneCount < 0
                || spec.foodCount < 0 || spec.linkCount < 0)
            && spec.expected != "reject"
            && spec.expected != "divergent"
            && spec.expected != "known-defect") {
        throw std::runtime_error(
                "Negative object count is only valid for malformed-result fixtures: " + spec.id);
    }
    const std::set<std::string> supportedCompression{"none", "split-java-gzip"};
    if (supportedCompression.count(spec.compression) == 0) {
        throw std::runtime_error("Unsupported compression label for fixture " + spec.id);
    }
    const std::set<std::string> supportedExpected{
            "accept-native", "java-only", "java-header-only", "reject",
            "divergent", "known-defect"
    };
    if (supportedExpected.count(spec.expected) == 0) {
        throw std::runtime_error("Unsupported expected result for fixture " + spec.id);
    }
}

void compareField(const std::string& actual,
        const std::string& expected,
        const std::string& fieldName,
        const std::string& fixtureId) {
    if (actual != expected) {
        throw std::runtime_error(
                "Manifest mismatch for " + fixtureId + " field " + fieldName
                + ": expected '" + expected + "', got '" + actual + "'");
    }
}

} // namespace

const std::vector<FixtureSpec>& formatFixtureSpecs() {
    static const std::vector<FixtureSpec> specs = createSpecs();
    return specs;
}

void writeFormatFixtureCorpus(const std::filesystem::path& corpusDirectory) {
    std::filesystem::create_directories(corpusDirectory);
    const auto& specs = formatFixtureSpecs();
    std::set<std::string> ids;
    std::set<std::string> generatedFiles;
    std::ofstream manifest(corpusDirectory / "manifest.tsv", std::ios::trunc);
    if (!manifest) {
        throw std::runtime_error("Cannot create fixture manifest");
    }
    manifest << MANIFEST_HEADER << '\n';
    for (const FixtureSpec& spec : specs) {
        validateSpec(spec);
        if (!ids.insert(spec.id).second) {
            throw std::runtime_error("Duplicate fixture id: " + spec.id);
        }
        const std::vector<uint8_t> bytes = generateFixture(spec);
        if (bytes.empty()) {
            throw std::runtime_error("Generated empty fixture: " + spec.id);
        }
        const std::string fileName = fixtureFileName(spec);
        generatedFiles.insert(fileName);
        writeFile(corpusDirectory / fileName, bytes);
        manifest
                << spec.id << '\t' << fileName << '\t' << spec.family << '\t'
                << integerOrDash(spec.outerVersion) << '\t'
                << integerOrDash(spec.environmentVersion) << '\t'
                << integerOrDash(spec.cellVersion) << '\t'
                << integerOrDash(spec.geneVersion) << '\t'
                << integerOrDash(spec.snapshotVersion) << '\t'
                << integerOrDash(spec.productVersion) << '\t'
                << spec.compression << '\t'
                << spec.cellCount << '\t' << spec.geneCount << '\t'
                << spec.foodCount << '\t' << spec.linkCount << '\t'
                << spec.expected << '\t' << spec.semanticValues << '\t'
                << spec.covers << '\t' << PROVENANCE << '\t' << LICENSE << '\t'
                << test_support::stableDigest(bytes) << '\n';
    }
    if (!manifest) {
        throw std::runtime_error("Cannot write fixture manifest");
    }
    manifest.close();
    for (const auto& entry : std::filesystem::directory_iterator(corpusDirectory)) {
        if (entry.is_regular_file()
                && entry.path().extension() == ".bin"
                && generatedFiles.count(entry.path().filename().string()) == 0) {
            std::filesystem::remove(entry.path());
        }
    }
}

void verifyFormatFixtureCorpus(const std::filesystem::path& corpusDirectory) {
    const auto& specs = formatFixtureSpecs();
    for (const FixtureSpec& spec : specs) {
        validateSpec(spec);
    }

    std::ifstream manifest(corpusDirectory / "manifest.tsv");
    if (!manifest) {
        throw std::runtime_error("Cannot open fixture manifest");
    }
    std::string line;
    if (!std::getline(manifest, line)) {
        throw std::runtime_error("Fixture manifest is empty");
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line != MANIFEST_HEADER) {
        throw std::runtime_error("Unexpected fixture manifest schema");
    }

    std::set<std::string> seen;
    size_t lineNumber = 1;
    while (std::getline(manifest, line)) {
        lineNumber++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            throw std::runtime_error("Blank manifest row at line " + std::to_string(lineNumber));
        }
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() != 20) {
            throw std::runtime_error(
                    "Expected 20 manifest fields at line " + std::to_string(lineNumber));
        }
        const std::string& id = fields[0];
        const auto found = std::find_if(
                specs.begin(), specs.end(), [&id](const FixtureSpec& spec) {
                    return spec.id == id;
                });
        if (found == specs.end()) {
            throw std::runtime_error("Manifest contains unknown fixture: " + id);
        }
        if (!seen.insert(id).second) {
            throw std::runtime_error("Manifest contains duplicate fixture: " + id);
        }
        const FixtureSpec& spec = *found;
        compareField(fields[1], fixtureFileName(spec), "file", id);
        compareField(fields[2], spec.family, "family", id);
        if (parseIntegerOrDash(fields[3], "outer_version") != spec.outerVersion
                || parseIntegerOrDash(fields[4], "environment_version") != spec.environmentVersion
                || parseIntegerOrDash(fields[5], "cell_version") != spec.cellVersion
                || parseIntegerOrDash(fields[6], "gene_version") != spec.geneVersion
                || parseIntegerOrDash(fields[7], "snapshot_version") != spec.snapshotVersion
                || parseIntegerOrDash(fields[8], "product_version") != spec.productVersion) {
            throw std::runtime_error("Manifest version mismatch for fixture " + id);
        }
        compareField(fields[9], spec.compression, "compression", id);
        if (parseIntegerOrDash(fields[10], "cell_count") != spec.cellCount
                || parseIntegerOrDash(fields[11], "gene_count") != spec.geneCount
                || parseIntegerOrDash(fields[12], "food_count") != spec.foodCount
                || parseIntegerOrDash(fields[13], "link_count") != spec.linkCount) {
            throw std::runtime_error("Manifest object count mismatch for fixture " + id);
        }
        compareField(fields[14], spec.expected, "expected", id);
        compareField(fields[15], spec.semanticValues, "semantic_values", id);
        compareField(fields[16], spec.covers, "covers", id);
        compareField(fields[17], std::string(PROVENANCE), "provenance", id);
        compareField(fields[18], std::string(LICENSE), "license", id);
        if (fields[19].size() != 64
                || !std::all_of(fields[19].begin(), fields[19].end(), [](unsigned char value) {
                       return std::isdigit(value) != 0 || (value >= 'a' && value <= 'f');
                   })) {
            throw std::runtime_error("Invalid SHA-256 value for fixture " + id);
        }

        const std::vector<uint8_t> actual = readFile(corpusDirectory / fields[1]);
        if (actual.empty()) {
            throw std::runtime_error("Fixture file is empty: " + fields[1]);
        }
        compareField(test_support::stableDigest(actual), fields[19], "sha256", id);
        const std::vector<uint8_t> generated = generateFixture(spec);
        if (!fixtureMatchesSpec(actual, generated, spec)) {
            throw std::runtime_error("Fixture is not reproducible from its spec: " + id);
        }
    }
    if (!manifest.eof()) {
        throw std::runtime_error("Cannot finish reading fixture manifest");
    }
    if (seen.size() != specs.size()) {
        throw std::runtime_error(
                "Manifest has " + std::to_string(seen.size()) + " fixtures, expected "
                + std::to_string(specs.size()));
    }

    for (const auto& entry : std::filesystem::directory_iterator(corpusDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            const std::string id = entry.path().stem().string();
            if (seen.count(id) == 0) {
                throw std::runtime_error("Unlisted fixture file: " + entry.path().filename().string());
            }
        }
    }
}

} // namespace cellularity::test_fixtures
