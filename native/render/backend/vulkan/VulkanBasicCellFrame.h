#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "render/frame/PreparedRenderFrameView.h"

namespace cellularity::render::vulkan {

enum class VulkanBasicCellProgram : std::uint8_t {
    Buoyocyte = 0,
    Gamete = 1,
    Keratinocyte = 2,
    Lipocyte = 3,
    Myocyte = 4,
    Neurocyte = 5,
    Phagocyte = 6,
    Photocyte = 7,
    Secrocyte = 8,
    Stemocyte = 9,
    Count = 10
};

inline constexpr std::size_t kVulkanBasicCellProgramCount =
        static_cast<std::size_t>(VulkanBasicCellProgram::Count);

enum class VulkanWorldProgram : std::uint8_t {
    Arrow = 0,
    CiliocyteOutline = 1,
    Dead = 2,
    DevorocyteOutline = 3,
    FlagellocyteOutline = 4,
    Food = 5,
    GlueocyteOutline = 6,
    Link = 7,
    Marker = 8,
    NitrocyteOutline = 9,
    PixelText = 10,
    SenseocyteOutline = 11,
    StereocyteOutline = 12,
    VirocyteOutline = 13,
    GenomeGrid = 14,
    GenomeTree = 15,
    Count = 16,
};

inline constexpr std::size_t kVulkanWorldProgramCount =
        static_cast<std::size_t>(VulkanWorldProgram::Count);

struct VulkanPreparedFrameUploadIdentity final {
    std::uintptr_t frameAddress = 0;
    std::uint32_t layoutVersion = 0;
    std::uint64_t worldEpoch = 0;
    std::uint64_t modeGeneration = 0;
    std::uint64_t captureSequence = 0;
    std::uint64_t geometryRevision = 0;
    std::uint64_t instanceRevision = 0;
    std::uint64_t markerRevision = 0;

    [[nodiscard]] static VulkanPreparedFrameUploadIdentity from(
            const PreparedRenderFrameView& frame) noexcept;

    friend bool operator==(const VulkanPreparedFrameUploadIdentity& left,
            const VulkanPreparedFrameUploadIdentity& right) noexcept {
        return left.frameAddress == right.frameAddress
                && left.layoutVersion == right.layoutVersion
                && left.worldEpoch == right.worldEpoch
                && left.modeGeneration == right.modeGeneration
                && left.captureSequence == right.captureSequence
                && left.geometryRevision == right.geometryRevision
                && left.instanceRevision == right.instanceRevision
                && left.markerRevision == right.markerRevision;
    }

    friend bool operator!=(const VulkanPreparedFrameUploadIdentity& left,
            const VulkanPreparedFrameUploadIdentity& right) noexcept {
        return !(left == right);
    }
};

struct VulkanBasicCellStreamRange final {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct VulkanBasicCellDraw final {
    VulkanBasicCellProgram program = VulkanBasicCellProgram::Phagocyte;
    std::uint32_t cellTypeIndex = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
};

struct VulkanIndexedWorldDraw final {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
};

struct VulkanOutlineDraw final {
    VulkanWorldProgram program = VulkanWorldProgram::FlagellocyteOutline;
    std::uint32_t cellTypeIndex = 0;
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
};

struct VulkanLinearWorldDraw final {
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
};

struct VulkanBasicCellUploadPlan final {
    VulkanPreparedFrameUploadIdentity identity{};
    VulkanBasicCellStreamRange positions{};
    VulkanBasicCellStreamRange textureCoordinates{};
    VulkanBasicCellStreamRange centers{};
    VulkanBasicCellStreamRange colors{};
    VulkanBasicCellStreamRange indices{};
    VulkanBasicCellStreamRange foodInstances{};
    VulkanBasicCellStreamRange foodAttributes{};
    VulkanBasicCellStreamRange deadInstances{};
    VulkanBasicCellStreamRange deadAttributes{};
    VulkanBasicCellStreamRange linkVertices{};
    VulkanBasicCellStreamRange linkPatternPositions{};
    VulkanBasicCellStreamRange markerInstances{};
    std::uint64_t requiredBytes = 0;
    std::array<VulkanBasicCellDraw,
            CellTypes::length * kMaximumDrawBunches>
            draws{};
    std::size_t drawCount = 0;
    std::array<VulkanIndexedWorldDraw, kMaximumDrawBunches> arrowDraws{};
    std::size_t arrowDrawCount = 0;
    std::array<VulkanOutlineDraw,
            CellTypes::length * kMaximumDrawBunches>
            outlineDraws{};
    std::size_t outlineDrawCount = 0;
    std::array<VulkanLinearWorldDraw, kMaximumDrawBunches> linkDraws{};
    std::size_t linkDrawCount = 0;
    std::uint32_t foodInstanceCount = 0;
    std::uint32_t deadInstanceCount = 0;
    std::uint32_t markerInstanceCount = 0;
    bool previewMode = false;
};

[[nodiscard]] const char* vulkanBasicCellProgramName(
        VulkanBasicCellProgram program) noexcept;

[[nodiscard]] VulkanBasicCellProgram vulkanBasicCellProgramForType(
        int cellTypeIndex);

[[nodiscard]] const char* vulkanWorldProgramName(
        VulkanWorldProgram program) noexcept;

[[nodiscard]] VulkanWorldProgram vulkanOutlineProgramForType(
        int cellTypeIndex);

// Converts the immutable renderer payload into exact Vulkan byte ranges and
// indexed draw commands. The conversion does not copy geometry and performs
// only bounded metadata validation; the backend copies the returned ranges
// directly from the source vectors while it owns the published frame lease.
[[nodiscard]] bool buildVulkanBasicCellUploadPlan(
        const PreparedRenderFrameView& frame, VulkanBasicCellUploadPlan& output,
        std::string& error);

[[nodiscard]] inline bool buildVulkanBasicCellUploadPlan(
        const PreparedRenderFrame& frame, VulkanBasicCellUploadPlan& output,
        std::string& error) {
    return buildVulkanBasicCellUploadPlan(
            makePreparedRenderFrameView(frame), output, error);
}

// Deterministic frame used by the isolated Vulkan surface until production
// runtime backend selection is introduced in V-15. It exercises every basic
// cell type through the same PreparedRenderFrame consumer used by live frames.
[[nodiscard]] PreparedRenderFrame makeVulkanBasicCellDemoFrame();

[[nodiscard]] RenderPresentationState makeVulkanBasicCellDemoPresentation(
        int surfaceWidth, int surfaceHeight);

} // namespace cellularity::render::vulkan
