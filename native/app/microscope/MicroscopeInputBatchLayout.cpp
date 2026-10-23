#include "app/microscope/MicroscopeInputBatchLayout.h"

#include <cmath>
#include <cstring>

namespace cellularity::microscope {
namespace {

std::uint16_t readU16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0])
            | static_cast<std::uint16_t>(bytes[1]) << 8U;
}

std::uint32_t readU32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0])
            | static_cast<std::uint32_t>(bytes[1]) << 8U
            | static_cast<std::uint32_t>(bytes[2]) << 16U
            | static_cast<std::uint32_t>(bytes[3]) << 24U;
}

std::uint64_t readU64(const std::uint8_t* bytes) noexcept {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::int32_t readI32(const std::uint8_t* bytes) noexcept {
    const std::uint32_t bits = readU32(bytes);
    std::int32_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::int64_t readI64(const std::uint8_t* bytes) noexcept {
    const std::uint64_t bits = readU64(bytes);
    std::int64_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

float readFloat(const std::uint8_t* bytes) noexcept {
    const std::uint32_t bits = readU32(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void writeU16(std::uint8_t* bytes, std::uint16_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(std::uint8_t* bytes, std::uint32_t value) noexcept {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void writeU64(std::uint8_t* bytes, std::uint64_t value) noexcept {
    for (unsigned index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

bool canonicalZero(const std::uint8_t* bytes) noexcept {
    return readU32(bytes) == 0;
}

bool finite(float value) noexcept {
    return std::isfinite(value);
}

bool validateCommand(
        const std::uint8_t* bytes,
        MicroscopeInputCommandRecord& command) noexcept {
    if (readU16(bytes + 2) != 0 || readU64(bytes + 24) != 0) {
        return false;
    }
    const std::uint16_t kindWire = readU16(bytes);
    if (kindWire < static_cast<std::uint16_t>(MicroscopeInputCommandKind::Down)
            || kindWire
                    > static_cast<std::uint16_t>(
                            MicroscopeInputCommandKind::ZoomEnded)) {
        return false;
    }
    command.kind = static_cast<MicroscopeInputCommandKind>(kindWire);
    command.pointerId = readI32(bytes + 4);
    for (std::size_t index = 0; index < command.values.size(); ++index) {
        command.values[index] = readFloat(bytes + 8 + index * sizeof(float));
    }

    const auto usedValuesAreFinite = [&](std::size_t count) noexcept {
        for (std::size_t index = 0; index < count; ++index) {
            if (!finite(command.values[index])) {
                return false;
            }
        }
        for (std::size_t index = count; index < command.values.size(); ++index) {
            if (!canonicalZero(bytes + 8 + index * sizeof(float))) {
                return false;
            }
        }
        return true;
    };

    switch (command.kind) {
        case MicroscopeInputCommandKind::Down:
        case MicroscopeInputCommandKind::Move:
            return command.pointerId >= 0 && command.pointerId <= 31
                    && usedValuesAreFinite(2);
        case MicroscopeInputCommandKind::Up:
            return command.pointerId >= 0 && command.pointerId <= 31
                    && usedValuesAreFinite(0);
        case MicroscopeInputCommandKind::Pan:
        case MicroscopeInputCommandKind::Fling:
        case MicroscopeInputCommandKind::Tap:
            return command.pointerId == 0 && usedValuesAreFinite(2);
        case MicroscopeInputCommandKind::Zoom:
            return command.pointerId == 0 && usedValuesAreFinite(3)
                    && command.values[0] > 0.0F;
        case MicroscopeInputCommandKind::ZoomEnded:
            return command.pointerId == 0 && usedValuesAreFinite(0);
    }
    return false;
}

} // namespace

bool decodeMicroscopeInputBatch(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        MicroscopeInputBatch& batch) noexcept {
    batch = {};
    if (bytes == nullptr || byteCount < kMicroscopeInputHeaderSize
            || readU32(bytes) != kMicroscopeInputBatchMagic
            || readU16(bytes + 4) != kMicroscopeInputBatchVersion
            || readU16(bytes + 6) != kMicroscopeInputHeaderSize
            || readU16(bytes + 8) != kMicroscopeInputCommandRecordSize
            || readU32(bytes + 12) != 0) {
        return false;
    }
    const std::size_t commandCount = readU16(bytes + 10);
    if (commandCount == 0 || commandCount > kMicroscopeInputCommandCapacity) {
        return false;
    }
    const std::size_t requiredBytes = kMicroscopeInputHeaderSize
            + commandCount * kMicroscopeInputCommandRecordSize;
    if (requiredBytes > byteCount) {
        return false;
    }

    batch.sequence = readU64(bytes + 16);
    batch.eventTimeNanos = readI64(bytes + 24);
    batch.expectedInputEpoch = readU64(bytes + 32);
    batch.profilingIngressTimestampNanos = readI64(bytes + 40);
    if (batch.sequence == 0 || batch.eventTimeNanos < 0
            || batch.expectedInputEpoch == 0) {
        return false;
    }

    for (std::size_t index = 0; index < commandCount; ++index) {
        const std::uint8_t* record = bytes + kMicroscopeInputHeaderSize
                + index * kMicroscopeInputCommandRecordSize;
        if (!validateCommand(record, batch.commands[index])) {
            batch = {};
            return false;
        }
        if (batch.commands[index].kind == MicroscopeInputCommandKind::Tap) {
            ++batch.tapCount;
            if (batch.tapCount > 1) {
                batch = {};
                return false;
            }
        }
    }
    batch.commandCount = commandCount;
    return true;
}

std::uint64_t readMicroscopeInputBatchSequence(
        const std::uint8_t* bytes, std::size_t byteCount) noexcept {
    constexpr std::size_t kSequenceEnd = 24;
    return bytes != nullptr && byteCount >= kSequenceEnd
            ? readU64(bytes + 16)
            : 0;
}

void initializeMicroscopeInputBatchResult(
        MicroscopeInputBatchResult& result,
        std::uint64_t batchSequence,
        std::uint64_t inputEpoch,
        MicroscopeInputIngressStatus status) noexcept {
    result = {};
    result.batchSequence = batchSequence;
    result.inputEpoch = inputEpoch;
    result.ingressStatus = status;
}

void markMicroscopeInputBatchInternalFailure(
        MicroscopeInputBatchResult& result,
        bool dispatchStarted) noexcept {
    result.ingressStatus = MicroscopeInputIngressStatus::InternalFailure;
    result.aggregateFlags = 0;
    result.viewportRevision = 0;
    result.presentationMotionRevision = 0;
    result.zoomDisplaySizeBits = 0;
    if (!dispatchStarted) {
        result.commandCount = 0;
        result.commandStatuses.fill(MicroscopeInputCommandWireStatus::Unused);
    }
}

void writeMicroscopeInputBatchResult(
        std::uint8_t* destination,
        const MicroscopeInputBatchResult& result) noexcept {
    if (destination == nullptr) {
        return;
    }
    std::memset(destination, 0, kMicroscopeInputResultBufferCapacity);
    writeU32(destination, kMicroscopeInputResultMagic);
    writeU16(destination + 4, kMicroscopeInputBatchVersion);
    writeU16(destination + 6,
            static_cast<std::uint16_t>(kMicroscopeInputResultHeaderSize));
    writeU16(destination + 8,
            static_cast<std::uint16_t>(kMicroscopeInputResultStatusSize));
    writeU16(destination + 10,
            static_cast<std::uint16_t>(result.commandCount));
    writeU32(destination + 12, result.aggregateFlags);
    writeU64(destination + 16, result.batchSequence);
    writeU64(destination + 24, result.inputEpoch);
    writeU64(destination + 32, result.viewportRevision);
    writeU64(destination + 40, result.presentationMotionRevision);
    writeU32(destination + 48, result.zoomDisplaySizeBits);
    writeU32(destination + 52,
            static_cast<std::uint32_t>(result.ingressStatus));
    for (std::size_t index = 0;
            index < result.commandCount
            && index < kMicroscopeInputCommandCapacity;
            ++index) {
        writeU32(destination + kMicroscopeInputResultHeaderSize
                        + index * kMicroscopeInputResultStatusSize,
                static_cast<std::uint32_t>(result.commandStatuses[index]));
    }
}

void writePresentationMotionSnapshot(
        std::uint8_t* destination,
        const PresentationMotionSnapshot& snapshot) noexcept {
    if (destination == nullptr) {
        return;
    }
    std::memset(destination, 0, kPresentationMotionSnapshotSize);
    writeU32(destination, kPresentationMotionSnapshotMagic);
    writeU16(destination + 4, kPresentationMotionSnapshotVersion);
    writeU16(destination + 6,
            static_cast<std::uint16_t>(kPresentationMotionSnapshotSize));
    writeU64(destination + 8, snapshot.revision);
    writeU64(destination + 16, snapshot.cameraRevision);
    std::uint32_t flags = 0;
    if (snapshot.inertiaActive) {
        flags |= kPresentationMotionInertiaActive;
    }
    if (snapshot.cameraMotionActive) {
        flags |= kPresentationMotionCameraActive;
    }
    writeU32(destination + 24, flags);
}

} // namespace cellularity::microscope
