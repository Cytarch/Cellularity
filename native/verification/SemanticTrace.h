#pragma once

#if defined(CELLULARITY_SEMANTIC_TRACE)

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace cellularity::verification {

enum class TraceChannel : std::uint8_t {
    Sweep = 1,
    Endpoint = 2,
    Apply = 3,
};

enum class SemanticEventKind : std::uint16_t {
    SweepPair = 1,

    EndpointPairSkipped = 100,
    EndpointAdmission = 101,
    EndpointFertilizationTarget = 102,
    EndpointContainmentDeath = 103,
    EndpointCleanup = 104,

    ApplyIntentBegin = 200,
    ApplyTypedHandler = 201,
    ApplyIntentResult = 202,
    RandomLong = 203,
    VirocyteOutcome = 204,
    FertilizationResolve = 205,
    FertilizationApply = 206,
    CiliocyteQueued = 207,
    CiliocyteApplied = 208,
    TopologyCellLink = 209,
    TopologyBoundaryLink = 210,
    TopologyCellRemove = 211,
    TopologyBoundaryRemove = 212,
    TopologyMoveCell = 213,
    CellAllocate = 214,
    CellCopy = 215,
    CellRelease = 216,
    CellDeath = 217,
    CellSplit = 218,
    ContactCacheFreshness = 219,
};

enum class TraceFieldType : std::uint8_t {
    Signed32 = 1,
    Unsigned32 = 2,
    Signed64 = 3,
    Unsigned64 = 4,
    Float32 = 5,
    Float64 = 6,
    Boolean = 7,
};

struct TraceField {
    TraceFieldType type;
    std::uint64_t bits;
};

[[nodiscard]] TraceField traceI32(std::int32_t value) noexcept;
[[nodiscard]] TraceField traceU32(std::uint32_t value) noexcept;
[[nodiscard]] TraceField traceI64(std::int64_t value) noexcept;
[[nodiscard]] TraceField traceU64(std::uint64_t value) noexcept;
[[nodiscard]] TraceField traceF32(float value) noexcept;
[[nodiscard]] TraceField traceF64(double value) noexcept;
[[nodiscard]] TraceField traceBool(bool value) noexcept;

struct TraceChannelResult {
    std::uint64_t eventCount = 0;
    std::string digest;
    std::vector<std::string> capturedEventDigests;
    std::vector<std::string> capturedEventDescriptions;
    bool diagnosticWindowTruncated = false;
};

struct SemanticTraceResult {
    TraceChannelResult sweep;
    TraceChannelResult endpoint;
    TraceChannelResult apply;
};

enum class SemanticTraceMode : std::uint8_t {
    Exact = 0,
    DiscreteOnly = 1,
};

class SemanticTraceSink {
public:
    static constexpr std::size_t kMaximumCapturedEventsPerChannel = 512;

    explicit SemanticTraceSink(
            SemanticTraceMode mode = SemanticTraceMode::Exact);
    ~SemanticTraceSink();
    SemanticTraceSink(SemanticTraceSink&&) noexcept;
    SemanticTraceSink& operator=(SemanticTraceSink&&) noexcept;
    SemanticTraceSink(const SemanticTraceSink&) = delete;
    SemanticTraceSink& operator=(const SemanticTraceSink&) = delete;

    [[nodiscard]] SemanticTraceResult finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void record(TraceChannel channel,
            SemanticEventKind kind,
            std::initializer_list<TraceField> fields);
    friend void emitSemanticTrace(TraceChannel,
            SemanticEventKind,
            std::initializer_list<TraceField>);
};

class ScopedSemanticTrace final {
public:
    explicit ScopedSemanticTrace(SemanticTraceSink& sink) noexcept;
    ~ScopedSemanticTrace();
    ScopedSemanticTrace(const ScopedSemanticTrace&) = delete;
    ScopedSemanticTrace& operator=(const ScopedSemanticTrace&) = delete;

private:
    SemanticTraceSink* previous_;
};

void emitSemanticTrace(TraceChannel channel,
        SemanticEventKind kind,
        std::initializer_list<TraceField> fields);

[[nodiscard]] const char* traceChannelName(TraceChannel channel) noexcept;
[[nodiscard]] const char* semanticEventKindName(SemanticEventKind kind) noexcept;

} // namespace cellularity::verification

#define CELLULARITY_SEMANTIC_EVENT(channel, kind, ...) \
    ::cellularity::verification::emitSemanticTrace(    \
            (channel), (kind), {__VA_ARGS__})

#else

#define CELLULARITY_SEMANTIC_EVENT(...) ((void)0)

#endif
