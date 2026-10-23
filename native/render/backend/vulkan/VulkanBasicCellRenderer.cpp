#include "render/backend/vulkan/VulkanBasicCellRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace cellularity::render::vulkan {
namespace {

constexpr VkDeviceSize kInitialGeometryBufferBytes = 256 * 1024;
constexpr VkDeviceSize kInitialTextGeometryBufferBytes = 64 * 1024;
constexpr VkDeviceSize kCellUniformBytes = 96;

NativeRenderTexturePayload glyphAtlasPayload() {
    NativeRenderTexturePayload payload;
    payload.width = PixelTextRenderer::kAtlasWidth;
    payload.height = PixelTextRenderer::kAtlasHeight;
    payload.pixelFormat = NativeRenderTexturePixelFormat::R8;
    payload.minFilter = NativeRenderTextureFilter::Nearest;
    payload.magFilter = NativeRenderTextureFilter::Nearest;
    payload.wrapS = NativeRenderTextureWrap::ClampToEdge;
    payload.wrapT = NativeRenderTextureWrap::ClampToEdge;
    const auto& atlas = PixelTextRenderer::glyphAtlas();
    payload.pixels.assign(atlas.begin(), atlas.end());
    return payload;
}

template <typename Handle>
std::uint64_t handleValue(Handle handle) noexcept {
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

bool validSpirv(const std::vector<std::uint32_t>& words) noexcept {
    constexpr std::uint32_t kSpirvMagic = 0x07230203U;
    return !words.empty() && words.front() == kSpirvMagic;
}

} // namespace

bool VulkanBasicCellRenderer::initialize(
        std::shared_ptr<VulkanDeviceContext> device,
        const VulkanCommandPool& commandPool, VkQueue graphicsQueue,
        PFN_vkSetDebugUtilsObjectNameEXT setDebugObjectName,
        std::array<VulkanCellShaderSpirv, kVulkanBasicCellProgramCount>
                shaderSpirv,
        std::array<VulkanCellShaderSpirv, kVulkanWorldProgramCount>
                worldShaderSpirv,
        NativeRenderTexturePayload dotTexture,
        NativeRenderTexturePayload besselTexture,
        std::string& error) {
    reset();
    error.clear();
    if (!device || commandPool.get() == VK_NULL_HANDLE
            || graphicsQueue == VK_NULL_HANDLE) {
        error = "Vulkan basic-cell renderer requires a device, command pool, and queue";
        return false;
    }
    mDevice = std::move(device);
    mSetDebugObjectName = setDebugObjectName;
    if (!createShaderResources(std::move(shaderSpirv),
                std::move(worldShaderSpirv), error)
            || !createTexture(std::move(dotTexture), commandPool,
                    graphicsQueue, "small-dots", mDotTextureImage,
                    mDotTextureView, mDotTextureSampler, mDotTextureWidth,
                    mDotTextureHeight, error)
            || !createTexture(std::move(besselTexture), commandPool,
                    graphicsQueue, "Bessel", mBesselTextureImage,
                    mBesselTextureView, mBesselTextureSampler,
                    mBesselTextureWidth, mBesselTextureHeight, error)
            || !createTexture(glyphAtlasPayload(), commandPool,
                    graphicsQueue, "pixel glyph atlas", mGlyphAtlasImage,
                    mGlyphAtlasView, mGlyphAtlasSampler, mGlyphAtlasWidth,
                    mGlyphAtlasHeight, error)
            || !createDescriptorResources(error)
            || !createGeometryBuffers(error)
            || !createTextGeometryBuffers(error)
            || !createUnitQuadBuffer(error)) {
        reset();
        return false;
    }
    return true;
}

void VulkanBasicCellRenderer::reset() noexcept {
    destroyPipelines();
    for (GeometrySlot& slot : mGeometrySlots) {
        slot.buffer.reset();
        slot.plan = {};
        slot.lastUseSerial = 0;
        slot.valid = false;
    }
    for (TextGeometrySlot& slot : mTextGeometrySlots) {
        slot.buffer.reset();
        slot.source = nullptr;
        slot.revision = 0;
        slot.lastUseSerial = 0;
        slot.instanceCount = 0;
        slot.valid = false;
    }
    mHasActiveGeometry = false;
    mHasActiveTextGeometry = false;
    mActiveGeometrySlot = 0;
    mActiveTextGeometrySlot = 0;
    mPipelineLayout.reset();
    for (VulkanDescriptorSet& descriptorSet : mDescriptorSets) {
        descriptorSet.reset();
    }
    mDescriptorPool.reset();
    mDescriptorSetLayout.reset();
    mUnitQuadBuffer.reset();
    mGlyphAtlasSampler.reset();
    mGlyphAtlasView.reset();
    mGlyphAtlasImage.reset();
    mBesselTextureSampler.reset();
    mBesselTextureView.reset();
    mBesselTextureImage.reset();
    mDotTextureSampler.reset();
    mDotTextureView.reset();
    mDotTextureImage.reset();
    for (VulkanShaderModule& shader : mFragmentShaders) {
        shader.reset();
    }
    for (VulkanShaderModule& shader : mVertexShaders) {
        shader.reset();
    }
    for (VulkanShaderModule& shader : mWorldFragmentShaders) {
        shader.reset();
    }
    for (VulkanShaderModule& shader : mWorldVertexShaders) {
        shader.reset();
    }
    mDevice.reset();
    mSetDebugObjectName = nullptr;
    mDotTextureWidth = 0;
    mDotTextureHeight = 0;
    mBesselTextureWidth = 0;
    mBesselTextureHeight = 0;
    mGlyphAtlasWidth = 0;
    mGlyphAtlasHeight = 0;
    mGeometryUploadCount = 0;
    mGeometryUploadSkipCount = 0;
    mGeometryUploadBytes = 0;
    mGeometryBufferGrowthCount = 0;
    mTextGeometryUploadCount = 0;
    mTextGeometryUploadSkipCount = 0;
    mTextGeometryUploadBytes = 0;
    mTextGeometryBufferGrowthCount = 0;
    mBasicCellDrawCount = 0;
    mArrowDrawCount = 0;
    mFoodDrawCount = 0;
    mLinkDrawCount = 0;
    mDeadDrawCount = 0;
    mOutlineDrawCount = 0;
    mMarkerDrawCount = 0;
    mGenomeGridDrawCount = 0;
    mGenomeTreeDrawCount = 0;
    mTextDrawCount = 0;
    mPipelineBindCount = 0;
}

bool VulkanBasicCellRenderer::createShaderModule(
        const std::vector<std::uint32_t>& spirv, VulkanShaderModule& output,
        std::string& error) const {
    if (!validSpirv(spirv)) {
        error = "Vulkan basic-cell shader contains invalid SPIR-V";
        return false;
    }
    const VkShaderModuleCreateInfo createInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            spirv.size() * sizeof(std::uint32_t),
            spirv.data(),
    };
    VkShaderModule shader = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(
            mDevice->device(), &createInfo, nullptr, &shader);
    if (result != VK_SUCCESS) {
        error = "vkCreateShaderModule(basic cell) failed with "
                + std::to_string(result);
        return false;
    }
    output = VulkanShaderModule(mDevice, shader);
    return true;
}

bool VulkanBasicCellRenderer::createShaderResources(
        std::array<VulkanCellShaderSpirv,
                kVulkanBasicCellProgramCount>&& shaderSpirv,
        std::array<VulkanCellShaderSpirv,
                kVulkanWorldProgramCount>&& worldShaderSpirv,
        std::string& error) {
    for (std::size_t index = 0; index < shaderSpirv.size(); ++index) {
        if (!createShaderModule(shaderSpirv[index].vertex,
                    mVertexShaders[index], error)
                || !createShaderModule(shaderSpirv[index].fragment,
                        mFragmentShaders[index], error)) {
            return false;
        }
        const auto program = static_cast<VulkanBasicCellProgram>(index);
        const std::string baseName = "Cellularity Vulkan "
                + std::string(vulkanBasicCellProgramName(program));
        nameObject(VK_OBJECT_TYPE_SHADER_MODULE,
                mVertexShaders[index].get(),
                (baseName + " vertex shader").c_str());
        nameObject(VK_OBJECT_TYPE_SHADER_MODULE,
                mFragmentShaders[index].get(),
                (baseName + " fragment shader").c_str());
    }
    for (std::size_t index = 0; index < worldShaderSpirv.size(); ++index) {
        if (!createShaderModule(worldShaderSpirv[index].vertex,
                    mWorldVertexShaders[index], error)
                || !createShaderModule(worldShaderSpirv[index].fragment,
                        mWorldFragmentShaders[index], error)) {
            return false;
        }
        const auto program = static_cast<VulkanWorldProgram>(index);
        const std::string baseName = "Cellularity Vulkan "
                + std::string(vulkanWorldProgramName(program));
        nameObject(VK_OBJECT_TYPE_SHADER_MODULE,
                mWorldVertexShaders[index].get(),
                (baseName + " vertex shader").c_str());
        nameObject(VK_OBJECT_TYPE_SHADER_MODULE,
                mWorldFragmentShaders[index].get(),
                (baseName + " fragment shader").c_str());
    }
    return true;
}

bool VulkanBasicCellRenderer::createTexture(
        NativeRenderTexturePayload&& texture,
        const VulkanCommandPool& commandPool, VkQueue graphicsQueue,
        const char* debugName, VulkanImage& image,
        VulkanImageView& imageView, VulkanSampler& sampler,
        std::uint32_t& width, std::uint32_t& height,
        std::string& error) {
    const std::uint64_t expectedBytes = texture.pixels.size();
    if (!texture.valid()
            || texture.width
                    > mDevice->properties().limits.maxImageDimension2D
            || texture.height
                    > mDevice->properties().limits.maxImageDimension2D) {
        error = std::string("Vulkan ") + debugName
                + " texture payload is invalid";
        return false;
    }
    const VkFormat format = texture.pixelFormat
                    == NativeRenderTexturePixelFormat::R8
            ? VK_FORMAT_R8_UNORM
            : VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(mDevice->physicalDevice(),
            format, &formatProperties);
    VkFormatFeatureFlags requiredTextureFeatures =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (texture.minFilter == NativeRenderTextureFilter::Linear
            || texture.magFilter == NativeRenderTextureFilter::Linear) {
        requiredTextureFeatures |=
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    }
    if ((formatProperties.optimalTilingFeatures & requiredTextureFeatures)
            != requiredTextureFeatures) {
        error = std::string("Vulkan device cannot sample ") + debugName
                + " texture with the requested filter";
        return false;
    }

    VulkanBuffer staging;
    VkResult result = VulkanBuffer::create(mDevice,
            static_cast<VkDeviceSize>(expectedBytes),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true, staging);
    if (result != VK_SUCCESS) {
        error = "Could not allocate Vulkan dot-texture staging buffer";
        return false;
    }
    std::memcpy(staging.mappedData(), texture.pixels.data(),
            texture.pixels.size());
    result = staging.flush(0, static_cast<VkDeviceSize>(expectedBytes));
    if (result != VK_SUCCESS) {
        error = "Could not flush Vulkan dot-texture staging buffer";
        return false;
    }
    mDevice->counters().uploadBytes += expectedBytes;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {texture.width, texture.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    result = VulkanImage::create(mDevice, imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, image);
    if (result != VK_SUCCESS) {
        error = "Could not allocate Vulkan dot texture";
        return false;
    }

    std::vector<VulkanCommandBuffer> commands;
    result = commandPool.allocatePrimary(1, commands);
    if (result != VK_SUCCESS || commands.empty()) {
        error = "Could not allocate Vulkan texture upload command buffer";
        return false;
    }
    const VkCommandBuffer command = commands.front().get();
    const VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            nullptr,
    };
    result = vkBeginCommandBuffer(command, &beginInfo);
    if (result != VK_SUCCESS) {
        error = "Could not begin Vulkan texture upload";
        return false;
    }

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image.get();
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {texture.width, texture.height, 1};
    vkCmdCopyBufferToImage(command, staging.get(), image.get(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier toShaderRead{};
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = image.get();
    toShaderRead.subresourceRange = toTransfer.subresourceRange;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
            1, &toShaderRead);
    result = vkEndCommandBuffer(command);
    if (result != VK_SUCCESS) {
        error = "Could not finish Vulkan texture upload commands";
        return false;
    }

    const VkFenceCreateInfo fenceInfo{
            VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            nullptr,
            0,
    };
    VkFence fenceHandle = VK_NULL_HANDLE;
    result = vkCreateFence(
            mDevice->device(), &fenceInfo, nullptr, &fenceHandle);
    if (result != VK_SUCCESS) {
        error = "Could not create Vulkan texture upload fence";
        return false;
    }
    VulkanFence fence(mDevice, fenceHandle);
    const VkSubmitInfo submitInfo{
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            nullptr,
            0,
            nullptr,
            nullptr,
            1,
            &command,
            0,
            nullptr,
    };
    result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence.get());
    if (result == VK_SUCCESS) {
        ++mDevice->counters().queueSubmissionCount;
        ++mDevice->counters().fenceWaitCount;
        result = vkWaitForFences(mDevice->device(), 1, &fenceHandle, VK_TRUE,
                std::numeric_limits<std::uint64_t>::max());
    }
    if (result != VK_SUCCESS) {
        error = "Vulkan dot-texture upload did not complete";
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    result = vkCreateImageView(mDevice->device(), &viewInfo, nullptr, &view);
    if (result != VK_SUCCESS) {
        error = "Could not create Vulkan dot-texture image view";
        return false;
    }
    imageView = VulkanImageView(mDevice, view);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = texture.magFilter
                    == NativeRenderTextureFilter::Nearest
            ? VK_FILTER_NEAREST
            : VK_FILTER_LINEAR;
    samplerInfo.minFilter = texture.minFilter
                    == NativeRenderTextureFilter::Nearest
            ? VK_FILTER_NEAREST
            : VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = texture.wrapS
                    == NativeRenderTextureWrap::ClampToEdge
            ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = texture.wrapT
                    == NativeRenderTextureWrap::ClampToEdge
            ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = samplerInfo.addressModeV;
    samplerInfo.maxAnisotropy = 1.0F;
    samplerInfo.minLod = 0.0F;
    samplerInfo.maxLod = 0.0F;
    VkSampler samplerHandle = VK_NULL_HANDLE;
    result = vkCreateSampler(
            mDevice->device(), &samplerInfo, nullptr, &samplerHandle);
    if (result != VK_SUCCESS) {
        error = "Could not create Vulkan dot-texture sampler";
        return false;
    }
    sampler = VulkanSampler(mDevice, samplerHandle);
    width = texture.width;
    height = texture.height;
    const std::string objectPrefix =
            "Cellularity Vulkan " + std::string(debugName);
    nameObject(VK_OBJECT_TYPE_IMAGE, image.get(),
            (objectPrefix + " image").c_str());
    nameObject(VK_OBJECT_TYPE_DEVICE_MEMORY, image.memory(),
            (objectPrefix + " memory").c_str());
    nameObject(VK_OBJECT_TYPE_IMAGE_VIEW, imageView.get(),
            (objectPrefix + " image view").c_str());
    nameObject(VK_OBJECT_TYPE_SAMPLER, sampler.get(),
            (objectPrefix + " sampler").c_str());
    return true;
}

bool VulkanBasicCellRenderer::createDescriptorResources(
        std::string& error) {
    const std::array<VkDescriptorSetLayoutBinding, 4> bindings = {{
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                    VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    }};
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            static_cast<std::uint32_t>(bindings.size()),
            bindings.data(),
    };
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorSetLayout(
            mDevice->device(), &layoutInfo, nullptr, &layout);
    if (result != VK_SUCCESS) {
        error = "Could not create Vulkan basic-cell descriptor layout";
        return false;
    }
    mDescriptorSetLayout = VulkanDescriptorSetLayout(mDevice, layout);

    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    kFrameDescriptorCount},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    kFrameDescriptorCount * 3},
    }};
    const VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            nullptr,
            VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            kFrameDescriptorCount,
            static_cast<std::uint32_t>(poolSizes.size()),
            poolSizes.data(),
    };
    result = VulkanDescriptorPool::create(mDevice, poolInfo, mDescriptorPool);
    if (result != VK_SUCCESS) {
        error = "Could not create Vulkan basic-cell descriptor pool";
        return false;
    }
    const std::vector<VkDescriptorSetLayout> layouts(
            kFrameDescriptorCount, mDescriptorSetLayout.get());
    std::vector<VulkanDescriptorSet> descriptorSets;
    result = mDescriptorPool.allocate(layouts, descriptorSets);
    if (result != VK_SUCCESS
            || descriptorSets.size() != kFrameDescriptorCount) {
        error = "Could not allocate Vulkan basic-cell descriptor sets";
        return false;
    }
    for (std::size_t index = 0; index < mDescriptorSets.size(); ++index) {
        mDescriptorSets[index] = std::move(descriptorSets[index]);
    }

    const VkDescriptorSetLayout setLayout = mDescriptorSetLayout.get();
    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            1,
            &setLayout,
            0,
            nullptr,
    };
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    result = vkCreatePipelineLayout(mDevice->device(), &pipelineLayoutInfo,
            nullptr, &pipelineLayout);
    if (result != VK_SUCCESS) {
        error = "Could not create Vulkan basic-cell pipeline layout";
        return false;
    }
    mPipelineLayout = VulkanPipelineLayout(mDevice, pipelineLayout);
    nameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
            mDescriptorSetLayout.get(),
            "Cellularity Vulkan basic-cell descriptor layout");
    nameObject(VK_OBJECT_TYPE_DESCRIPTOR_POOL, mDescriptorPool.get(),
            "Cellularity Vulkan basic-cell descriptor pool");
    nameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, mPipelineLayout.get(),
            "Cellularity Vulkan basic-cell pipeline layout");
    for (std::size_t index = 0; index < mDescriptorSets.size(); ++index) {
        nameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET,
                mDescriptorSets[index].get(),
                ("Cellularity Vulkan basic-cell descriptor set "
                        + std::to_string(index))
                        .c_str());
    }
    return true;
}

bool VulkanBasicCellRenderer::createGeometryBuffers(std::string& error) {
    for (std::size_t index = 0; index < mGeometrySlots.size(); ++index) {
        VkResult result = VulkanBuffer::create(mDevice,
                kInitialGeometryBufferBytes,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true,
                mGeometrySlots[index].buffer);
        if (result != VK_SUCCESS) {
            error = "Could not allocate Vulkan basic-cell geometry buffer";
            return false;
        }
        nameObject(VK_OBJECT_TYPE_BUFFER,
                mGeometrySlots[index].buffer.get(),
                ("Cellularity Vulkan basic-cell geometry buffer "
                        + std::to_string(index))
                        .c_str());
        nameObject(VK_OBJECT_TYPE_DEVICE_MEMORY,
                mGeometrySlots[index].buffer.memory(),
                ("Cellularity Vulkan basic-cell geometry memory "
                        + std::to_string(index))
                        .c_str());
    }
    return true;
}

bool VulkanBasicCellRenderer::createTextGeometryBuffers(
        std::string& error) {
    for (std::size_t index = 0; index < mTextGeometrySlots.size(); ++index) {
        const VkResult result = VulkanBuffer::create(mDevice,
                kInitialTextGeometryBufferBytes,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true,
                mTextGeometrySlots[index].buffer);
        if (result != VK_SUCCESS) {
            error = "Could not allocate Vulkan text geometry buffer";
            return false;
        }
        nameObject(VK_OBJECT_TYPE_BUFFER,
                mTextGeometrySlots[index].buffer.get(),
                ("Cellularity Vulkan text geometry buffer "
                        + std::to_string(index))
                        .c_str());
        nameObject(VK_OBJECT_TYPE_DEVICE_MEMORY,
                mTextGeometrySlots[index].buffer.memory(),
                ("Cellularity Vulkan text geometry memory "
                        + std::to_string(index))
                        .c_str());
    }
    return true;
}

bool VulkanBasicCellRenderer::createUnitQuadBuffer(std::string& error) {
    static constexpr std::array<float, 12> kUnitQuad = {
            1.0F,
            1.0F,
            1.0F,
            -1.0F,
            -1.0F,
            -1.0F,
            -1.0F,
            -1.0F,
            -1.0F,
            1.0F,
            1.0F,
            1.0F,
    };
    const VkDeviceSize size = sizeof(kUnitQuad);
    const VkResult result = VulkanBuffer::create(mDevice, size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true,
            mUnitQuadBuffer);
    if (result != VK_SUCCESS || mUnitQuadBuffer.mappedData() == nullptr) {
        error = "Could not allocate Vulkan world unit-quad buffer";
        return false;
    }
    std::memcpy(mUnitQuadBuffer.mappedData(), kUnitQuad.data(), size);
    if (mUnitQuadBuffer.flush(0, size) != VK_SUCCESS) {
        error = "Could not flush Vulkan world unit-quad buffer";
        return false;
    }
    mDevice->counters().uploadBytes += size;
    nameObject(VK_OBJECT_TYPE_BUFFER, mUnitQuadBuffer.get(),
            "Cellularity Vulkan world unit-quad buffer");
    nameObject(VK_OBJECT_TYPE_DEVICE_MEMORY, mUnitQuadBuffer.memory(),
            "Cellularity Vulkan world unit-quad memory");
    return true;
}

bool VulkanBasicCellRenderer::createPipelines(
        VkRenderPass renderPass, std::string& error,
        VulkanPipelineTarget target) {
    destroyPipelines(target);
    if (renderPass == VK_NULL_HANDLE || !mPipelineLayout) {
        error = "Vulkan basic-cell pipelines require a render pass and layout";
        return false;
    }
    PipelineSet& pipelines = pipelineSet(target);

    constexpr std::array<VkVertexInputBindingDescription, 4> bindings = {{
            {0, 2 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
            {1, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
            {2, 2 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
            {3, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
    }};
    constexpr std::array<VkVertexInputAttributeDescription, 4> attributes = {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
            {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {2, 2, VK_FORMAT_R32G32_SFLOAT, 0},
            {3, 3, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
    }};
    const VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<std::uint32_t>(bindings.size()),
            bindings.data(),
            static_cast<std::uint32_t>(attributes.size()),
            attributes.data(),
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            VK_FALSE,
    };
    const VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            nullptr,
            0,
            1,
            nullptr,
            1,
            nullptr,
    };
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstAlphaBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
            | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
            | VK_COLOR_COMPONENT_A_BIT;
    const VkPipelineColorBlendStateCreateInfo colorBlending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            nullptr,
            0,
            VK_FALSE,
            VK_LOGIC_OP_COPY,
            1,
            &blendAttachment,
            {0.0F, 0.0F, 0.0F, 0.0F},
    };
    constexpr std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
    };
    const VkPipelineDynamicStateCreateInfo dynamicState{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<std::uint32_t>(dynamicStates.size()),
            dynamicStates.data(),
    };

    const auto createBasicCellPipeline = [&](std::size_t index,
                                                 VkBool32 blendEnabled,
                                                 VulkanPipeline& output,
                                                 const char* debugVariant) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
                        0, VK_SHADER_STAGE_VERTEX_BIT,
                        mVertexShaders[index].get(), "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
                        0, VK_SHADER_STAGE_FRAGMENT_BIT,
                        mFragmentShaders[index].get(), "main", nullptr},
        }};
        VkPipelineColorBlendAttachmentState basicCellBlend = blendAttachment;
        basicCellBlend.blendEnable = blendEnabled;
        VkPipelineColorBlendStateCreateInfo basicCellColorBlending =
                colorBlending;
        basicCellColorBlending.pAttachments = &basicCellBlend;
        VkGraphicsPipelineCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        createInfo.pStages = stages.data();
        createInfo.pVertexInputState = &vertexInput;
        createInfo.pInputAssemblyState = &inputAssembly;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterization;
        createInfo.pMultisampleState = &multisampling;
        createInfo.pDepthStencilState = &depthStencil;
        createInfo.pColorBlendState = &basicCellColorBlending;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = mPipelineLayout.get();
        createInfo.renderPass = renderPass;
        createInfo.subpass = 0;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateGraphicsPipelines(mDevice->device(),
                VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            error = "Could not create Vulkan basic-cell pipeline "
                    + std::string(vulkanBasicCellProgramName(
                            static_cast<VulkanBasicCellProgram>(index)))
                    + " (" + debugVariant + ")";
            return false;
        }
        output = VulkanPipeline(mDevice, pipeline);
        nameObject(VK_OBJECT_TYPE_PIPELINE, output.get(),
                ("Cellularity Vulkan "
                        + std::string(vulkanBasicCellProgramName(
                                static_cast<VulkanBasicCellProgram>(index)))
                        + " basic-cell " + debugVariant + " pipeline")
                        .c_str());
        return true;
    };

    for (std::size_t index = 0;
            index < pipelines.blendedBasicCells.size(); ++index) {
        if (!createBasicCellPipeline(index, VK_TRUE,
                    pipelines.blendedBasicCells[index], "blended")
                || !createBasicCellPipeline(index, VK_FALSE,
                        pipelines.opaqueBasicCells[index], "opaque")) {
            destroyPipelines(target);
            return false;
        }
    }

    constexpr std::array<VkVertexInputBindingDescription, 4>
            outlineBindings = {{
                    {0, 2 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
                    {1, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
                    {2, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
                    {3, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
            }};
    constexpr std::array<VkVertexInputAttributeDescription, 4>
            outlineAttributes = {{
                    {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                    {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                    {2, 2, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                    {3, 3, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            }};
    const VkPipelineVertexInputStateCreateInfo outlineVertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<std::uint32_t>(outlineBindings.size()),
            outlineBindings.data(),
            static_cast<std::uint32_t>(outlineAttributes.size()),
            outlineAttributes.data(),
    };
    constexpr std::array<VkVertexInputBindingDescription, 3>
            instanceBindings = {{
                    {0, 2 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
                    {1, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_INSTANCE},
                    {2, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_INSTANCE},
            }};
    constexpr std::array<VkVertexInputAttributeDescription, 3>
            instanceAttributes = {{
                    {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                    {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                    {2, 2, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            }};
    const VkPipelineVertexInputStateCreateInfo instanceVertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<std::uint32_t>(instanceBindings.size()),
            instanceBindings.data(),
            static_cast<std::uint32_t>(instanceAttributes.size()),
            instanceAttributes.data(),
    };
    constexpr std::array<VkVertexInputBindingDescription, 2> linkBindings = {{
            {0, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
            {1, sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
    }};
    constexpr std::array<VkVertexInputAttributeDescription, 2>
            linkAttributes = {{
                    {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                    {1, 1, VK_FORMAT_R32_SFLOAT, 0},
            }};
    const VkPipelineVertexInputStateCreateInfo linkVertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<std::uint32_t>(linkBindings.size()),
            linkBindings.data(),
            static_cast<std::uint32_t>(linkAttributes.size()),
            linkAttributes.data(),
    };
    constexpr std::array<VkVertexInputBindingDescription, 2>
            markerBindings = {{
                    {0, 2 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
                    {1, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_INSTANCE},
            }};
    constexpr std::array<VkVertexInputAttributeDescription, 2>
            markerAttributes = {{
                    {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                    {1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            }};
    const VkPipelineVertexInputStateCreateInfo markerVertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<std::uint32_t>(markerBindings.size()),
            markerBindings.data(),
            static_cast<std::uint32_t>(markerAttributes.size()),
            markerAttributes.data(),
    };
    constexpr VkVertexInputBindingDescription gridBinding{
            0, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX
    };
    constexpr std::array<VkVertexInputAttributeDescription, 2>
            gridAttributes = {{
                    {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                    {1, 0, VK_FORMAT_R32G32_SFLOAT, 2 * sizeof(float)},
            }};
    const VkPipelineVertexInputStateCreateInfo gridVertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            1,
            &gridBinding,
            static_cast<std::uint32_t>(gridAttributes.size()),
            gridAttributes.data(),
    };
    constexpr VkVertexInputBindingDescription treeBinding{
            0, 2 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX
    };
    constexpr VkVertexInputAttributeDescription treeAttribute{
            0, 0, VK_FORMAT_R32G32_SFLOAT, 0
    };
    const VkPipelineVertexInputStateCreateInfo treeVertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            1,
            &treeBinding,
            1,
            &treeAttribute,
    };
    constexpr std::array<VkVertexInputBindingDescription, 1>
            textBindings = {{
                    {0, PixelTextRenderer::kInstanceFloatCount * sizeof(float),
                            VK_VERTEX_INPUT_RATE_INSTANCE},
            }};
    constexpr std::array<VkVertexInputAttributeDescription, 3>
            textAttributes = {{
                    {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                    {1, 0, VK_FORMAT_R32_SFLOAT, 4 * sizeof(float)},
                    {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                            5 * sizeof(float)},
            }};
    const VkPipelineVertexInputStateCreateInfo textVertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            nullptr,
            0,
            static_cast<std::uint32_t>(textBindings.size()),
            textBindings.data(),
            static_cast<std::uint32_t>(textAttributes.size()),
            textAttributes.data(),
    };

    const auto createWorldPipeline = [&](VulkanWorldProgram program,
                                             const VkPipelineVertexInputStateCreateInfo& worldVertexInput,
                                             VkBool32 depthTest,
                                             VkBool32 depthWrite,
                                             VkCompareOp depthCompare,
                                             VkBool32 blendEnabled) {
        const std::size_t index = static_cast<std::size_t>(program);
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
                        0, VK_SHADER_STAGE_VERTEX_BIT,
                        mWorldVertexShaders[index].get(), "main", nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr,
                        0, VK_SHADER_STAGE_FRAGMENT_BIT,
                        mWorldFragmentShaders[index].get(), "main", nullptr},
        }};
        VkPipelineDepthStencilStateCreateInfo worldDepth = depthStencil;
        worldDepth.depthTestEnable = depthTest;
        worldDepth.depthWriteEnable = depthWrite;
        worldDepth.depthCompareOp = depthCompare;
        VkPipelineColorBlendAttachmentState worldBlend = blendAttachment;
        worldBlend.blendEnable = blendEnabled;
        VkPipelineColorBlendStateCreateInfo worldColorBlending =
                colorBlending;
        worldColorBlending.pAttachments = &worldBlend;
        VkGraphicsPipelineCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        createInfo.pStages = stages.data();
        createInfo.pVertexInputState = &worldVertexInput;
        createInfo.pInputAssemblyState = &inputAssembly;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterization;
        createInfo.pMultisampleState = &multisampling;
        createInfo.pDepthStencilState = &worldDepth;
        createInfo.pColorBlendState = &worldColorBlending;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = mPipelineLayout.get();
        createInfo.renderPass = renderPass;
        createInfo.subpass = 0;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateGraphicsPipelines(mDevice->device(),
                VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            error = "Could not create Vulkan world pipeline "
                    + std::string(vulkanWorldProgramName(program));
            return false;
        }
        pipelines.world[index] = VulkanPipeline(mDevice, pipeline);
        nameObject(VK_OBJECT_TYPE_PIPELINE, pipeline,
                ("Cellularity Vulkan "
                        + std::string(vulkanWorldProgramName(program))
                        + " world pipeline")
                        .c_str());
        return true;
    };

    if (!createWorldPipeline(VulkanWorldProgram::Arrow, vertexInput,
                VK_TRUE, VK_TRUE, VK_COMPARE_OP_ALWAYS, VK_TRUE)
            || !createWorldPipeline(VulkanWorldProgram::Food,
                    instanceVertexInput, VK_FALSE, VK_FALSE,
                    VK_COMPARE_OP_ALWAYS, VK_TRUE)
            || !createWorldPipeline(VulkanWorldProgram::Link,
                    linkVertexInput, VK_FALSE, VK_FALSE,
                    VK_COMPARE_OP_ALWAYS, VK_TRUE)
            || !createWorldPipeline(VulkanWorldProgram::Marker,
                    markerVertexInput, VK_FALSE, VK_FALSE,
                    VK_COMPARE_OP_ALWAYS, VK_TRUE)
            || !createWorldPipeline(VulkanWorldProgram::PixelText,
                    textVertexInput, VK_FALSE, VK_FALSE,
                    VK_COMPARE_OP_ALWAYS, VK_TRUE)
            || !createWorldPipeline(VulkanWorldProgram::Dead,
                    instanceVertexInput, VK_TRUE, VK_FALSE,
                    VK_COMPARE_OP_ALWAYS, VK_TRUE)
            || !createWorldPipeline(VulkanWorldProgram::GenomeGrid,
                    gridVertexInput, VK_FALSE, VK_FALSE,
                    VK_COMPARE_OP_ALWAYS, VK_FALSE)
            || !createWorldPipeline(VulkanWorldProgram::GenomeTree,
                    treeVertexInput, VK_FALSE, VK_FALSE,
                    VK_COMPARE_OP_ALWAYS, VK_TRUE)) {
        destroyPipelines(target);
        return false;
    }
    constexpr std::array<VulkanWorldProgram, 8> kOutlinePrograms = {
            VulkanWorldProgram::CiliocyteOutline,
            VulkanWorldProgram::DevorocyteOutline,
            VulkanWorldProgram::FlagellocyteOutline,
            VulkanWorldProgram::GlueocyteOutline,
            VulkanWorldProgram::NitrocyteOutline,
            VulkanWorldProgram::SenseocyteOutline,
            VulkanWorldProgram::StereocyteOutline,
            VulkanWorldProgram::VirocyteOutline,
    };
    for (const VulkanWorldProgram program : kOutlinePrograms) {
        if (!createWorldPipeline(program, outlineVertexInput, VK_TRUE,
                    VK_FALSE, VK_COMPARE_OP_LESS, VK_TRUE)) {
            destroyPipelines(target);
            return false;
        }
    }
    return true;
}

void VulkanBasicCellRenderer::destroyPipelines(
        VulkanPipelineTarget target) noexcept {
    PipelineSet& pipelines = pipelineSet(target);
    for (VulkanPipeline& pipeline : pipelines.world) {
        pipeline.reset();
    }
    for (VulkanPipeline& pipeline : pipelines.blendedBasicCells) {
        pipeline.reset();
    }
    for (VulkanPipeline& pipeline : pipelines.opaqueBasicCells) {
        pipeline.reset();
    }
}

void VulkanBasicCellRenderer::destroyPipelines() noexcept {
    for (std::size_t index = 0; index < mPipelineSets.size(); ++index) {
        destroyPipelines(static_cast<VulkanPipelineTarget>(index));
    }
}

VulkanBasicCellRenderer::PipelineSet&
VulkanBasicCellRenderer::pipelineSet(
        VulkanPipelineTarget target) noexcept {
    const std::size_t index = target == VulkanPipelineTarget::Offscreen
            ? static_cast<std::size_t>(VulkanPipelineTarget::Offscreen)
            : static_cast<std::size_t>(VulkanPipelineTarget::Surface);
    return mPipelineSets[index];
}

const VulkanBasicCellRenderer::PipelineSet&
VulkanBasicCellRenderer::pipelineSet(
        VulkanPipelineTarget target) const noexcept {
    const std::size_t index = target == VulkanPipelineTarget::Offscreen
            ? static_cast<std::size_t>(VulkanPipelineTarget::Offscreen)
            : static_cast<std::size_t>(VulkanPipelineTarget::Surface);
    return mPipelineSets[index];
}

void VulkanBasicCellRenderer::updateFrameDescriptor(
        std::size_t frameIndex, VkBuffer uniformBuffer) const {
    if (frameIndex >= mDescriptorSets.size()
            || uniformBuffer == VK_NULL_HANDLE || !mDotTextureView
            || !mDotTextureSampler || !mBesselTextureView
            || !mBesselTextureSampler || !mGlyphAtlasView
            || !mGlyphAtlasSampler) {
        return;
    }
    const VkDescriptorBufferInfo bufferInfo{
            uniformBuffer,
            0,
            kCellUniformBytes,
    };
    const VkDescriptorImageInfo dotImageInfo{
            mDotTextureSampler.get(),
            mDotTextureView.get(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkDescriptorImageInfo besselImageInfo{
            mBesselTextureSampler.get(),
            mBesselTextureView.get(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkDescriptorImageInfo glyphAtlasImageInfo{
            mGlyphAtlasSampler.get(),
            mGlyphAtlasView.get(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const std::array<VkWriteDescriptorSet, 4> writes = {{
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                    mDescriptorSets[frameIndex].get(), 0, 0, 1,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr,
                    &bufferInfo, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                    mDescriptorSets[frameIndex].get(), 1, 0, 1,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dotImageInfo,
                    nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                    mDescriptorSets[frameIndex].get(), 2, 0, 1,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &besselImageInfo,
                    nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                    mDescriptorSets[frameIndex].get(), 3, 0, 1,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    &glyphAtlasImageInfo, nullptr, nullptr},
    }};
    vkUpdateDescriptorSets(mDevice->device(),
            static_cast<std::uint32_t>(writes.size()), writes.data(), 0,
            nullptr);
    ++mDevice->counters().descriptorUpdateCount;
}

bool VulkanBasicCellRenderer::needsGeometryUpload(
        const PreparedRenderFrameView& frame) const noexcept {
    return !mHasActiveGeometry
            || !mGeometrySlots[mActiveGeometrySlot].valid
            || mGeometrySlots[mActiveGeometrySlot].plan.identity
            != VulkanPreparedFrameUploadIdentity::from(frame);
}

std::optional<std::size_t> VulkanBasicCellRenderer::writableGeometrySlot(
        std::uint64_t completedSubmissionSerial,
        std::uint64_t reservedSlotMask) const noexcept {
    const auto isReserved = [reservedSlotMask](std::size_t index) {
        return index < std::numeric_limits<std::uint64_t>::digits
                && (reservedSlotMask & (std::uint64_t{1} << index)) != 0;
    };
    for (std::size_t index = 0; index < mGeometrySlots.size(); ++index) {
        if (!isReserved(index)
                && (!mHasActiveGeometry || index != mActiveGeometrySlot)
                && mGeometrySlots[index].lastUseSerial
                        <= completedSubmissionSerial) {
            return index;
        }
    }
    if (mHasActiveGeometry && !isReserved(mActiveGeometrySlot)
            && mGeometrySlots[mActiveGeometrySlot].lastUseSerial
                    <= completedSubmissionSerial) {
        return mActiveGeometrySlot;
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
VulkanBasicCellRenderer::requiredGeometryWaitSerial(
        std::uint64_t completedSubmissionSerial,
        std::uint64_t reservedSlotMask) const noexcept {
    if (writableGeometrySlot(
                completedSubmissionSerial, reservedSlotMask)
                    .has_value()) {
        return std::nullopt;
    }
    std::uint64_t earliest = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < mGeometrySlots.size(); ++index) {
        if (index >= std::numeric_limits<std::uint64_t>::digits
                || (reservedSlotMask & (std::uint64_t{1} << index)) == 0) {
            earliest = std::min(
                    earliest, mGeometrySlots[index].lastUseSerial);
        }
    }
    return earliest == std::numeric_limits<std::uint64_t>::max()
            ? std::nullopt
            : std::optional<std::uint64_t>(earliest);
}

bool VulkanBasicCellRenderer::ensureGeometryCapacity(GeometrySlot& slot,
        VkDeviceSize requiredBytes, std::size_t slotIndex,
        std::string& error) {
    if (requiredBytes <= slot.buffer.size()) {
        return true;
    }
    VkDeviceSize capacity = std::max(
            slot.buffer.size(), kInitialGeometryBufferBytes);
    while (capacity < requiredBytes) {
        if (capacity > std::numeric_limits<VkDeviceSize>::max() / 2) {
            capacity = requiredBytes;
            break;
        }
        capacity *= 2;
    }
    VulkanBuffer replacement;
    VulkanResourceCounters& counters = mDevice->counters();
    const bool previousGrowthState = counters.insideControlledCapacityGrowth;
    counters.insideControlledCapacityGrowth = true;
    const VkResult result = VulkanBuffer::create(mDevice, capacity,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                    | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true, replacement);
    counters.insideControlledCapacityGrowth = previousGrowthState;
    if (result != VK_SUCCESS) {
        error = "Could not grow Vulkan basic-cell geometry buffer";
        return false;
    }
    slot.buffer = std::move(replacement);
    ++mGeometryBufferGrowthCount;
    nameObject(VK_OBJECT_TYPE_BUFFER, slot.buffer.get(),
            ("Cellularity Vulkan basic-cell geometry buffer "
                    + std::to_string(slotIndex))
                    .c_str());
    nameObject(VK_OBJECT_TYPE_DEVICE_MEMORY, slot.buffer.memory(),
            ("Cellularity Vulkan basic-cell geometry memory "
                    + std::to_string(slotIndex))
                    .c_str());
    return true;
}

bool VulkanBasicCellRenderer::uploadGeometry(
        const PreparedRenderFrameView& frame,
        std::uint64_t completedSubmissionSerial, std::string& error,
        std::size_t* uploadedSlot, std::uint64_t reservedSlotMask) {
    if (!frame.valid || !frame.geometryReady
            || frame.layout.version != kPreparedRenderFrameLayoutVersion) {
        error = "Vulkan basic-cell renderer requires a published compatible frame";
        return false;
    }
    const bool activeSlotReserved = mHasActiveGeometry
            && mActiveGeometrySlot
                    < std::numeric_limits<std::uint64_t>::digits
            && (reservedSlotMask
                       & (std::uint64_t{1} << mActiveGeometrySlot))
                    != 0;
    if (!needsGeometryUpload(frame) && !activeSlotReserved) {
        ++mGeometryUploadSkipCount;
        if (uploadedSlot != nullptr) {
            *uploadedSlot = mActiveGeometrySlot;
        }
        return true;
    }
    const auto slotIndex = writableGeometrySlot(
            completedSubmissionSerial, reservedSlotMask);
    if (!slotIndex.has_value()) {
        error = "No completed Vulkan geometry slot is available for upload";
        return false;
    }
    VulkanBasicCellUploadPlan plan;
    if (!buildVulkanBasicCellUploadPlan(frame, plan, error)) {
        return false;
    }
    GeometrySlot& slot = mGeometrySlots[*slotIndex];
    if (!ensureGeometryCapacity(slot,
                static_cast<VkDeviceSize>(plan.requiredBytes), *slotIndex,
                error)) {
        return false;
    }
    if (plan.requiredBytes > slot.buffer.size()
            || plan.requiredBytes
                    > std::numeric_limits<std::size_t>::max()) {
        error = "Vulkan basic-cell upload exceeds mapped address space";
        return false;
    }
    auto* destination = static_cast<std::uint8_t*>(slot.buffer.mappedData());
    if (destination == nullptr) {
        error = "Vulkan basic-cell geometry buffer is not mapped";
        return false;
    }
    const auto copy = [destination](const VulkanBasicCellStreamRange& range,
                              const void* source) {
        if (range.size != 0) {
            std::memcpy(destination + static_cast<std::size_t>(range.offset),
                    source, static_cast<std::size_t>(range.size));
        }
    };
    copy(plan.positions, frame.cellTriangleCoords.data());
    copy(plan.textureCoordinates, frame.cellTextureCoords.data());
    copy(plan.centers, frame.cellCenterCoords.data());
    copy(plan.colors, frame.cellColors.data());
    copy(plan.indices, frame.cellIndices.data());
    copy(plan.foodInstances, frame.foodInstanceAttributes.data());
    copy(plan.foodAttributes, frame.foodVertexAttributes.data());
    copy(plan.deadInstances, frame.deadCellInstanceAttributes.data());
    copy(plan.deadAttributes, frame.deadCellVertexAttributes.data());
    copy(plan.linkVertices, frame.linkTriangleCoords.data());
    copy(plan.linkPatternPositions, frame.linkTextureCoords.data());
    if (plan.markerInstances.size != 0) {
        auto* markers = reinterpret_cast<float*>(destination
                + static_cast<std::size_t>(plan.markerInstances.offset));
        for (const RenderMarker& marker : frame.markers) {
            *markers++ = marker.x;
            *markers++ = marker.y;
            *markers++ = marker.radius;
            *markers++ = marker.alpha;
        }
    }
    if (plan.requiredBytes != 0) {
        const VkResult result = slot.buffer.flush(
                0, static_cast<VkDeviceSize>(plan.requiredBytes));
        if (result != VK_SUCCESS) {
            error = "Could not flush Vulkan basic-cell geometry upload";
            return false;
        }
    }
    const std::uint64_t uploadedBytes = plan.positions.size
            + plan.textureCoordinates.size + plan.centers.size
            + plan.colors.size + plan.indices.size
            + plan.foodInstances.size + plan.foodAttributes.size
            + plan.deadInstances.size + plan.deadAttributes.size
            + plan.linkVertices.size + plan.linkPatternPositions.size
            + plan.markerInstances.size;
    mDevice->counters().uploadBytes += uploadedBytes;
    mGeometryUploadBytes += uploadedBytes;
    ++mGeometryUploadCount;
    slot.plan = plan;
    slot.valid = true;
    mActiveGeometrySlot = *slotIndex;
    mHasActiveGeometry = true;
    if (uploadedSlot != nullptr) {
        *uploadedSlot = *slotIndex;
    }
    return true;
}

bool VulkanBasicCellRenderer::needsTextGeometryUpload(
        const PixelTextRenderer& geometry) const noexcept {
    return !mHasActiveTextGeometry
            || !mTextGeometrySlots[mActiveTextGeometrySlot].valid
            || mTextGeometrySlots[mActiveTextGeometrySlot].source != &geometry
            || mTextGeometrySlots[mActiveTextGeometrySlot].revision
            != geometry.geometryRevision();
}

std::optional<std::size_t>
VulkanBasicCellRenderer::writableTextGeometrySlot(
        std::uint64_t completedSubmissionSerial) const noexcept {
    for (std::size_t index = 0; index < mTextGeometrySlots.size(); ++index) {
        if ((!mHasActiveTextGeometry || index != mActiveTextGeometrySlot)
                && mTextGeometrySlots[index].lastUseSerial
                        <= completedSubmissionSerial) {
            return index;
        }
    }
    if (mHasActiveTextGeometry
            && mTextGeometrySlots[mActiveTextGeometrySlot].lastUseSerial
                    <= completedSubmissionSerial) {
        return mActiveTextGeometrySlot;
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
VulkanBasicCellRenderer::requiredTextGeometryWaitSerial(
        std::uint64_t completedSubmissionSerial) const noexcept {
    if (writableTextGeometrySlot(completedSubmissionSerial).has_value()) {
        return std::nullopt;
    }
    std::uint64_t earliest = std::numeric_limits<std::uint64_t>::max();
    for (const TextGeometrySlot& slot : mTextGeometrySlots) {
        earliest = std::min(earliest, slot.lastUseSerial);
    }
    return earliest == std::numeric_limits<std::uint64_t>::max()
            ? std::nullopt
            : std::optional<std::uint64_t>(earliest);
}

bool VulkanBasicCellRenderer::ensureTextGeometryCapacity(
        TextGeometrySlot& slot, VkDeviceSize requiredBytes,
        std::size_t slotIndex, std::string& error) {
    if (requiredBytes <= slot.buffer.size()) {
        return true;
    }
    VkDeviceSize capacity = std::max(
            slot.buffer.size(), kInitialTextGeometryBufferBytes);
    while (capacity < requiredBytes) {
        if (capacity > std::numeric_limits<VkDeviceSize>::max() / 2) {
            capacity = requiredBytes;
            break;
        }
        capacity *= 2;
    }
    VulkanBuffer replacement;
    VulkanResourceCounters& counters = mDevice->counters();
    const bool previousGrowthState = counters.insideControlledCapacityGrowth;
    counters.insideControlledCapacityGrowth = true;
    const VkResult result = VulkanBuffer::create(mDevice, capacity,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true, replacement);
    counters.insideControlledCapacityGrowth = previousGrowthState;
    if (result != VK_SUCCESS) {
        error = "Could not grow Vulkan text geometry buffer";
        return false;
    }
    slot.buffer = std::move(replacement);
    ++mTextGeometryBufferGrowthCount;
    nameObject(VK_OBJECT_TYPE_BUFFER, slot.buffer.get(),
            ("Cellularity Vulkan text geometry buffer "
                    + std::to_string(slotIndex))
                    .c_str());
    nameObject(VK_OBJECT_TYPE_DEVICE_MEMORY, slot.buffer.memory(),
            ("Cellularity Vulkan text geometry memory "
                    + std::to_string(slotIndex))
                    .c_str());
    return true;
}

bool VulkanBasicCellRenderer::uploadTextGeometry(
        const PixelTextRenderer& geometry,
        std::uint64_t completedSubmissionSerial, std::string& error) {
    if (!geometry.hasCommittedGeometry() || geometry.instanceCount() < 0) {
        error = "Vulkan text renderer requires committed geometry";
        return false;
    }
    const std::uint64_t instanceCount =
            static_cast<std::uint64_t>(geometry.instanceCount());
    if (instanceCount
                    > std::numeric_limits<std::uint32_t>::max()
            || instanceCount
                    > std::numeric_limits<std::uint64_t>::max()
                            / (PixelTextRenderer::kInstanceFloatCount
                                    * sizeof(float))) {
        error = "Vulkan text instance count exceeds backend limits";
        return false;
    }
    const std::uint64_t requiredBytes = instanceCount
            * PixelTextRenderer::kInstanceFloatCount * sizeof(float);
    if (geometry.packedInstances().size()
            != instanceCount * PixelTextRenderer::kInstanceFloatCount) {
        error = "Vulkan text geometry has an invalid packed range";
        return false;
    }
    if (!needsTextGeometryUpload(geometry)) {
        ++mTextGeometryUploadSkipCount;
        return true;
    }
    const auto slotIndex =
            writableTextGeometrySlot(completedSubmissionSerial);
    if (!slotIndex.has_value()) {
        error = "No completed Vulkan text geometry slot is available for upload";
        return false;
    }
    TextGeometrySlot& slot = mTextGeometrySlots[*slotIndex];
    if (!ensureTextGeometryCapacity(slot,
                static_cast<VkDeviceSize>(requiredBytes), *slotIndex, error)) {
        return false;
    }
    if (requiredBytes > slot.buffer.size()
            || requiredBytes > std::numeric_limits<std::size_t>::max()) {
        error = "Vulkan text upload exceeds mapped address space";
        return false;
    }
    if (requiredBytes != 0) {
        if (slot.buffer.mappedData() == nullptr) {
            error = "Vulkan text geometry buffer is not mapped";
            return false;
        }
        std::memcpy(slot.buffer.mappedData(),
                geometry.packedInstances().data(),
                static_cast<std::size_t>(requiredBytes));
        const VkResult result = slot.buffer.flush(
                0, static_cast<VkDeviceSize>(requiredBytes));
        if (result != VK_SUCCESS) {
            error = "Could not flush Vulkan text geometry upload";
            return false;
        }
    }
    mDevice->counters().uploadBytes += requiredBytes;
    mTextGeometryUploadBytes += requiredBytes;
    ++mTextGeometryUploadCount;
    slot.source = &geometry;
    slot.revision = geometry.geometryRevision();
    slot.instanceCount = static_cast<std::uint32_t>(instanceCount);
    slot.valid = true;
    mActiveTextGeometrySlot = *slotIndex;
    mHasActiveTextGeometry = true;
    return true;
}

void VulkanBasicCellRenderer::record(VkCommandBuffer commandBuffer,
        std::size_t frameIndex, std::uint32_t dynamicUniformOffset,
        bool drawText, VulkanPipelineTarget target,
        VulkanGpuTimestampWriter* timestampWriter) const {
    if (!mHasActiveGeometry) {
        return;
    }
    VulkanBasicCellDrawOptions options;
    options.drawText = drawText;
    recordGeometry(commandBuffer, frameIndex, dynamicUniformOffset,
            mActiveGeometrySlot, options, target, timestampWriter);
}

void VulkanBasicCellRenderer::recordGeometry(
        VkCommandBuffer commandBuffer, std::size_t frameIndex,
        std::uint32_t dynamicUniformOffset, std::size_t geometrySlot,
        const VulkanBasicCellDrawOptions& options,
        VulkanPipelineTarget target,
        VulkanGpuTimestampWriter* timestampWriter) const {
    if (!ready(target) || geometrySlot >= mGeometrySlots.size()
            || !mGeometrySlots[geometrySlot].valid
            || frameIndex >= mDescriptorSets.size()) {
        return;
    }
    const PipelineSet& pipelines = pipelineSet(target);
    const GeometrySlot& slot = mGeometrySlots[geometrySlot];
    const VkDescriptorSet descriptorSet =
            mDescriptorSets[frameIndex].get();
    ++mDevice->counters().descriptorBindCount;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            mPipelineLayout.get(), 0, 1, &descriptorSet, 1,
            &dynamicUniformOffset);

    if (options.drawMarkers && slot.plan.markerInstanceCount > 0) {
        const std::array<VkBuffer, 2> buffers = {
                mUnitQuadBuffer.get(), slot.buffer.get()
        };
        const std::array<VkDeviceSize, 2> offsets = {
                0, slot.plan.markerInstances.offset
        };
        vkCmdBindVertexBuffers(commandBuffer, 0,
                static_cast<std::uint32_t>(buffers.size()), buffers.data(),
                offsets.data());
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines.world[static_cast<std::size_t>(
                                        VulkanWorldProgram::Marker)]
                        .get());
        ++mPipelineBindCount;
        vkCmdDraw(commandBuffer, 6, slot.plan.markerInstanceCount, 0, 0);
        ++mMarkerDrawCount;
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }

    if (options.drawFood && slot.plan.foodInstanceCount > 0) {
        const std::array<VkBuffer, 3> buffers = {
                mUnitQuadBuffer.get(), slot.buffer.get(), slot.buffer.get()
        };
        const std::array<VkDeviceSize, 3> offsets = {
                0, slot.plan.foodInstances.offset,
                slot.plan.foodAttributes.offset
        };
        vkCmdBindVertexBuffers(commandBuffer, 0,
                static_cast<std::uint32_t>(buffers.size()), buffers.data(),
                offsets.data());
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines.world[static_cast<std::size_t>(
                                        VulkanWorldProgram::Food)]
                        .get());
        ++mPipelineBindCount;
        vkCmdDraw(commandBuffer, 6, slot.plan.foodInstanceCount, 0, 0);
        ++mFoodDrawCount;
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }

    if (!options.previewOrder && options.drawLinks
            && slot.plan.linkDrawCount > 0) {
        const std::array<VkBuffer, 2> buffers = {
                slot.buffer.get(), slot.buffer.get()
        };
        const std::array<VkDeviceSize, 2> offsets = {
                slot.plan.linkVertices.offset,
                slot.plan.linkPatternPositions.offset
        };
        vkCmdBindVertexBuffers(commandBuffer, 0,
                static_cast<std::uint32_t>(buffers.size()), buffers.data(),
                offsets.data());
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines.world[static_cast<std::size_t>(
                                        VulkanWorldProgram::Link)]
                        .get());
        ++mPipelineBindCount;
        for (std::size_t index = 0; index < slot.plan.linkDrawCount;
                ++index) {
            const VulkanLinearWorldDraw& draw = slot.plan.linkDraws[index];
            vkCmdDraw(commandBuffer, draw.vertexCount, 1, draw.firstVertex, 0);
            ++mLinkDrawCount;
        }
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }

    if (slot.plan.deadInstanceCount > 0) {
        const std::array<VkBuffer, 3> buffers = {
                mUnitQuadBuffer.get(), slot.buffer.get(), slot.buffer.get()
        };
        const std::array<VkDeviceSize, 3> offsets = {
                0, slot.plan.deadInstances.offset,
                slot.plan.deadAttributes.offset
        };
        vkCmdBindVertexBuffers(commandBuffer, 0,
                static_cast<std::uint32_t>(buffers.size()), buffers.data(),
                offsets.data());
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines.world[static_cast<std::size_t>(
                                        VulkanWorldProgram::Dead)]
                        .get());
        ++mPipelineBindCount;
        vkCmdDraw(commandBuffer, 6, slot.plan.deadInstanceCount, 0, 0);
        ++mDeadDrawCount;
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }

    const std::array<VkBuffer, 4> cellBuffers = {
            slot.buffer.get(), slot.buffer.get(), slot.buffer.get(),
            slot.buffer.get()
    };
    const std::array<VkDeviceSize, 4> basicCellOffsets = {
            slot.plan.positions.offset,
            slot.plan.textureCoordinates.offset,
            slot.plan.centers.offset,
            slot.plan.colors.offset
    };
    vkCmdBindVertexBuffers(commandBuffer, 0,
            static_cast<std::uint32_t>(cellBuffers.size()),
            cellBuffers.data(), basicCellOffsets.data());
    vkCmdBindIndexBuffer(commandBuffer, slot.buffer.get(),
            slot.plan.indices.offset, VK_INDEX_TYPE_UINT16);
    const auto& basicCellPipelines = options.blendBasicCells
            ? pipelines.blendedBasicCells
            : pipelines.opaqueBasicCells;
    VulkanBasicCellProgram activeProgram = VulkanBasicCellProgram::Count;
    for (std::size_t index = 0; index < slot.plan.drawCount; ++index) {
        const VulkanBasicCellDraw& draw = slot.plan.draws[index];
        if (draw.program != activeProgram) {
            activeProgram = draw.program;
            vkCmdBindPipeline(commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    basicCellPipelines[static_cast<std::size_t>(activeProgram)]
                            .get());
            ++mPipelineBindCount;
        }
        vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.firstIndex,
                draw.vertexOffset, 0);
        ++mBasicCellDrawCount;
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }

    if (slot.plan.previewMode && options.drawDirectionArrow
            && slot.plan.arrowDrawCount > 0) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines.world[static_cast<std::size_t>(
                                        VulkanWorldProgram::Arrow)]
                        .get());
        ++mPipelineBindCount;
        for (std::size_t index = 0; index < slot.plan.arrowDrawCount;
                ++index) {
            const VulkanIndexedWorldDraw& draw =
                    slot.plan.arrowDraws[index];
            vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1,
                    draw.firstIndex, draw.vertexOffset, 0);
            ++mArrowDrawCount;
        }
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }

    if (options.previewOrder && options.drawLinks
            && slot.plan.linkDrawCount > 0) {
        const std::array<VkBuffer, 2> buffers = {
                slot.buffer.get(), slot.buffer.get()
        };
        const std::array<VkDeviceSize, 2> offsets = {
                slot.plan.linkVertices.offset,
                slot.plan.linkPatternPositions.offset
        };
        vkCmdBindVertexBuffers(commandBuffer, 0,
                static_cast<std::uint32_t>(buffers.size()), buffers.data(),
                offsets.data());
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines.world[static_cast<std::size_t>(
                                        VulkanWorldProgram::Link)]
                        .get());
        ++mPipelineBindCount;
        for (std::size_t index = 0; index < slot.plan.linkDrawCount;
                ++index) {
            const VulkanLinearWorldDraw& draw = slot.plan.linkDraws[index];
            vkCmdDraw(commandBuffer, draw.vertexCount, 1, draw.firstVertex, 0);
            ++mLinkDrawCount;
        }
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }

    if (slot.plan.outlineDrawCount > 0) {
        const std::array<VkDeviceSize, 4> outlineOffsets = {
                slot.plan.positions.offset,
                slot.plan.textureCoordinates.offset,
                slot.plan.centers.offset,
                slot.plan.colors.offset
        };
        vkCmdBindVertexBuffers(commandBuffer, 0,
                static_cast<std::uint32_t>(cellBuffers.size()),
                cellBuffers.data(), outlineOffsets.data());
        VulkanWorldProgram activeOutline = VulkanWorldProgram::Count;
        for (std::size_t index = 0; index < slot.plan.outlineDrawCount;
                ++index) {
            const VulkanOutlineDraw& draw = slot.plan.outlineDraws[index];
            if (draw.program != activeOutline) {
                activeOutline = draw.program;
                vkCmdBindPipeline(commandBuffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipelines.world[static_cast<std::size_t>(
                                                activeOutline)]
                                .get());
                ++mPipelineBindCount;
            }
            vkCmdDraw(commandBuffer, draw.vertexCount, 1,
                    draw.firstVertex, 0);
            ++mOutlineDrawCount;
        }
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }

    if (options.drawText && mHasActiveTextGeometry) {
        const TextGeometrySlot& textSlot =
                mTextGeometrySlots[mActiveTextGeometrySlot];
        if (textSlot.valid && textSlot.instanceCount > 0) {
            const VkBuffer textBuffer = textSlot.buffer.get();
            constexpr VkDeviceSize kTextOffset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &textBuffer,
                    &kTextOffset);
            vkCmdBindPipeline(commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelines.world[static_cast<std::size_t>(
                                            VulkanWorldProgram::PixelText)]
                            .get());
            ++mPipelineBindCount;
            vkCmdDraw(commandBuffer, 6, textSlot.instanceCount, 0, 0);
            ++mTextDrawCount;
        }
    }
    if (timestampWriter != nullptr) {
        timestampWriter->checkpoint(commandBuffer);
    }
}

void VulkanBasicCellRenderer::recordGenomeGrid(
        VkCommandBuffer commandBuffer, std::size_t frameIndex,
        std::uint32_t dynamicUniformOffset, VkBuffer vertexBuffer,
        VkDeviceSize vertexOffset, VulkanPipelineTarget target) const {
    if (!ready(target) || frameIndex >= mDescriptorSets.size()
            || vertexBuffer == VK_NULL_HANDLE) {
        return;
    }
    const PipelineSet& pipelines = pipelineSet(target);
    const VkDescriptorSet descriptorSet = mDescriptorSets[frameIndex].get();
    ++mDevice->counters().descriptorBindCount;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            mPipelineLayout.get(), 0, 1, &descriptorSet, 1,
            &dynamicUniformOffset);
    vkCmdBindVertexBuffers(
            commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelines.world[static_cast<std::size_t>(
                                    VulkanWorldProgram::GenomeGrid)]
                    .get());
    ++mPipelineBindCount;
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    ++mGenomeGridDrawCount;
}

void VulkanBasicCellRenderer::recordGenomeTreeLines(
        VkCommandBuffer commandBuffer, std::size_t frameIndex,
        std::uint32_t dynamicUniformOffset, VkBuffer vertexBuffer,
        VkDeviceSize vertexOffset, std::uint32_t vertexCount,
        VulkanPipelineTarget target) const {
    if (!ready(target) || frameIndex >= mDescriptorSets.size()
            || vertexBuffer == VK_NULL_HANDLE || vertexCount == 0) {
        return;
    }
    const PipelineSet& pipelines = pipelineSet(target);
    const VkDescriptorSet descriptorSet = mDescriptorSets[frameIndex].get();
    ++mDevice->counters().descriptorBindCount;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            mPipelineLayout.get(), 0, 1, &descriptorSet, 1,
            &dynamicUniformOffset);
    vkCmdBindVertexBuffers(
            commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelines.world[static_cast<std::size_t>(
                                    VulkanWorldProgram::GenomeTree)]
                    .get());
    ++mPipelineBindCount;
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
    ++mGenomeTreeDrawCount;
}

void VulkanBasicCellRenderer::markActiveGeometryUsed(
        std::uint64_t submissionSerial) noexcept {
    markGeometrySlotUsed(mActiveGeometrySlot, submissionSerial);
}

void VulkanBasicCellRenderer::markGeometrySlotUsed(
        std::size_t geometrySlot,
        std::uint64_t submissionSerial) noexcept {
    if (geometrySlot < mGeometrySlots.size()
            && mGeometrySlots[geometrySlot].valid) {
        mGeometrySlots[geometrySlot].lastUseSerial = submissionSerial;
    }
}

void VulkanBasicCellRenderer::markActiveTextGeometryUsed(
        std::uint64_t submissionSerial) noexcept {
    if (mHasActiveTextGeometry) {
        mTextGeometrySlots[mActiveTextGeometrySlot].lastUseSerial =
                submissionSerial;
    }
}

bool VulkanBasicCellRenderer::ready(
        VulkanPipelineTarget target) const noexcept {
    const PipelineSet& pipelines = pipelineSet(target);
    return mDevice && mPipelineLayout && mDescriptorSetLayout
            && mDotTextureImage.get() != VK_NULL_HANDLE
            && mDotTextureView && mDotTextureSampler
            && mBesselTextureImage.get() != VK_NULL_HANDLE
            && mBesselTextureView && mBesselTextureSampler
            && mGlyphAtlasImage.get() != VK_NULL_HANDLE
            && mGlyphAtlasView && mGlyphAtlasSampler
            && mUnitQuadBuffer.get() != VK_NULL_HANDLE
            && std::all_of(mGeometrySlots.begin(), mGeometrySlots.end(),
                    [](const GeometrySlot& slot) {
                        return slot.buffer.get() != VK_NULL_HANDLE;
                    })
            && std::all_of(mTextGeometrySlots.begin(),
                    mTextGeometrySlots.end(),
                    [](const TextGeometrySlot& slot) {
                        return slot.buffer.get() != VK_NULL_HANDLE;
                    })
            && std::all_of(pipelines.blendedBasicCells.begin(),
                    pipelines.blendedBasicCells.end(),
                    [](const VulkanPipeline& pipeline) {
                        return static_cast<bool>(pipeline);
                    })
            && std::all_of(pipelines.opaqueBasicCells.begin(),
                    pipelines.opaqueBasicCells.end(),
                    [](const VulkanPipeline& pipeline) {
                        return static_cast<bool>(pipeline);
                    })
            && std::all_of(pipelines.world.begin(), pipelines.world.end(),
                    [](const VulkanPipeline& pipeline) {
                        return static_cast<bool>(pipeline);
                    });
}

std::uint64_t VulkanBasicCellRenderer::geometryUploadCount() const noexcept {
    return mGeometryUploadCount;
}

std::uint64_t VulkanBasicCellRenderer::geometryUploadSkipCount() const noexcept {
    return mGeometryUploadSkipCount;
}

std::uint64_t VulkanBasicCellRenderer::geometryUploadBytes() const noexcept {
    return mGeometryUploadBytes;
}

std::uint64_t VulkanBasicCellRenderer::geometryBufferGrowthCount() const noexcept {
    return mGeometryBufferGrowthCount;
}

std::uint64_t VulkanBasicCellRenderer::basicCellDrawCount() const noexcept {
    return mBasicCellDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::arrowDrawCount() const noexcept {
    return mArrowDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::foodDrawCount() const noexcept {
    return mFoodDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::linkDrawCount() const noexcept {
    return mLinkDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::deadDrawCount() const noexcept {
    return mDeadDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::outlineDrawCount() const noexcept {
    return mOutlineDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::markerDrawCount() const noexcept {
    return mMarkerDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::genomeGridDrawCount() const noexcept {
    return mGenomeGridDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::genomeTreeDrawCount() const noexcept {
    return mGenomeTreeDrawCount;
}

std::uint64_t VulkanBasicCellRenderer::textDrawCount() const noexcept {
    return mTextDrawCount;
}

std::uint64_t
VulkanBasicCellRenderer::textGeometryUploadCount() const noexcept {
    return mTextGeometryUploadCount;
}

std::uint64_t
VulkanBasicCellRenderer::textGeometryUploadSkipCount() const noexcept {
    return mTextGeometryUploadSkipCount;
}

std::uint64_t
VulkanBasicCellRenderer::textGeometryUploadBytes() const noexcept {
    return mTextGeometryUploadBytes;
}

std::uint64_t
VulkanBasicCellRenderer::textGeometryBufferGrowthCount() const noexcept {
    return mTextGeometryBufferGrowthCount;
}

std::uint64_t VulkanBasicCellRenderer::pipelineBindCount() const noexcept {
    return mPipelineBindCount;
}

std::uint64_t VulkanBasicCellRenderer::activeCaptureSequence() const noexcept {
    return mHasActiveGeometry
            ? mGeometrySlots[mActiveGeometrySlot]
                      .plan.identity.captureSequence
            : 0;
}

std::uint64_t VulkanBasicCellRenderer::activeGeometryRevision() const noexcept {
    return mHasActiveGeometry
            ? mGeometrySlots[mActiveGeometrySlot]
                      .plan.identity.geometryRevision
            : 0;
}

std::uint64_t VulkanBasicCellRenderer::activeInstanceRevision() const noexcept {
    return mHasActiveGeometry
            ? mGeometrySlots[mActiveGeometrySlot]
                      .plan.identity.instanceRevision
            : 0;
}

std::uint32_t VulkanBasicCellRenderer::dotTextureWidth() const noexcept {
    return mDotTextureWidth;
}

std::uint32_t VulkanBasicCellRenderer::dotTextureHeight() const noexcept {
    return mDotTextureHeight;
}

std::uint32_t VulkanBasicCellRenderer::besselTextureWidth() const noexcept {
    return mBesselTextureWidth;
}

std::uint32_t VulkanBasicCellRenderer::besselTextureHeight() const noexcept {
    return mBesselTextureHeight;
}

std::uint32_t VulkanBasicCellRenderer::glyphAtlasWidth() const noexcept {
    return mGlyphAtlasWidth;
}

std::uint32_t VulkanBasicCellRenderer::glyphAtlasHeight() const noexcept {
    return mGlyphAtlasHeight;
}

template <typename Handle>
void VulkanBasicCellRenderer::nameObject(VkObjectType type, Handle handle,
        const char* name) const noexcept {
    if (mSetDebugObjectName == nullptr || handle == VK_NULL_HANDLE) {
        return;
    }
    const VkDebugUtilsObjectNameInfoEXT info{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            nullptr,
            type,
            handleValue(handle),
            name,
    };
    mSetDebugObjectName(mDevice->device(), &info);
}

} // namespace cellularity::render::vulkan
