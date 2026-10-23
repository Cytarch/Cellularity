#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "render/NativeRenderTextureLoader.h"
#include "render/backend/vulkan/VulkanBasicCellFrame.h"
#include "render/backend/vulkan/VulkanResourceFoundation.h"
#include "render/text/PixelTextRenderer.h"

namespace cellularity::render::vulkan {

inline constexpr std::size_t kVulkanGpuPassCount = 10;

/** Non-owning writer for the fixed microscope pass timestamp sequence. */
struct VulkanGpuTimestampWriter final {
    VkQueryPool queryPool = VK_NULL_HANDLE;
    std::uint32_t nextQuery = 0;
    std::uint32_t endQuery = 0;

    void checkpoint(VkCommandBuffer commandBuffer) noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
        if (queryPool == VK_NULL_HANDLE || nextQuery >= endQuery) {
            return;
        }
        vkCmdWriteTimestamp(commandBuffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool,
                nextQuery++);
#else
        static_cast<void>(commandBuffer);
#endif
    }
};

struct VulkanCellShaderSpirv final {
    std::vector<std::uint32_t> vertex;
    std::vector<std::uint32_t> fragment;
};

struct VulkanBasicCellDrawOptions final {
    bool previewOrder = false;
    bool blendBasicCells = true;
    bool drawDirectionArrow = true;
    bool drawMarkers = true;
    bool drawLinks = true;
    bool drawFood = true;
    bool drawText = false;
};

enum class VulkanPipelineTarget : std::uint8_t {
    Surface = 0,
    Offscreen = 1,
    Count = 2
};

class VulkanBasicCellRenderer final {
public:
    static constexpr std::size_t kGeometrySlotCount = 2;
    static constexpr std::size_t kTextGeometrySlotCount = 2;
    static constexpr std::size_t kFrameDescriptorCount = 2;

    VulkanBasicCellRenderer() = default;
    ~VulkanBasicCellRenderer() = default;

    VulkanBasicCellRenderer(const VulkanBasicCellRenderer&) = delete;
    VulkanBasicCellRenderer& operator=(const VulkanBasicCellRenderer&) = delete;

    [[nodiscard]] bool initialize(
            std::shared_ptr<VulkanDeviceContext> device,
            const VulkanCommandPool& commandPool, VkQueue graphicsQueue,
            PFN_vkSetDebugUtilsObjectNameEXT setDebugObjectName,
            std::array<VulkanCellShaderSpirv,
                    kVulkanBasicCellProgramCount>
                    shaderSpirv,
            std::array<VulkanCellShaderSpirv, kVulkanWorldProgramCount>
                    worldShaderSpirv,
            NativeRenderTexturePayload dotTexture,
            NativeRenderTexturePayload besselTexture,
            std::string& error);
    void reset() noexcept;

    [[nodiscard]] bool createPipelines(
            VkRenderPass renderPass, std::string& error,
            VulkanPipelineTarget target = VulkanPipelineTarget::Surface);
    void destroyPipelines(VulkanPipelineTarget target) noexcept;
    void destroyPipelines() noexcept;

    void updateFrameDescriptor(
            std::size_t frameIndex, VkBuffer uniformBuffer) const;

    [[nodiscard]] bool needsGeometryUpload(
            const PreparedRenderFrameView& frame) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> requiredGeometryWaitSerial(
            std::uint64_t completedSubmissionSerial,
            std::uint64_t reservedSlotMask = 0) const noexcept;
    [[nodiscard]] bool uploadGeometry(const PreparedRenderFrameView& frame,
            std::uint64_t completedSubmissionSerial, std::string& error,
            std::size_t* uploadedSlot = nullptr,
            std::uint64_t reservedSlotMask = 0);
    [[nodiscard]] bool needsTextGeometryUpload(
            const PixelTextRenderer& geometry) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
    requiredTextGeometryWaitSerial(
            std::uint64_t completedSubmissionSerial) const noexcept;
    [[nodiscard]] bool uploadTextGeometry(
            const PixelTextRenderer& geometry,
            std::uint64_t completedSubmissionSerial, std::string& error);

    void record(VkCommandBuffer commandBuffer, std::size_t frameIndex,
            std::uint32_t dynamicUniformOffset, bool drawText,
            VulkanPipelineTarget target = VulkanPipelineTarget::Surface,
            VulkanGpuTimestampWriter* timestampWriter = nullptr) const;
    void recordGeometry(VkCommandBuffer commandBuffer,
            std::size_t frameIndex, std::uint32_t dynamicUniformOffset,
            std::size_t geometrySlot,
            const VulkanBasicCellDrawOptions& options,
            VulkanPipelineTarget target = VulkanPipelineTarget::Surface,
            VulkanGpuTimestampWriter* timestampWriter = nullptr) const;
    void recordGenomeGrid(VkCommandBuffer commandBuffer,
            std::size_t frameIndex, std::uint32_t dynamicUniformOffset,
            VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
            VulkanPipelineTarget target = VulkanPipelineTarget::Surface) const;
    void recordGenomeTreeLines(VkCommandBuffer commandBuffer,
            std::size_t frameIndex, std::uint32_t dynamicUniformOffset,
            VkBuffer vertexBuffer, VkDeviceSize vertexOffset,
            std::uint32_t vertexCount,
            VulkanPipelineTarget target = VulkanPipelineTarget::Surface) const;
    void markActiveGeometryUsed(std::uint64_t submissionSerial) noexcept;
    void markGeometrySlotUsed(std::size_t geometrySlot,
            std::uint64_t submissionSerial) noexcept;
    void markActiveTextGeometryUsed(
            std::uint64_t submissionSerial) noexcept;

    [[nodiscard]] bool ready(
            VulkanPipelineTarget target = VulkanPipelineTarget::Surface)
            const noexcept;
    [[nodiscard]] std::uint64_t geometryUploadCount() const noexcept;
    [[nodiscard]] std::uint64_t geometryUploadSkipCount() const noexcept;
    [[nodiscard]] std::uint64_t geometryUploadBytes() const noexcept;
    [[nodiscard]] std::uint64_t geometryBufferGrowthCount() const noexcept;
    [[nodiscard]] std::uint64_t basicCellDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t arrowDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t foodDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t linkDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t deadDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t outlineDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t markerDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t genomeGridDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t genomeTreeDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t textDrawCount() const noexcept;
    [[nodiscard]] std::uint64_t textGeometryUploadCount() const noexcept;
    [[nodiscard]] std::uint64_t textGeometryUploadSkipCount() const noexcept;
    [[nodiscard]] std::uint64_t textGeometryUploadBytes() const noexcept;
    [[nodiscard]] std::uint64_t textGeometryBufferGrowthCount() const noexcept;
    [[nodiscard]] std::uint64_t pipelineBindCount() const noexcept;
    [[nodiscard]] std::uint64_t activeCaptureSequence() const noexcept;
    [[nodiscard]] std::uint64_t activeGeometryRevision() const noexcept;
    [[nodiscard]] std::uint64_t activeInstanceRevision() const noexcept;
    [[nodiscard]] std::uint32_t dotTextureWidth() const noexcept;
    [[nodiscard]] std::uint32_t dotTextureHeight() const noexcept;
    [[nodiscard]] std::uint32_t besselTextureWidth() const noexcept;
    [[nodiscard]] std::uint32_t besselTextureHeight() const noexcept;
    [[nodiscard]] std::uint32_t glyphAtlasWidth() const noexcept;
    [[nodiscard]] std::uint32_t glyphAtlasHeight() const noexcept;

private:
    struct PipelineSet final {
        std::array<VulkanPipeline, kVulkanBasicCellProgramCount>
                blendedBasicCells;
        std::array<VulkanPipeline, kVulkanBasicCellProgramCount>
                opaqueBasicCells;
        std::array<VulkanPipeline, kVulkanWorldProgramCount> world;
    };

    struct GeometrySlot final {
        VulkanBuffer buffer;
        VulkanBasicCellUploadPlan plan{};
        std::uint64_t lastUseSerial = 0;
        bool valid = false;
    };

    struct TextGeometrySlot final {
        VulkanBuffer buffer;
        const PixelTextRenderer* source = nullptr;
        std::uint64_t revision = 0;
        std::uint64_t lastUseSerial = 0;
        std::uint32_t instanceCount = 0;
        bool valid = false;
    };

    [[nodiscard]] bool createShaderResources(
            std::array<VulkanCellShaderSpirv,
                    kVulkanBasicCellProgramCount>&& shaderSpirv,
            std::array<VulkanCellShaderSpirv,
                    kVulkanWorldProgramCount>&& worldShaderSpirv,
            std::string& error);
    [[nodiscard]] bool createTexture(
            NativeRenderTexturePayload&& texture,
            const VulkanCommandPool& commandPool, VkQueue graphicsQueue,
            const char* debugName, VulkanImage& image,
            VulkanImageView& imageView, VulkanSampler& sampler,
            std::uint32_t& width, std::uint32_t& height,
            std::string& error);
    [[nodiscard]] bool createDescriptorResources(std::string& error);
    [[nodiscard]] bool createGeometryBuffers(std::string& error);
    [[nodiscard]] bool createTextGeometryBuffers(std::string& error);
    [[nodiscard]] bool createUnitQuadBuffer(std::string& error);
    [[nodiscard]] bool ensureGeometryCapacity(
            GeometrySlot& slot, VkDeviceSize requiredBytes,
            std::size_t slotIndex, std::string& error);
    [[nodiscard]] std::optional<std::size_t> writableGeometrySlot(
            std::uint64_t completedSubmissionSerial,
            std::uint64_t reservedSlotMask) const noexcept;
    [[nodiscard]] bool ensureTextGeometryCapacity(TextGeometrySlot& slot,
            VkDeviceSize requiredBytes, std::size_t slotIndex,
            std::string& error);
    [[nodiscard]] std::optional<std::size_t> writableTextGeometrySlot(
            std::uint64_t completedSubmissionSerial) const noexcept;
    [[nodiscard]] bool createShaderModule(
            const std::vector<std::uint32_t>& spirv,
            VulkanShaderModule& output, std::string& error) const;
    [[nodiscard]] PipelineSet& pipelineSet(
            VulkanPipelineTarget target) noexcept;
    [[nodiscard]] const PipelineSet& pipelineSet(
            VulkanPipelineTarget target) const noexcept;

    template <typename Handle>
    void nameObject(VkObjectType type, Handle handle,
            const char* name) const noexcept;

    std::shared_ptr<VulkanDeviceContext> mDevice;
    PFN_vkSetDebugUtilsObjectNameEXT mSetDebugObjectName = nullptr;
    std::array<VulkanShaderModule, kVulkanBasicCellProgramCount>
            mVertexShaders;
    std::array<VulkanShaderModule, kVulkanBasicCellProgramCount>
            mFragmentShaders;
    std::array<VulkanShaderModule, kVulkanWorldProgramCount>
            mWorldVertexShaders;
    std::array<VulkanShaderModule, kVulkanWorldProgramCount>
            mWorldFragmentShaders;
    VulkanDescriptorSetLayout mDescriptorSetLayout;
    VulkanDescriptorPool mDescriptorPool;
    std::array<VulkanDescriptorSet, kFrameDescriptorCount> mDescriptorSets;
    VulkanPipelineLayout mPipelineLayout;
    std::array<PipelineSet,
            static_cast<std::size_t>(VulkanPipelineTarget::Count)>
            mPipelineSets;
    VulkanImage mDotTextureImage;
    VulkanImageView mDotTextureView;
    VulkanSampler mDotTextureSampler;
    VulkanImage mBesselTextureImage;
    VulkanImageView mBesselTextureView;
    VulkanSampler mBesselTextureSampler;
    VulkanImage mGlyphAtlasImage;
    VulkanImageView mGlyphAtlasView;
    VulkanSampler mGlyphAtlasSampler;
    VulkanBuffer mUnitQuadBuffer;
    std::array<GeometrySlot, kGeometrySlotCount> mGeometrySlots;
    std::array<TextGeometrySlot, kTextGeometrySlotCount> mTextGeometrySlots;
    std::size_t mActiveGeometrySlot = 0;
    std::size_t mActiveTextGeometrySlot = 0;
    bool mHasActiveGeometry = false;
    bool mHasActiveTextGeometry = false;
    std::uint32_t mDotTextureWidth = 0;
    std::uint32_t mDotTextureHeight = 0;
    std::uint32_t mBesselTextureWidth = 0;
    std::uint32_t mBesselTextureHeight = 0;
    std::uint32_t mGlyphAtlasWidth = 0;
    std::uint32_t mGlyphAtlasHeight = 0;
    std::uint64_t mGeometryUploadCount = 0;
    mutable std::uint64_t mGeometryUploadSkipCount = 0;
    std::uint64_t mGeometryUploadBytes = 0;
    std::uint64_t mGeometryBufferGrowthCount = 0;
    std::uint64_t mTextGeometryUploadCount = 0;
    mutable std::uint64_t mTextGeometryUploadSkipCount = 0;
    std::uint64_t mTextGeometryUploadBytes = 0;
    std::uint64_t mTextGeometryBufferGrowthCount = 0;
    mutable std::uint64_t mBasicCellDrawCount = 0;
    mutable std::uint64_t mArrowDrawCount = 0;
    mutable std::uint64_t mFoodDrawCount = 0;
    mutable std::uint64_t mLinkDrawCount = 0;
    mutable std::uint64_t mDeadDrawCount = 0;
    mutable std::uint64_t mOutlineDrawCount = 0;
    mutable std::uint64_t mMarkerDrawCount = 0;
    mutable std::uint64_t mGenomeGridDrawCount = 0;
    mutable std::uint64_t mGenomeTreeDrawCount = 0;
    mutable std::uint64_t mTextDrawCount = 0;
    mutable std::uint64_t mPipelineBindCount = 0;
};

} // namespace cellularity::render::vulkan
