#pragma once

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace cellularity::render::vulkan {

enum class VulkanResourceKind : std::size_t {
    Buffer,
    DeviceMemory,
    Image,
    ImageView,
    Sampler,
    ShaderModule,
    DescriptorSetLayout,
    DescriptorPool,
    DescriptorSet,
    PipelineLayout,
    Pipeline,
    CommandPool,
    CommandBuffer,
    Semaphore,
    Fence,
    RenderPass,
    Framebuffer,
    Swapchain,
    QueryPool,
    Count
};

struct VulkanResourceKindCounters final {
    std::uint64_t created = 0;
    std::uint64_t destroyed = 0;
    std::uint64_t live = 0;
    std::uint64_t peakLive = 0;
};

struct VulkanResourceCounters final {
    // Mutated only by the dedicated Vulkan render thread. Validation callback
    // counters remain separately synchronized by the session.
    std::array<VulkanResourceKindCounters,
            static_cast<std::size_t>(VulkanResourceKind::Count)>
            resources{};
    std::uint64_t resourceCreateCount = 0;
    std::uint64_t resourceDestroyCount = 0;
    std::uint64_t resourceAccountingViolationCount = 0;
    std::uint64_t liveResourceCount = 0;
    std::uint64_t peakLiveResourceCount = 0;
    std::uint64_t steadyStateResourceCreateCount = 0;
    std::uint64_t controlledCapacityResourceCreateCount = 0;
    std::uint64_t memoryAllocationCount = 0;
    std::uint64_t memoryFreeCount = 0;
    std::uint64_t memoryBytesAllocatedTotal = 0;
    std::uint64_t memoryBytesLive = 0;
    std::uint64_t memoryBytesPeak = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t uploadFlushCount = 0;
    std::uint64_t uploadArenaGrowthCount = 0;
    std::uint64_t commandBufferRecordCount = 0;
    std::uint64_t descriptorBindCount = 0;
    std::uint64_t descriptorUpdateCount = 0;
    std::uint64_t queueSubmissionCount = 0;
    std::uint64_t fenceWaitCount = 0;
    std::uint64_t blockingFenceWaitCount = 0;
    std::uint64_t deviceWaitIdleCount = 0;
    std::uint64_t deferredReleaseRetireCount = 0;
    std::uint64_t deferredReleaseDestroyCount = 0;
    std::uint64_t deferredReleaseLiveCount = 0;
    std::uint64_t deferredReleasePeakLiveCount = 0;
    std::uint64_t synchronizationReuseViolationCount = 0;
    std::uint64_t deviceDestructionCount = 0;
    std::uint64_t deviceDestructionWithLiveResourcesCount = 0;
    bool insideSteadyStateFrame = false;
    bool insideControlledCapacityGrowth = false;

    void resourceCreated(VulkanResourceKind kind) noexcept;
    void resourceDestroyed(VulkanResourceKind kind) noexcept;
    void memoryAllocated(VkDeviceSize byteCount) noexcept;
    void memoryFreed(VkDeviceSize byteCount) noexcept;
};

[[nodiscard]] const char* resourceKindName(
        VulkanResourceKind kind) noexcept;

class VulkanDeviceContext final {
public:
    // Child wrappers retain this context, so VkDevice cannot be destroyed
    // before the last resource that names it.
    VulkanDeviceContext(VkPhysicalDevice physicalDevice, VkDevice device,
            std::shared_ptr<VulkanResourceCounters> counters) noexcept;
    ~VulkanDeviceContext();

    VulkanDeviceContext(const VulkanDeviceContext&) = delete;
    VulkanDeviceContext& operator=(const VulkanDeviceContext&) = delete;

    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept;
    [[nodiscard]] VkDevice device() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceProperties& properties() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceMemoryProperties& memoryProperties()
            const noexcept;
    [[nodiscard]] VulkanResourceCounters& counters() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> findMemoryType(
            std::uint32_t typeBits, VkMemoryPropertyFlags required,
            VkMemoryPropertyFlags preferred = 0) const noexcept;

private:
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties mProperties{};
    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    std::shared_ptr<VulkanResourceCounters> mCounters;
};

template <typename Handle, auto DestroyFunction, VulkanResourceKind Kind>
class VulkanUniqueDeviceHandle final {
public:
    VulkanUniqueDeviceHandle() = default;

    VulkanUniqueDeviceHandle(
            std::shared_ptr<VulkanDeviceContext> device, Handle handle) noexcept
            : mDevice(std::move(device)), mHandle(handle) {
        if (mHandle != VK_NULL_HANDLE) {
            mDevice->counters().resourceCreated(Kind);
        }
    }

    ~VulkanUniqueDeviceHandle() {
        reset();
    }

    VulkanUniqueDeviceHandle(const VulkanUniqueDeviceHandle&) = delete;
    VulkanUniqueDeviceHandle& operator=(
            const VulkanUniqueDeviceHandle&) = delete;

    VulkanUniqueDeviceHandle(VulkanUniqueDeviceHandle&& other) noexcept
            : mDevice(std::move(other.mDevice)),
              mHandle(std::exchange(other.mHandle, Handle{})) {}

    VulkanUniqueDeviceHandle& operator=(
            VulkanUniqueDeviceHandle&& other) noexcept {
        if (this != &other) {
            reset();
            mDevice = std::move(other.mDevice);
            mHandle = std::exchange(other.mHandle, Handle{});
        }
        return *this;
    }

    void reset() noexcept {
        if (mHandle != VK_NULL_HANDLE) {
            DestroyFunction(mDevice->device(), mHandle, nullptr);
            mDevice->counters().resourceDestroyed(Kind);
            mHandle = VK_NULL_HANDLE;
        }
        mDevice.reset();
    }

    [[nodiscard]] Handle get() const noexcept {
        return mHandle;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return mHandle != VK_NULL_HANDLE;
    }

private:
    std::shared_ptr<VulkanDeviceContext> mDevice;
    Handle mHandle = VK_NULL_HANDLE;
};

using VulkanImageView = VulkanUniqueDeviceHandle<VkImageView,
        vkDestroyImageView, VulkanResourceKind::ImageView>;
using VulkanSampler = VulkanUniqueDeviceHandle<VkSampler,
        vkDestroySampler, VulkanResourceKind::Sampler>;
using VulkanShaderModule = VulkanUniqueDeviceHandle<VkShaderModule,
        vkDestroyShaderModule, VulkanResourceKind::ShaderModule>;
using VulkanDescriptorSetLayout =
        VulkanUniqueDeviceHandle<VkDescriptorSetLayout,
                vkDestroyDescriptorSetLayout,
                VulkanResourceKind::DescriptorSetLayout>;
using VulkanPipelineLayout = VulkanUniqueDeviceHandle<VkPipelineLayout,
        vkDestroyPipelineLayout, VulkanResourceKind::PipelineLayout>;
using VulkanPipeline = VulkanUniqueDeviceHandle<VkPipeline,
        vkDestroyPipeline, VulkanResourceKind::Pipeline>;
using VulkanSemaphore = VulkanUniqueDeviceHandle<VkSemaphore,
        vkDestroySemaphore, VulkanResourceKind::Semaphore>;
using VulkanFence = VulkanUniqueDeviceHandle<VkFence,
        vkDestroyFence, VulkanResourceKind::Fence>;
using VulkanRenderPass = VulkanUniqueDeviceHandle<VkRenderPass,
        vkDestroyRenderPass, VulkanResourceKind::RenderPass>;
using VulkanFramebuffer = VulkanUniqueDeviceHandle<VkFramebuffer,
        vkDestroyFramebuffer, VulkanResourceKind::Framebuffer>;
using VulkanSwapchain = VulkanUniqueDeviceHandle<VkSwapchainKHR,
        vkDestroySwapchainKHR, VulkanResourceKind::Swapchain>;
using VulkanQueryPool = VulkanUniqueDeviceHandle<VkQueryPool,
        vkDestroyQueryPool, VulkanResourceKind::QueryPool>;

class VulkanDeviceMemory final {
public:
    VulkanDeviceMemory() = default;
    ~VulkanDeviceMemory();

    VulkanDeviceMemory(const VulkanDeviceMemory&) = delete;
    VulkanDeviceMemory& operator=(const VulkanDeviceMemory&) = delete;
    VulkanDeviceMemory(VulkanDeviceMemory&& other) noexcept;
    VulkanDeviceMemory& operator=(VulkanDeviceMemory&& other) noexcept;

    static VkResult allocate(
            const std::shared_ptr<VulkanDeviceContext>& device,
            const VkMemoryRequirements& requirements,
            VkMemoryPropertyFlags requiredProperties,
            VkMemoryPropertyFlags preferredProperties,
            bool persistentlyMapped, VulkanDeviceMemory& output) noexcept;

    void reset() noexcept;
    [[nodiscard]] VkDeviceMemory get() const noexcept;
    [[nodiscard]] void* mappedData() const noexcept;
    [[nodiscard]] VkDeviceSize size() const noexcept;
    [[nodiscard]] VkMemoryPropertyFlags properties() const noexcept;
    [[nodiscard]] bool isHostCoherent() const noexcept;
    [[nodiscard]] VkResult flush(
            VkDeviceSize offset, VkDeviceSize size) noexcept;
    [[nodiscard]] VkResult invalidate(
            VkDeviceSize offset, VkDeviceSize size) noexcept;

private:
    std::shared_ptr<VulkanDeviceContext> mDevice;
    VkDeviceMemory mMemory = VK_NULL_HANDLE;
    VkDeviceSize mSize = 0;
    VkMemoryPropertyFlags mProperties = 0;
    void* mMappedData = nullptr;
};

class VulkanBuffer final {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    static VkResult create(
            const std::shared_ptr<VulkanDeviceContext>& device,
            VkDeviceSize size, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags requiredProperties,
            VkMemoryPropertyFlags preferredProperties,
            bool persistentlyMapped, VulkanBuffer& output) noexcept;

    void reset() noexcept;
    [[nodiscard]] VkBuffer get() const noexcept;
    [[nodiscard]] VkDeviceMemory memory() const noexcept;
    [[nodiscard]] void* mappedData() const noexcept;
    [[nodiscard]] VkDeviceSize size() const noexcept;
    [[nodiscard]] bool isHostCoherent() const noexcept;
    [[nodiscard]] VkResult flush(
            VkDeviceSize offset, VkDeviceSize size) noexcept;
    [[nodiscard]] VkResult invalidate(
            VkDeviceSize offset, VkDeviceSize size) noexcept;

private:
    std::shared_ptr<VulkanDeviceContext> mDevice;
    VkBuffer mBuffer = VK_NULL_HANDLE;
    VkDeviceSize mSize = 0;
    VulkanDeviceMemory mMemory;
};

class VulkanImage final {
public:
    VulkanImage() = default;
    ~VulkanImage();

    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    VulkanImage(VulkanImage&& other) noexcept;
    VulkanImage& operator=(VulkanImage&& other) noexcept;

    static VkResult create(
            const std::shared_ptr<VulkanDeviceContext>& device,
            const VkImageCreateInfo& createInfo,
            VkMemoryPropertyFlags requiredProperties,
            VkMemoryPropertyFlags preferredProperties,
            VulkanImage& output) noexcept;

    void reset() noexcept;
    [[nodiscard]] VkImage get() const noexcept;
    [[nodiscard]] VkDeviceMemory memory() const noexcept;

private:
    std::shared_ptr<VulkanDeviceContext> mDevice;
    VkImage mImage = VK_NULL_HANDLE;
    VulkanDeviceMemory mMemory;
};

class VulkanDescriptorPoolState;

class VulkanDescriptorSet final {
public:
    // Retains its descriptor pool and frees the set individually. The fixed
    // pool therefore cannot disappear while a frame still owns a set.
    VulkanDescriptorSet() = default;
    ~VulkanDescriptorSet();

    VulkanDescriptorSet(const VulkanDescriptorSet&) = delete;
    VulkanDescriptorSet& operator=(const VulkanDescriptorSet&) = delete;
    VulkanDescriptorSet(VulkanDescriptorSet&& other) noexcept;
    VulkanDescriptorSet& operator=(VulkanDescriptorSet&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] VkDescriptorSet get() const noexcept;

private:
    friend class VulkanDescriptorPool;
    VulkanDescriptorSet(std::shared_ptr<VulkanDescriptorPoolState> pool,
            VkDescriptorSet descriptorSet) noexcept;

    std::shared_ptr<VulkanDescriptorPoolState> mPool;
    VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;
};

class VulkanDescriptorPool final {
public:
    VulkanDescriptorPool() = default;

    static VkResult create(
            const std::shared_ptr<VulkanDeviceContext>& device,
            const VkDescriptorPoolCreateInfo& createInfo,
            VulkanDescriptorPool& output);
    [[nodiscard]] VkResult allocate(
            const std::vector<VkDescriptorSetLayout>& layouts,
            std::vector<VulkanDescriptorSet>& output) const;
    void reset() noexcept;
    [[nodiscard]] VkDescriptorPool get() const noexcept;

private:
    std::shared_ptr<VulkanDescriptorPoolState> mState;
};

class VulkanCommandPoolState;

class VulkanCommandBuffer final {
public:
    // Retains the command pool from which it was allocated.
    VulkanCommandBuffer() = default;
    ~VulkanCommandBuffer();

    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer&& other) noexcept;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] VkCommandBuffer get() const noexcept;

private:
    friend class VulkanCommandPool;
    VulkanCommandBuffer(std::shared_ptr<VulkanCommandPoolState> pool,
            VkCommandBuffer commandBuffer) noexcept;

    std::shared_ptr<VulkanCommandPoolState> mPool;
    VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
};

class VulkanCommandPool final {
public:
    VulkanCommandPool() = default;

    static VkResult create(
            const std::shared_ptr<VulkanDeviceContext>& device,
            const VkCommandPoolCreateInfo& createInfo,
            VulkanCommandPool& output);
    [[nodiscard]] VkResult allocatePrimary(std::uint32_t count,
            std::vector<VulkanCommandBuffer>& output) const;
    void reset() noexcept;
    [[nodiscard]] VkCommandPool get() const noexcept;

private:
    std::shared_ptr<VulkanCommandPoolState> mState;
};

class VulkanDeferredReleaseQueue final {
public:
    // releaseAll() may be called only after the owning frame fence completes.
    VulkanDeferredReleaseQueue() = default;
    explicit VulkanDeferredReleaseQueue(
            std::shared_ptr<VulkanResourceCounters> counters);
    ~VulkanDeferredReleaseQueue();

    VulkanDeferredReleaseQueue(const VulkanDeferredReleaseQueue&) = delete;
    VulkanDeferredReleaseQueue& operator=(
            const VulkanDeferredReleaseQueue&) = delete;
    VulkanDeferredReleaseQueue(VulkanDeferredReleaseQueue&&) noexcept = default;
    VulkanDeferredReleaseQueue& operator=(
            VulkanDeferredReleaseQueue&&) noexcept = default;

    template <typename Resource>
    void retire(Resource&& resource) {
        using StoredResource = std::decay_t<Resource>;
        mEntries.push_back(std::make_unique<Entry<StoredResource>>(
                std::forward<Resource>(resource)));
        if (mCounters) {
            ++mCounters->deferredReleaseRetireCount;
            ++mCounters->deferredReleaseLiveCount;
            mCounters->deferredReleasePeakLiveCount = std::max(
                    mCounters->deferredReleasePeakLiveCount,
                    mCounters->deferredReleaseLiveCount);
        }
    }

    void releaseAll() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct EntryBase {
        virtual ~EntryBase() = default;
    };

    template <typename Resource>
    struct Entry final : EntryBase {
        explicit Entry(Resource&& resourceValue)
                : resource(std::move(resourceValue)) {}

        Resource resource;
    };

    std::shared_ptr<VulkanResourceCounters> mCounters;
    std::vector<std::unique_ptr<EntryBase>> mEntries;
};

struct VulkanUploadSlice final {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mappedData = nullptr;
};

class VulkanUploadArena final {
public:
    // One arena belongs to one frame slot. beginFrame() is legal only after
    // that slot's fence completes; this is what makes linear reuse lock-free.
    VulkanUploadArena() = default;

    [[nodiscard]] VkResult initialize(
            const std::shared_ptr<VulkanDeviceContext>& device,
            VkDeviceSize initialCapacity) noexcept;
    [[nodiscard]] VkResult beginFrame(VkDeviceSize requiredCapacity,
            VulkanDeferredReleaseQueue& deferredReleases,
            bool& bufferChanged) noexcept;
    [[nodiscard]] std::optional<VulkanUploadSlice> write(
            const void* source, VkDeviceSize size, VkDeviceSize alignment,
            VkResult& result) noexcept;
    void reset() noexcept;

    [[nodiscard]] VkBuffer buffer() const noexcept;
    [[nodiscard]] VkDeviceSize capacity() const noexcept;
    [[nodiscard]] VkDeviceSize used() const noexcept;

private:
    [[nodiscard]] VkResult createBuffer(
            VkDeviceSize capacity, VulkanBuffer& output) const noexcept;

    std::shared_ptr<VulkanDeviceContext> mDevice;
    VulkanBuffer mBuffer;
    VkDeviceSize mCapacity = 0;
    VkDeviceSize mCursor = 0;
};

} // namespace cellularity::render::vulkan
