#include "render/backend/vulkan/VulkanResourceFoundation.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace cellularity::render::vulkan {
namespace {

constexpr std::size_t resourceIndex(VulkanResourceKind kind) noexcept {
    return static_cast<std::size_t>(kind);
}

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept {
    if (alignment <= 1) {
        return value;
    }
    const VkDeviceSize remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    const VkDeviceSize increment = alignment - remainder;
    if (value > std::numeric_limits<VkDeviceSize>::max() - increment) {
        return std::numeric_limits<VkDeviceSize>::max();
    }
    return value + increment;
}

} // namespace

void VulkanResourceCounters::resourceCreated(
        VulkanResourceKind kind) noexcept {
    VulkanResourceKindCounters& kindCounters = resources[resourceIndex(kind)];
    ++kindCounters.created;
    ++kindCounters.live;
    kindCounters.peakLive = std::max(
            kindCounters.peakLive, kindCounters.live);
    ++resourceCreateCount;
    ++liveResourceCount;
    peakLiveResourceCount = std::max(
            peakLiveResourceCount, liveResourceCount);
    if (insideSteadyStateFrame && insideControlledCapacityGrowth) {
        ++controlledCapacityResourceCreateCount;
    } else if (insideSteadyStateFrame) {
        ++steadyStateResourceCreateCount;
    }
}

void VulkanResourceCounters::resourceDestroyed(
        VulkanResourceKind kind) noexcept {
    VulkanResourceKindCounters& kindCounters = resources[resourceIndex(kind)];
    ++kindCounters.destroyed;
    if (kindCounters.live > 0) {
        --kindCounters.live;
    } else {
        ++resourceAccountingViolationCount;
    }
    ++resourceDestroyCount;
    if (liveResourceCount > 0) {
        --liveResourceCount;
    } else {
        ++resourceAccountingViolationCount;
    }
}

void VulkanResourceCounters::memoryAllocated(
        VkDeviceSize byteCount) noexcept {
    ++memoryAllocationCount;
    memoryBytesAllocatedTotal += byteCount;
    memoryBytesLive += byteCount;
    memoryBytesPeak = std::max(memoryBytesPeak, memoryBytesLive);
}

void VulkanResourceCounters::memoryFreed(VkDeviceSize byteCount) noexcept {
    ++memoryFreeCount;
    if (byteCount <= memoryBytesLive) {
        memoryBytesLive -= byteCount;
    } else {
        memoryBytesLive = 0;
        ++resourceAccountingViolationCount;
    }
}

const char* resourceKindName(VulkanResourceKind kind) noexcept {
    constexpr std::array<const char*,
            static_cast<std::size_t>(VulkanResourceKind::Count)>
            names = {
                    "buffer",
                    "device_memory",
                    "image",
                    "image_view",
                    "sampler",
                    "shader_module",
                    "descriptor_set_layout",
                    "descriptor_pool",
                    "descriptor_set",
                    "pipeline_layout",
                    "pipeline",
                    "command_pool",
                    "command_buffer",
                    "semaphore",
                    "fence",
                    "render_pass",
                    "framebuffer",
                    "swapchain",
                    "query_pool"
            };
    const std::size_t index = resourceIndex(kind);
    return index < names.size() ? names[index] : "unknown";
}

VulkanDeviceContext::VulkanDeviceContext(VkPhysicalDevice physicalDevice,
        VkDevice device,
        std::shared_ptr<VulkanResourceCounters> counters) noexcept
        : mPhysicalDevice(physicalDevice),
          mDevice(device),
          mCounters(std::move(counters)) {
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &mProperties);
    vkGetPhysicalDeviceMemoryProperties(
            mPhysicalDevice, &mMemoryProperties);
}

VulkanDeviceContext::~VulkanDeviceContext() {
    if (mDevice != VK_NULL_HANDLE) {
        ++mCounters->deviceDestructionCount;
        if (mCounters->liveResourceCount != 0) {
            ++mCounters->deviceDestructionWithLiveResourcesCount;
        }
        vkDestroyDevice(mDevice, nullptr);
    }
}

VkPhysicalDevice VulkanDeviceContext::physicalDevice() const noexcept {
    return mPhysicalDevice;
}

VkDevice VulkanDeviceContext::device() const noexcept {
    return mDevice;
}

const VkPhysicalDeviceProperties& VulkanDeviceContext::properties()
        const noexcept {
    return mProperties;
}

const VkPhysicalDeviceMemoryProperties&
VulkanDeviceContext::memoryProperties() const noexcept {
    return mMemoryProperties;
}

VulkanResourceCounters& VulkanDeviceContext::counters() const noexcept {
    return *mCounters;
}

std::optional<std::uint32_t> VulkanDeviceContext::findMemoryType(
        std::uint32_t typeBits, VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred) const noexcept {
    const auto find = [this, typeBits](VkMemoryPropertyFlags properties)
            -> std::optional<std::uint32_t> {
        for (std::uint32_t index = 0;
                index < mMemoryProperties.memoryTypeCount; ++index) {
            if ((typeBits & (1U << index)) != 0
                    && (mMemoryProperties.memoryTypes[index].propertyFlags
                               & properties)
                            == properties) {
                return index;
            }
        }
        return std::nullopt;
    };
    if (preferred != 0) {
        if (const auto preferredType = find(required | preferred);
                preferredType.has_value()) {
            return preferredType;
        }
    }
    return find(required);
}

VulkanDeviceMemory::~VulkanDeviceMemory() {
    reset();
}

VulkanDeviceMemory::VulkanDeviceMemory(
        VulkanDeviceMemory&& other) noexcept
        : mDevice(std::move(other.mDevice)),
          mMemory(std::exchange(other.mMemory, VkDeviceMemory{})),
          mSize(std::exchange(other.mSize, 0)),
          mProperties(std::exchange(other.mProperties, 0)),
          mMappedData(std::exchange(other.mMappedData, nullptr)) {}

VulkanDeviceMemory& VulkanDeviceMemory::operator=(
        VulkanDeviceMemory&& other) noexcept {
    if (this != &other) {
        reset();
        mDevice = std::move(other.mDevice);
        mMemory = std::exchange(other.mMemory, VkDeviceMemory{});
        mSize = std::exchange(other.mSize, 0);
        mProperties = std::exchange(other.mProperties, 0);
        mMappedData = std::exchange(other.mMappedData, nullptr);
    }
    return *this;
}

VkResult VulkanDeviceMemory::allocate(
        const std::shared_ptr<VulkanDeviceContext>& device,
        const VkMemoryRequirements& requirements,
        VkMemoryPropertyFlags requiredProperties,
        VkMemoryPropertyFlags preferredProperties,
        bool persistentlyMapped, VulkanDeviceMemory& output) noexcept {
    output.reset();
    const auto memoryType = device->findMemoryType(
            requirements.memoryTypeBits,
            requiredProperties,
            preferredProperties);
    if (!memoryType.has_value()) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    const VkMemoryAllocateInfo allocationInfo{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            requirements.size,
            *memoryType
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult result = vkAllocateMemory(
            device->device(), &allocationInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        return result;
    }

    output.mDevice = device;
    output.mMemory = memory;
    output.mSize = requirements.size;
    output.mProperties = device->memoryProperties()
                                 .memoryTypes[*memoryType]
                                 .propertyFlags;
    device->counters().resourceCreated(VulkanResourceKind::DeviceMemory);
    device->counters().memoryAllocated(requirements.size);

    if (persistentlyMapped) {
        result = vkMapMemory(device->device(), memory, 0,
                VK_WHOLE_SIZE, 0, &output.mMappedData);
        if (result != VK_SUCCESS) {
            output.reset();
            return result;
        }
    }
    return VK_SUCCESS;
}

void VulkanDeviceMemory::reset() noexcept {
    if (mMemory != VK_NULL_HANDLE) {
        if (mMappedData != nullptr) {
            vkUnmapMemory(mDevice->device(), mMemory);
        }
        vkFreeMemory(mDevice->device(), mMemory, nullptr);
        mDevice->counters().memoryFreed(mSize);
        mDevice->counters().resourceDestroyed(
                VulkanResourceKind::DeviceMemory);
    }
    mMappedData = nullptr;
    mMemory = VK_NULL_HANDLE;
    mSize = 0;
    mProperties = 0;
    mDevice.reset();
}

VkDeviceMemory VulkanDeviceMemory::get() const noexcept {
    return mMemory;
}

void* VulkanDeviceMemory::mappedData() const noexcept {
    return mMappedData;
}

VkDeviceSize VulkanDeviceMemory::size() const noexcept {
    return mSize;
}

VkMemoryPropertyFlags VulkanDeviceMemory::properties() const noexcept {
    return mProperties;
}

bool VulkanDeviceMemory::isHostCoherent() const noexcept {
    return (mProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

VkResult VulkanDeviceMemory::flush(
        VkDeviceSize offset, VkDeviceSize size) noexcept {
    if (isHostCoherent() || size == 0) {
        return VK_SUCCESS;
    }
    if (mMappedData == nullptr || offset >= mSize || size > mSize - offset) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    const VkDeviceSize atomSize = std::max<VkDeviceSize>(
            mDevice->properties().limits.nonCoherentAtomSize, 1);
    const VkDeviceSize alignedOffset = offset - offset % atomSize;
    const VkMappedMemoryRange range{
            VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            nullptr,
            mMemory,
            alignedOffset,
            VK_WHOLE_SIZE
    };
    const VkResult result = vkFlushMappedMemoryRanges(
            mDevice->device(), 1, &range);
    if (result == VK_SUCCESS) {
        ++mDevice->counters().uploadFlushCount;
    }
    return result;
}

VkResult VulkanDeviceMemory::invalidate(
        VkDeviceSize offset, VkDeviceSize size) noexcept {
    if (isHostCoherent() || size == 0) {
        return VK_SUCCESS;
    }
    if (mMappedData == nullptr || offset >= mSize || size > mSize - offset) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    const VkDeviceSize atomSize = std::max<VkDeviceSize>(
            mDevice->properties().limits.nonCoherentAtomSize, 1);
    const VkDeviceSize alignedOffset = offset - offset % atomSize;
    const VkMappedMemoryRange range{
            VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            nullptr,
            mMemory,
            alignedOffset,
            VK_WHOLE_SIZE
    };
    return vkInvalidateMappedMemoryRanges(mDevice->device(), 1, &range);
}

VulkanBuffer::~VulkanBuffer() {
    reset();
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
        : mDevice(std::move(other.mDevice)),
          mBuffer(std::exchange(other.mBuffer, VkBuffer{})),
          mSize(std::exchange(other.mSize, 0)),
          mMemory(std::move(other.mMemory)) {}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        mDevice = std::move(other.mDevice);
        mBuffer = std::exchange(other.mBuffer, VkBuffer{});
        mSize = std::exchange(other.mSize, 0);
        mMemory = std::move(other.mMemory);
    }
    return *this;
}

VkResult VulkanBuffer::create(
        const std::shared_ptr<VulkanDeviceContext>& device,
        VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredProperties,
        VkMemoryPropertyFlags preferredProperties,
        bool persistentlyMapped, VulkanBuffer& output) noexcept {
    if (size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VulkanBuffer candidate;
    const VkBufferCreateInfo createInfo{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            size,
            usage,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr
    };
    VkResult result = vkCreateBuffer(
            device->device(), &createInfo, nullptr, &candidate.mBuffer);
    if (result != VK_SUCCESS) {
        return result;
    }
    candidate.mDevice = device;
    candidate.mSize = size;
    device->counters().resourceCreated(VulkanResourceKind::Buffer);

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(
            device->device(), candidate.mBuffer, &requirements);
    result = VulkanDeviceMemory::allocate(device, requirements,
            requiredProperties, preferredProperties,
            persistentlyMapped, candidate.mMemory);
    if (result != VK_SUCCESS) {
        return result;
    }
    result = vkBindBufferMemory(device->device(), candidate.mBuffer,
            candidate.mMemory.get(), 0);
    if (result != VK_SUCCESS) {
        return result;
    }

    output = std::move(candidate);
    return VK_SUCCESS;
}

void VulkanBuffer::reset() noexcept {
    if (mBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(mDevice->device(), mBuffer, nullptr);
        mDevice->counters().resourceDestroyed(VulkanResourceKind::Buffer);
        mBuffer = VK_NULL_HANDLE;
    }
    mMemory.reset();
    mSize = 0;
    mDevice.reset();
}

VkBuffer VulkanBuffer::get() const noexcept {
    return mBuffer;
}

VkDeviceMemory VulkanBuffer::memory() const noexcept {
    return mMemory.get();
}

void* VulkanBuffer::mappedData() const noexcept {
    return mMemory.mappedData();
}

VkDeviceSize VulkanBuffer::size() const noexcept {
    return mSize;
}

bool VulkanBuffer::isHostCoherent() const noexcept {
    return mMemory.isHostCoherent();
}

VkResult VulkanBuffer::flush(
        VkDeviceSize offset, VkDeviceSize size) noexcept {
    return mMemory.flush(offset, size);
}

VkResult VulkanBuffer::invalidate(
        VkDeviceSize offset, VkDeviceSize size) noexcept {
    return mMemory.invalidate(offset, size);
}

VulkanImage::~VulkanImage() {
    reset();
}

VulkanImage::VulkanImage(VulkanImage&& other) noexcept
        : mDevice(std::move(other.mDevice)),
          mImage(std::exchange(other.mImage, VkImage{})),
          mMemory(std::move(other.mMemory)) {}

VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept {
    if (this != &other) {
        reset();
        mDevice = std::move(other.mDevice);
        mImage = std::exchange(other.mImage, VkImage{});
        mMemory = std::move(other.mMemory);
    }
    return *this;
}

VkResult VulkanImage::create(
        const std::shared_ptr<VulkanDeviceContext>& device,
        const VkImageCreateInfo& createInfo,
        VkMemoryPropertyFlags requiredProperties,
        VkMemoryPropertyFlags preferredProperties,
        VulkanImage& output) noexcept {
    VulkanImage candidate;
    VkResult result = vkCreateImage(
            device->device(), &createInfo, nullptr, &candidate.mImage);
    if (result != VK_SUCCESS) {
        return result;
    }
    candidate.mDevice = device;
    device->counters().resourceCreated(VulkanResourceKind::Image);

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(
            device->device(), candidate.mImage, &requirements);
    result = VulkanDeviceMemory::allocate(device, requirements,
            requiredProperties, preferredProperties,
            false, candidate.mMemory);
    if (result != VK_SUCCESS) {
        return result;
    }
    result = vkBindImageMemory(device->device(), candidate.mImage,
            candidate.mMemory.get(), 0);
    if (result != VK_SUCCESS) {
        return result;
    }

    output = std::move(candidate);
    return VK_SUCCESS;
}

void VulkanImage::reset() noexcept {
    if (mImage != VK_NULL_HANDLE) {
        vkDestroyImage(mDevice->device(), mImage, nullptr);
        mDevice->counters().resourceDestroyed(VulkanResourceKind::Image);
        mImage = VK_NULL_HANDLE;
    }
    mMemory.reset();
    mDevice.reset();
}

VkImage VulkanImage::get() const noexcept {
    return mImage;
}

VkDeviceMemory VulkanImage::memory() const noexcept {
    return mMemory.get();
}

class VulkanDescriptorPoolState final {
public:
    explicit VulkanDescriptorPoolState(
            std::shared_ptr<VulkanDeviceContext> device) noexcept
            : mDevice(std::move(device)) {}

    void adopt(VkDescriptorPool descriptorPool) noexcept {
        mDescriptorPool = descriptorPool;
        mDevice->counters().resourceCreated(
                VulkanResourceKind::DescriptorPool);
    }

    ~VulkanDescriptorPoolState() {
        if (mDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(
                    mDevice->device(), mDescriptorPool, nullptr);
            mDevice->counters().resourceDestroyed(
                    VulkanResourceKind::DescriptorPool);
        }
    }

    [[nodiscard]] VkDevice device() const noexcept {
        return mDevice->device();
    }

    [[nodiscard]] VkDescriptorPool pool() const noexcept {
        return mDescriptorPool;
    }

    void descriptorSetAllocated() noexcept {
        mDevice->counters().resourceCreated(
                VulkanResourceKind::DescriptorSet);
    }

    void free(VkDescriptorSet descriptorSet) noexcept {
        (void)vkFreeDescriptorSets(
                mDevice->device(), mDescriptorPool, 1, &descriptorSet);
        mDevice->counters().resourceDestroyed(
                VulkanResourceKind::DescriptorSet);
    }

private:
    std::shared_ptr<VulkanDeviceContext> mDevice;
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
};

VulkanDescriptorSet::VulkanDescriptorSet(
        std::shared_ptr<VulkanDescriptorPoolState> pool,
        VkDescriptorSet descriptorSet) noexcept
        : mPool(std::move(pool)), mDescriptorSet(descriptorSet) {
    mPool->descriptorSetAllocated();
}

VulkanDescriptorSet::~VulkanDescriptorSet() {
    reset();
}

VulkanDescriptorSet::VulkanDescriptorSet(
        VulkanDescriptorSet&& other) noexcept
        : mPool(std::move(other.mPool)),
          mDescriptorSet(std::exchange(
                  other.mDescriptorSet, VkDescriptorSet{})) {}

VulkanDescriptorSet& VulkanDescriptorSet::operator=(
        VulkanDescriptorSet&& other) noexcept {
    if (this != &other) {
        reset();
        mPool = std::move(other.mPool);
        mDescriptorSet = std::exchange(
                other.mDescriptorSet, VkDescriptorSet{});
    }
    return *this;
}

void VulkanDescriptorSet::reset() noexcept {
    if (mDescriptorSet != VK_NULL_HANDLE) {
        mPool->free(mDescriptorSet);
        mDescriptorSet = VK_NULL_HANDLE;
    }
    mPool.reset();
}

VkDescriptorSet VulkanDescriptorSet::get() const noexcept {
    return mDescriptorSet;
}

VkResult VulkanDescriptorPool::create(
        const std::shared_ptr<VulkanDeviceContext>& device,
        const VkDescriptorPoolCreateInfo& createInfo,
        VulkanDescriptorPool& output) {
    if ((createInfo.flags
                & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
            == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    // Allocate the shared parent before creating the Vulkan handle. If host
    // allocation fails, there is no already-created handle to leak.
    auto state = std::make_shared<VulkanDescriptorPoolState>(device);
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    const VkResult result = vkCreateDescriptorPool(
            device->device(), &createInfo, nullptr, &descriptorPool);
    if (result != VK_SUCCESS) {
        return result;
    }
    state->adopt(descriptorPool);
    output.mState = std::move(state);
    return VK_SUCCESS;
}

VkResult VulkanDescriptorPool::allocate(
        const std::vector<VkDescriptorSetLayout>& layouts,
        std::vector<VulkanDescriptorSet>& output) const {
    if (!mState || layouts.empty()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkDescriptorSet> descriptorSets(
            layouts.size(), VK_NULL_HANDLE);
    std::vector<VulkanDescriptorSet> ownedDescriptorSets;
    // Reserve before vkAllocateDescriptorSets so subsequent wrapper creation
    // cannot allocate and strand raw descriptor sets on a host OOM.
    ownedDescriptorSets.reserve(descriptorSets.size());
    const VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            nullptr,
            mState->pool(),
            static_cast<std::uint32_t>(layouts.size()),
            layouts.data()
    };
    const VkResult result = vkAllocateDescriptorSets(
            mState->device(), &allocateInfo, descriptorSets.data());
    if (result != VK_SUCCESS) {
        return result;
    }
    for (const VkDescriptorSet descriptorSet : descriptorSets) {
        ownedDescriptorSets.push_back(
                VulkanDescriptorSet(mState, descriptorSet));
    }
    output = std::move(ownedDescriptorSets);
    return VK_SUCCESS;
}

void VulkanDescriptorPool::reset() noexcept {
    mState.reset();
}

VkDescriptorPool VulkanDescriptorPool::get() const noexcept {
    return mState ? mState->pool() : VK_NULL_HANDLE;
}

class VulkanCommandPoolState final {
public:
    explicit VulkanCommandPoolState(
            std::shared_ptr<VulkanDeviceContext> device) noexcept
            : mDevice(std::move(device)) {}

    void adopt(VkCommandPool commandPool) noexcept {
        mCommandPool = commandPool;
        mDevice->counters().resourceCreated(VulkanResourceKind::CommandPool);
    }

    ~VulkanCommandPoolState() {
        if (mCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(mDevice->device(), mCommandPool, nullptr);
            mDevice->counters().resourceDestroyed(
                    VulkanResourceKind::CommandPool);
        }
    }

    [[nodiscard]] VkDevice device() const noexcept {
        return mDevice->device();
    }

    [[nodiscard]] VkCommandPool pool() const noexcept {
        return mCommandPool;
    }

    void commandBufferAllocated() noexcept {
        mDevice->counters().resourceCreated(
                VulkanResourceKind::CommandBuffer);
    }

    void free(VkCommandBuffer commandBuffer) noexcept {
        vkFreeCommandBuffers(
                mDevice->device(), mCommandPool, 1, &commandBuffer);
        mDevice->counters().resourceDestroyed(
                VulkanResourceKind::CommandBuffer);
    }

private:
    std::shared_ptr<VulkanDeviceContext> mDevice;
    VkCommandPool mCommandPool = VK_NULL_HANDLE;
};

VulkanCommandBuffer::VulkanCommandBuffer(
        std::shared_ptr<VulkanCommandPoolState> pool,
        VkCommandBuffer commandBuffer) noexcept
        : mPool(std::move(pool)), mCommandBuffer(commandBuffer) {
    mPool->commandBufferAllocated();
}

VulkanCommandBuffer::~VulkanCommandBuffer() {
    reset();
}

VulkanCommandBuffer::VulkanCommandBuffer(
        VulkanCommandBuffer&& other) noexcept
        : mPool(std::move(other.mPool)),
          mCommandBuffer(std::exchange(
                  other.mCommandBuffer, VkCommandBuffer{})) {}

VulkanCommandBuffer& VulkanCommandBuffer::operator=(
        VulkanCommandBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        mPool = std::move(other.mPool);
        mCommandBuffer = std::exchange(
                other.mCommandBuffer, VkCommandBuffer{});
    }
    return *this;
}

void VulkanCommandBuffer::reset() noexcept {
    if (mCommandBuffer != VK_NULL_HANDLE) {
        mPool->free(mCommandBuffer);
        mCommandBuffer = VK_NULL_HANDLE;
    }
    mPool.reset();
}

VkCommandBuffer VulkanCommandBuffer::get() const noexcept {
    return mCommandBuffer;
}

VkResult VulkanCommandPool::create(
        const std::shared_ptr<VulkanDeviceContext>& device,
        const VkCommandPoolCreateInfo& createInfo,
        VulkanCommandPool& output) {
    auto state = std::make_shared<VulkanCommandPoolState>(device);
    VkCommandPool commandPool = VK_NULL_HANDLE;
    const VkResult result = vkCreateCommandPool(
            device->device(), &createInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        return result;
    }
    state->adopt(commandPool);
    output.mState = std::move(state);
    return VK_SUCCESS;
}

VkResult VulkanCommandPool::allocatePrimary(std::uint32_t count,
        std::vector<VulkanCommandBuffer>& output) const {
    if (!mState || count == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkCommandBuffer> commandBuffers(
            count, VK_NULL_HANDLE);
    std::vector<VulkanCommandBuffer> ownedCommandBuffers;
    ownedCommandBuffers.reserve(commandBuffers.size());
    const VkCommandBufferAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            nullptr,
            mState->pool(),
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            count
    };
    const VkResult result = vkAllocateCommandBuffers(
            mState->device(), &allocateInfo, commandBuffers.data());
    if (result != VK_SUCCESS) {
        return result;
    }
    for (const VkCommandBuffer commandBuffer : commandBuffers) {
        ownedCommandBuffers.push_back(
                VulkanCommandBuffer(mState, commandBuffer));
    }
    output = std::move(ownedCommandBuffers);
    return VK_SUCCESS;
}

void VulkanCommandPool::reset() noexcept {
    mState.reset();
}

VkCommandPool VulkanCommandPool::get() const noexcept {
    return mState ? mState->pool() : VK_NULL_HANDLE;
}

VulkanDeferredReleaseQueue::VulkanDeferredReleaseQueue(
        std::shared_ptr<VulkanResourceCounters> counters)
        : mCounters(std::move(counters)) {
    mEntries.reserve(4);
}

VulkanDeferredReleaseQueue::~VulkanDeferredReleaseQueue() {
    releaseAll();
}

void VulkanDeferredReleaseQueue::releaseAll() noexcept {
    const std::size_t releaseCount = mEntries.size();
    mEntries.clear();
    if (mCounters) {
        mCounters->deferredReleaseDestroyCount += releaseCount;
        const std::uint64_t count = static_cast<std::uint64_t>(releaseCount);
        mCounters->deferredReleaseLiveCount =
                count <= mCounters->deferredReleaseLiveCount
                ? mCounters->deferredReleaseLiveCount - count
                : 0;
    }
}

std::size_t VulkanDeferredReleaseQueue::size() const noexcept {
    return mEntries.size();
}

VkResult VulkanUploadArena::initialize(
        const std::shared_ptr<VulkanDeviceContext>& device,
        VkDeviceSize initialCapacity) noexcept {
    reset();
    if (initialCapacity == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    mDevice = device;
    VulkanBuffer buffer;
    const VkResult result = createBuffer(initialCapacity, buffer);
    if (result != VK_SUCCESS) {
        mDevice.reset();
        return result;
    }
    mBuffer = std::move(buffer);
    mCapacity = initialCapacity;
    return VK_SUCCESS;
}

VkResult VulkanUploadArena::beginFrame(VkDeviceSize requiredCapacity,
        VulkanDeferredReleaseQueue& deferredReleases,
        bool& bufferChanged) noexcept {
    bufferChanged = false;
    mCursor = 0;
    if (requiredCapacity <= mCapacity) {
        return VK_SUCCESS;
    }
    VkDeviceSize newCapacity = std::max<VkDeviceSize>(mCapacity, 1);
    while (newCapacity < requiredCapacity) {
        if (newCapacity > std::numeric_limits<VkDeviceSize>::max() / 2) {
            newCapacity = requiredCapacity;
            break;
        }
        newCapacity *= 2;
    }

    VulkanBuffer replacement;
    const bool previousGrowthState =
            mDevice->counters().insideControlledCapacityGrowth;
    mDevice->counters().insideControlledCapacityGrowth = true;
    const VkResult result = createBuffer(newCapacity, replacement);
    mDevice->counters().insideControlledCapacityGrowth = previousGrowthState;
    if (result != VK_SUCCESS) {
        return result;
    }
    deferredReleases.retire(std::move(mBuffer));
    mBuffer = std::move(replacement);
    mCapacity = newCapacity;
    bufferChanged = true;
    ++mDevice->counters().uploadArenaGrowthCount;
    return VK_SUCCESS;
}

std::optional<VulkanUploadSlice> VulkanUploadArena::write(
        const void* source, VkDeviceSize size, VkDeviceSize alignment,
        VkResult& result) noexcept {
    if (source == nullptr || size == 0 || mBuffer.get() == VK_NULL_HANDLE) {
        result = VK_ERROR_INITIALIZATION_FAILED;
        return std::nullopt;
    }
    const VkDeviceSize offset = alignUp(mCursor, std::max<VkDeviceSize>(alignment, 1));
    if (offset == std::numeric_limits<VkDeviceSize>::max()
            || offset > mCapacity || size > mCapacity - offset) {
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        return std::nullopt;
    }

    auto* destination = static_cast<std::byte*>(mBuffer.mappedData()) + offset;
    std::memcpy(destination, source, static_cast<std::size_t>(size));
    result = mBuffer.flush(offset, size);
    if (result != VK_SUCCESS) {
        return std::nullopt;
    }
    mCursor = offset + size;
    mDevice->counters().uploadBytes += size;
    return VulkanUploadSlice{
            mBuffer.get(),
            offset,
            size,
            destination
    };
}

void VulkanUploadArena::reset() noexcept {
    mBuffer.reset();
    mCapacity = 0;
    mCursor = 0;
    mDevice.reset();
}

VkBuffer VulkanUploadArena::buffer() const noexcept {
    return mBuffer.get();
}

VkDeviceSize VulkanUploadArena::capacity() const noexcept {
    return mCapacity;
}

VkDeviceSize VulkanUploadArena::used() const noexcept {
    return mCursor;
}

VkResult VulkanUploadArena::createBuffer(
        VkDeviceSize capacity, VulkanBuffer& output) const noexcept {
    return VulkanBuffer::create(mDevice, capacity,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                    | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                    | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            true, output);
}

} // namespace cellularity::render::vulkan
