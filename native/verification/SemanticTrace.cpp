#include "verification/SemanticTrace.h"

#if defined(CELLULARITY_SEMANTIC_TRACE)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace cellularity::verification {
namespace {

constexpr std::uint8_t kTraceEncodingMagic = 0x43;
constexpr std::uint8_t kTraceEncodingVersion = 1;
constexpr std::uint8_t kDiscreteTraceEncodingVersion = 2;
constexpr std::size_t kMaximumFieldsPerEvent = 32;
constexpr std::size_t kMaximumEncodedEventBytes = 384;

std::uint32_t rotateRight(std::uint32_t value, std::uint32_t count) noexcept {
    return (value >> count) | (value << (32U - count));
}

class StreamingSha256 final {
public:
    StreamingSha256()
            : state_{
                      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
              } {
    }

    void update(const std::uint8_t* data, std::size_t size) {
        if (finished_) {
            throw std::logic_error("Semantic trace digest was already finalized");
        }
        totalBytes_ += size;
        while (size > 0) {
            const std::size_t copied = std::min(size, block_.size() - blockSize_);
            std::memcpy(block_.data() + blockSize_, data, copied);
            blockSize_ += copied;
            data += copied;
            size -= copied;
            if (blockSize_ == block_.size()) {
                transform(block_.data());
                blockSize_ = 0;
            }
        }
    }

    std::string finish() {
        if (finished_) {
            return result_;
        }
        const std::uint64_t bitLength = static_cast<std::uint64_t>(totalBytes_) * 8U;
        block_[blockSize_++] = 0x80;
        if (blockSize_ > 56) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(blockSize_), block_.end(), 0);
            transform(block_.data());
            blockSize_ = 0;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(blockSize_), block_.begin() + 56, 0);
        for (int shift = 56; shift >= 0; shift -= 8) {
            block_[56 + static_cast<std::size_t>((56 - shift) / 8)] =
                    static_cast<std::uint8_t>(bitLength >> shift);
        }
        transform(block_.data());

        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (const std::uint32_t value : state_) {
            out << std::setw(8) << value;
        }
        result_ = out.str();
        finished_ = true;
        return result_;
    }

private:
    void transform(const std::uint8_t* block) {
        static constexpr std::array<std::uint32_t, 64> constants{
                0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
                0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
                0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
                0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
                0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
                0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
                0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
                0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
                0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
                0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
                0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
                0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
                0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
                0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
                0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
                0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
        };
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; i++) {
            words[i] = static_cast<std::uint32_t>(block[i * 4]) << 24
                    | static_cast<std::uint32_t>(block[i * 4 + 1]) << 16
                    | static_cast<std::uint32_t>(block[i * 4 + 2]) << 8
                    | static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < words.size(); i++) {
            const std::uint32_t s0 = rotateRight(words[i - 15], 7)
                    ^ rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const std::uint32_t s1 = rotateRight(words[i - 2], 17)
                    ^ rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (std::size_t i = 0; i < words.size(); i++) {
            const std::uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sum1 + choice + constants[i] + words[i];
            const std::uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> block_{};
    std::size_t blockSize_ = 0;
    std::size_t totalBytes_ = 0;
    bool finished_ = false;
    std::string result_;
};

struct EncodedEvent {
    std::array<std::uint8_t, kMaximumEncodedEventBytes> bytes{};
    std::uint16_t size = 0;
};

void appendByte(EncodedEvent& event, std::uint8_t value) {
    if (event.size >= event.bytes.size()) {
        throw std::length_error("Semantic trace event exceeds its fixed encoding bound");
    }
    event.bytes[event.size++] = value;
}

void appendLittleEndian(EncodedEvent& event, std::uint64_t value, std::size_t width) {
    for (std::size_t byte = 0; byte < width; byte++) {
        appendByte(event, static_cast<std::uint8_t>(value >> (byte * 8U)));
    }
}

std::size_t fieldWidth(TraceFieldType type) {
    switch (type) {
        case TraceFieldType::Signed32:
        case TraceFieldType::Unsigned32:
        case TraceFieldType::Float32:
            return 4;
        case TraceFieldType::Signed64:
        case TraceFieldType::Unsigned64:
        case TraceFieldType::Float64:
            return 8;
        case TraceFieldType::Boolean:
            return 1;
    }
    throw std::logic_error("Unknown semantic trace field type");
}

EncodedEvent encodeEvent(TraceChannel channel,
        SemanticEventKind kind,
        std::initializer_list<TraceField> fields,
        SemanticTraceMode mode) {
    if (fields.size() > kMaximumFieldsPerEvent) {
        throw std::length_error("Semantic trace event has too many fields");
    }
    EncodedEvent event;
    appendByte(event, kTraceEncodingMagic);
    appendByte(event, mode == SemanticTraceMode::Exact ? kTraceEncodingVersion : kDiscreteTraceEncodingVersion);
    appendByte(event, static_cast<std::uint8_t>(channel));
    appendLittleEndian(event, static_cast<std::uint16_t>(kind), 2);
    std::size_t includedFieldCount = 0;
    for (const TraceField field : fields) {
        if (mode == SemanticTraceMode::Exact
                || (field.type != TraceFieldType::Float32
                        && field.type != TraceFieldType::Float64)) {
            includedFieldCount++;
        }
    }
    appendByte(event, static_cast<std::uint8_t>(includedFieldCount));
    for (const TraceField field : fields) {
        if (mode == SemanticTraceMode::DiscreteOnly
                && (field.type == TraceFieldType::Float32
                        || field.type == TraceFieldType::Float64)) {
            continue;
        }
        appendByte(event, static_cast<std::uint8_t>(field.type));
        appendLittleEndian(event, field.bits, fieldWidth(field.type));
    }
    return event;
}

std::string digestEvent(const EncodedEvent& event) {
    StreamingSha256 digest;
    digest.update(event.bytes.data(), event.size);
    // Per-event hashes are diagnostics only; the complete stream keeps its
    // full SHA-256 gate. Sixty-four bits keeps the bounded divergence window
    // compact without weakening the authoritative stream comparison.
    return digest.finish().substr(0, 16);
}

std::string describeEvent(SemanticEventKind kind,
        std::initializer_list<TraceField> fields,
        SemanticTraceMode mode) {
    std::ostringstream out;
    out << semanticEventKindName(kind) << '(';
    std::size_t index = 0;
    for (const TraceField field : fields) {
        if (mode == SemanticTraceMode::DiscreteOnly
                && (field.type == TraceFieldType::Float32
                        || field.type == TraceFieldType::Float64)) {
            continue;
        }
        if (index++ != 0) {
            out << ',';
        }
        out << static_cast<unsigned int>(field.type) << ":0x"
            << std::hex << std::setw(fieldWidth(field.type) * 2)
            << std::setfill('0') << field.bits;
    }
    out << ')';
    return out.str();
}

struct ChannelState {
    StreamingSha256 digest;
    std::uint64_t eventCount = 0;
    std::array<EncodedEvent, SemanticTraceSink::kMaximumCapturedEventsPerChannel> captured{};
    std::array<std::string, SemanticTraceSink::kMaximumCapturedEventsPerChannel> descriptions{};
    std::size_t capturedCount = 0;
    bool truncated = false;
};

thread_local SemanticTraceSink* activeSink = nullptr;

ChannelState& channelState(std::array<ChannelState, 3>& channels,
        TraceChannel channel) {
    const auto index = static_cast<std::size_t>(channel) - 1U;
    if (index >= channels.size()) {
        throw std::logic_error("Unknown semantic trace channel");
    }
    return channels[index];
}

TraceChannelResult finishChannel(ChannelState& state) {
    TraceChannelResult result;
    result.eventCount = state.eventCount;
    result.digest = state.digest.finish();
    result.diagnosticWindowTruncated = state.truncated;
    result.capturedEventDigests.reserve(state.capturedCount);
    result.capturedEventDescriptions.reserve(state.capturedCount);
    for (std::size_t index = 0; index < state.capturedCount; index++) {
        result.capturedEventDigests.push_back(digestEvent(state.captured[index]));
        result.capturedEventDescriptions.push_back(state.descriptions[index]);
    }
    return result;
}

} // namespace

struct SemanticTraceSink::Impl {
    explicit Impl(SemanticTraceMode traceMode)
            : mode(traceMode) {
    }

    std::array<ChannelState, 3> channels;
    bool finished = false;
    SemanticTraceResult result;
    SemanticTraceMode mode;
};

TraceField traceI32(std::int32_t value) noexcept {
    return {TraceFieldType::Signed32, static_cast<std::uint32_t>(value)};
}

TraceField traceU32(std::uint32_t value) noexcept {
    return {TraceFieldType::Unsigned32, value};
}

TraceField traceI64(std::int64_t value) noexcept {
    return {TraceFieldType::Signed64, static_cast<std::uint64_t>(value)};
}

TraceField traceU64(std::uint64_t value) noexcept {
    return {TraceFieldType::Unsigned64, value};
}

TraceField traceF32(float value) noexcept {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return {TraceFieldType::Float32, bits};
}

TraceField traceF64(double value) noexcept {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return {TraceFieldType::Float64, bits};
}

TraceField traceBool(bool value) noexcept {
    return {TraceFieldType::Boolean, value ? 1U : 0U};
}

SemanticTraceSink::SemanticTraceSink(SemanticTraceMode mode)
        : impl_(std::make_unique<Impl>(mode)) {
}

SemanticTraceSink::~SemanticTraceSink() = default;
SemanticTraceSink::SemanticTraceSink(SemanticTraceSink&&) noexcept = default;
SemanticTraceSink& SemanticTraceSink::operator=(SemanticTraceSink&&) noexcept = default;

SemanticTraceResult SemanticTraceSink::finish() {
    if (!impl_->finished) {
        impl_->result.sweep = finishChannel(impl_->channels[0]);
        impl_->result.endpoint = finishChannel(impl_->channels[1]);
        impl_->result.apply = finishChannel(impl_->channels[2]);
        impl_->finished = true;
    }
    return impl_->result;
}

void SemanticTraceSink::record(TraceChannel channel,
        SemanticEventKind kind,
        std::initializer_list<TraceField> fields) {
    if (impl_->finished) {
        throw std::logic_error("Semantic trace sink cannot record after finish");
    }
    EncodedEvent event = encodeEvent(channel, kind, fields, impl_->mode);
    ChannelState& state = channelState(impl_->channels, channel);
    state.digest.update(event.bytes.data(), event.size);
    state.eventCount++;
    if (state.capturedCount < state.captured.size()) {
        const std::size_t index = state.capturedCount++;
        state.captured[index] = event;
        state.descriptions[index] = describeEvent(kind, fields, impl_->mode);
    } else {
        state.truncated = true;
    }
}

ScopedSemanticTrace::ScopedSemanticTrace(SemanticTraceSink& sink) noexcept
        : previous_(activeSink) {
    activeSink = &sink;
}

ScopedSemanticTrace::~ScopedSemanticTrace() {
    activeSink = previous_;
}

void emitSemanticTrace(TraceChannel channel,
        SemanticEventKind kind,
        std::initializer_list<TraceField> fields) {
    if (activeSink != nullptr) {
        activeSink->record(channel, kind, fields);
    }
}

const char* traceChannelName(TraceChannel channel) noexcept {
    switch (channel) {
        case TraceChannel::Sweep:
            return "sweep";
        case TraceChannel::Endpoint:
            return "endpoint";
        case TraceChannel::Apply:
            return "apply";
    }
    return "unknown";
}

const char* semanticEventKindName(SemanticEventKind kind) noexcept {
    switch (kind) {
        case SemanticEventKind::SweepPair:
            return "SweepPair";
        case SemanticEventKind::EndpointPairSkipped:
            return "EndpointPairSkipped";
        case SemanticEventKind::EndpointAdmission:
            return "EndpointAdmission";
        case SemanticEventKind::EndpointFertilizationTarget:
            return "EndpointFertilizationTarget";
        case SemanticEventKind::EndpointContainmentDeath:
            return "EndpointContainmentDeath";
        case SemanticEventKind::EndpointCleanup:
            return "EndpointCleanup";
        case SemanticEventKind::ApplyIntentBegin:
            return "ApplyIntentBegin";
        case SemanticEventKind::ApplyTypedHandler:
            return "ApplyTypedHandler";
        case SemanticEventKind::ApplyIntentResult:
            return "ApplyIntentResult";
        case SemanticEventKind::RandomLong:
            return "RandomLong";
        case SemanticEventKind::VirocyteOutcome:
            return "VirocyteOutcome";
        case SemanticEventKind::FertilizationResolve:
            return "FertilizationResolve";
        case SemanticEventKind::FertilizationApply:
            return "FertilizationApply";
        case SemanticEventKind::CiliocyteQueued:
            return "CiliocyteQueued";
        case SemanticEventKind::CiliocyteApplied:
            return "CiliocyteApplied";
        case SemanticEventKind::TopologyCellLink:
            return "TopologyCellLink";
        case SemanticEventKind::TopologyBoundaryLink:
            return "TopologyBoundaryLink";
        case SemanticEventKind::TopologyCellRemove:
            return "TopologyCellRemove";
        case SemanticEventKind::TopologyBoundaryRemove:
            return "TopologyBoundaryRemove";
        case SemanticEventKind::TopologyMoveCell:
            return "TopologyMoveCell";
        case SemanticEventKind::CellAllocate:
            return "CellAllocate";
        case SemanticEventKind::CellCopy:
            return "CellCopy";
        case SemanticEventKind::CellRelease:
            return "CellRelease";
        case SemanticEventKind::CellDeath:
            return "CellDeath";
        case SemanticEventKind::CellSplit:
            return "CellSplit";
        case SemanticEventKind::ContactCacheFreshness:
            return "ContactCacheFreshness";
    }
    return "UnknownEvent";
}

} // namespace cellularity::verification

#endif
