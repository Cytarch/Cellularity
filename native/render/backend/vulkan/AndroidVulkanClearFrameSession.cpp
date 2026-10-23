#include "render/backend/vulkan/AndroidVulkanClearFrameSession.h"

#include "app/microscope/render/MicroscopeDebugHud.h"
#include "render/backend/vulkan/VulkanResourceFoundation.h"
#include "render/backend/vulkan/VulkanResult.h"

#include <android/log.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cellularity::render::vulkan {
namespace {

constexpr std::uint32_t kRequestedApiVersion = VK_API_VERSION_1_0;
constexpr std::uint32_t kFramesInFlight = 2;
constexpr std::uint64_t kAcquireTimeoutNanoseconds = 250'000'000;
constexpr VkDeviceSize kInitialUploadArenaBytes = 64 * 1024;
constexpr std::uint32_t kMissingQueueFamily =
        std::numeric_limits<std::uint32_t>::max();
constexpr VkFormat kOffscreenColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkDeviceSize kRgbaChannelCount = 4;
#if CELLULARITY_ENABLE_VULKAN_PROFILING
constexpr std::size_t kPerformanceSampleCapacity = 2'048;
#else
constexpr std::size_t kPerformanceSampleCapacity = 1;
#endif
constexpr std::uint32_t kGpuTimestampQueryCount =
        static_cast<std::uint32_t>(kVulkanGpuPassCount + 1);

using PerformanceClock = std::chrono::steady_clock;

std::uint64_t performanceNowNanos() noexcept {
    return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                    PerformanceClock::now().time_since_epoch())
                    .count());
}

struct TimingDistribution final {
    std::array<double, kPerformanceSampleCapacity> samples{};
    std::size_t retainedCount = 0;
    std::size_t nextSample = 0;
    std::uint64_t totalCount = 0;
    double totalNanos = 0.0;
    double maximumNanos = 0.0;

    void reset() noexcept {
        retainedCount = 0;
        nextSample = 0;
        totalCount = 0;
        totalNanos = 0.0;
        maximumNanos = 0.0;
    }

    void record(double nanoseconds) noexcept {
        if (!std::isfinite(nanoseconds) || nanoseconds < 0.0) {
            return;
        }
        samples[nextSample] = nanoseconds;
        nextSample = (nextSample + 1) % samples.size();
        retainedCount = std::min(retainedCount + 1, samples.size());
        ++totalCount;
        totalNanos += nanoseconds;
        maximumNanos = std::max(maximumNanos, nanoseconds);
    }

    [[nodiscard]] double percentile(double fraction) const {
        if (retainedCount == 0) {
            return 0.0;
        }
        std::array<double, kPerformanceSampleCapacity> sorted{};
        std::copy_n(samples.begin(), retainedCount, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + retainedCount);
        const std::size_t rank = std::clamp<std::size_t>(
                static_cast<std::size_t>(
                        std::ceil(fraction * retainedCount)),
                1, retainedCount);
        return sorted[rank - 1];
    }
};

struct VulkanFrameCpuSample final {
    std::uint64_t startedAtNanos = 0;
    std::uint64_t fenceWaitNanos = 0;
    std::uint64_t uploadNanos = 0;
    std::uint64_t acquireNanos = 0;
    std::uint64_t uniformNanos = 0;
    std::uint64_t commandRecordNanos = 0;
    std::uint64_t submitNanos = 0;
    std::uint64_t presentNanos = 0;
};

struct VulkanProfileCounterBaseline final {
    std::uint64_t swapchainCreations = 0;
    std::uint64_t geometryUploads = 0;
    std::uint64_t geometryUploadSkips = 0;
    std::uint64_t geometryUploadBytes = 0;
    std::uint64_t geometryBufferGrowths = 0;
    std::uint64_t textUploads = 0;
    std::uint64_t textUploadSkips = 0;
    std::uint64_t textUploadBytes = 0;
    std::uint64_t textBufferGrowths = 0;
    std::uint64_t basicCellDraws = 0;
    std::uint64_t arrowDraws = 0;
    std::uint64_t foodDraws = 0;
    std::uint64_t linkDraws = 0;
    std::uint64_t deadDraws = 0;
    std::uint64_t outlineDraws = 0;
    std::uint64_t markerDraws = 0;
    std::uint64_t genomeGridDraws = 0;
    std::uint64_t genomeTreeDraws = 0;
    std::uint64_t textDraws = 0;
    std::uint64_t pipelineBinds = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t uploadFlushes = 0;
    std::uint64_t uploadArenaGrowths = 0;
    std::uint64_t commandBufferRecords = 0;
    std::uint64_t descriptorBinds = 0;
    std::uint64_t descriptorUpdates = 0;
    std::uint64_t queueSubmissions = 0;
    std::uint64_t fenceWaits = 0;
    std::uint64_t blockingFenceWaits = 0;
    std::uint64_t deviceWaitIdles = 0;
    std::uint64_t steadyStateResourceCreates = 0;
    std::uint64_t controlledCapacityResourceCreates = 0;
    std::uint64_t memoryAllocations = 0;
    std::uint64_t memoryFrees = 0;
    std::uint64_t resourceCreates = 0;
    std::uint64_t resourceDestroys = 0;
};

constexpr std::array<const char*, kVulkanGpuPassCount> kGpuPassNames = {
        "background",
        "markers",
        "food",
        "links_before_cells",
        "dead_cells",
        "cells",
        "direction_arrows",
        "links_after_cells",
        "outlines",
        "text",
};

#if CELLULARITY_ENABLE_VULKAN_PROFILING
std::uint64_t timestampDelta(std::uint64_t start, std::uint64_t end,
        std::uint32_t validBits) noexcept {
    if (validBits == 0) {
        return 0;
    }
    if (validBits >= 64) {
        return end - start;
    }
    const std::uint64_t mask = (std::uint64_t{1} << validBits) - 1;
    return (end - start) & mask;
}
#endif

void appendTimingDistribution(std::ostringstream& stream,
        std::string_view prefix, const TimingDistribution& distribution) {
    const auto rounded = [](double value) {
        return static_cast<std::uint64_t>(std::llround(value));
    };
    stream << prefix << "_sample_count=" << distribution.totalCount << '\n';
    stream << prefix << "_retained_sample_count="
           << distribution.retainedCount << '\n';
    stream << prefix << "_p50_ns="
           << rounded(distribution.percentile(0.50)) << '\n';
    stream << prefix << "_p95_ns="
           << rounded(distribution.percentile(0.95)) << '\n';
    stream << prefix << "_p99_ns="
           << rounded(distribution.percentile(0.99)) << '\n';
    stream << prefix << "_max_ns="
           << rounded(distribution.maximumNanos) << '\n';
    stream << prefix << "_mean_ns="
           << rounded(distribution.totalCount == 0
                              ? 0.0
                              : distribution.totalNanos
                                      / distribution.totalCount)
           << '\n';
}

std::size_t offscreenTargetIndex(OffscreenRenderTarget target) noexcept {
    return target == OffscreenRenderTarget::GenomePreview ? 0U : 1U;
}

const char* offscreenTargetName(OffscreenRenderTarget target) noexcept {
    return target == OffscreenRenderTarget::GenomePreview
            ? "Genome Preview"
            : "Substrate Preview";
}

struct alignas(16) BackgroundUniformData final {
    std::array<float, 16> mvpMatrix{};
    std::array<float, 4> lightDirectionRangeIntensity{};
    std::array<float, 4> backgroundColorRadius{};
    std::array<float, 8> preRotation{};
    std::array<float, 4> geometry{};
};

static_assert(sizeof(BackgroundUniformData) == 144,
        "Vulkan background uniform data must match the std140 GLSL contract");
static_assert(offsetof(BackgroundUniformData, mvpMatrix) == 0);
static_assert(offsetof(BackgroundUniformData,
                      lightDirectionRangeIntensity)
        == 64);
static_assert(offsetof(BackgroundUniformData, backgroundColorRadius) == 80);
static_assert(offsetof(BackgroundUniformData, preRotation) == 96);
static_assert(offsetof(BackgroundUniformData, geometry) == 128);

struct alignas(16) CellUniformData final {
    std::array<float, 16> mvpMatrix{};
    std::array<float, 8> preRotation{};
};

static_assert(sizeof(CellUniformData) == 96,
        "Vulkan basic-cell uniform data must match the std140 GLSL contract");
static_assert(offsetof(CellUniformData, mvpMatrix) == 0);
static_assert(offsetof(CellUniformData, preRotation) == 64);

struct alignas(16) EnvironmentFieldUniformData final {
    std::array<float, 16> mvpMatrix{};
    std::array<float, 4> field{};
    std::array<float, 4> fieldExtra{};
    std::array<float, 4> preview{};
    std::array<float, 8> preRotation{};
};

static_assert(sizeof(EnvironmentFieldUniformData) == 144,
        "Vulkan environment-field uniform data must match the std140 GLSL contract");
static_assert(offsetof(EnvironmentFieldUniformData, mvpMatrix) == 0);
static_assert(offsetof(EnvironmentFieldUniformData, field) == 64);
static_assert(offsetof(EnvironmentFieldUniformData, fieldExtra) == 80);
static_assert(offsetof(EnvironmentFieldUniformData, preview) == 96);
static_assert(offsetof(EnvironmentFieldUniformData, preRotation) == 112);

struct QueueFamilies final {
    std::uint32_t graphics = kMissingQueueFamily;
    std::uint32_t present = kMissingQueueFamily;

    [[nodiscard]] bool complete() const noexcept {
        return graphics != kMissingQueueFamily
                && present != kMissingQueueFamily;
    }
};

struct FrameResources final {
    // The fence retires every resource reused through this frame slot,
    // including the acquire semaphore, command buffer, and upload arena.
    VulkanCommandBuffer commandBuffer;
    VulkanSemaphore imageAvailable;
    VulkanFence inFlight;
    VulkanUploadArena uploadArena;
    VulkanDescriptorSet backgroundDescriptorSet;
    VulkanDeferredReleaseQueue deferredReleases;
    bool submissionPending = false;
    std::uint64_t submissionSerial = 0;
    std::uint32_t gpuTimestampFirstQuery = 0;
    std::uint64_t gpuProfileGeneration = 0;
    bool gpuTimestampRecorded = false;
    bool gpuTimestampPending = false;
};

class NativeWindowReference final {
public:
    explicit NativeWindowReference(ANativeWindow* window) noexcept
            : mWindow(window) {}

    ~NativeWindowReference() {
        if (mWindow != nullptr) {
            ANativeWindow_release(mWindow);
        }
    }

    NativeWindowReference(const NativeWindowReference&) = delete;
    NativeWindowReference& operator=(const NativeWindowReference&) = delete;

    [[nodiscard]] ANativeWindow* release() noexcept {
        return std::exchange(mWindow, nullptr);
    }

private:
    ANativeWindow* mWindow;
};

class SteadyStateFrameScope final {
public:
    explicit SteadyStateFrameScope(
            VulkanResourceCounters& counters) noexcept
            : mCounters(counters) {
        mCounters.insideSteadyStateFrame = true;
    }

    ~SteadyStateFrameScope() {
        end();
    }

    SteadyStateFrameScope(const SteadyStateFrameScope&) = delete;
    SteadyStateFrameScope& operator=(
            const SteadyStateFrameScope&) = delete;

    void end() noexcept {
        if (mActive) {
            mCounters.insideSteadyStateFrame = false;
            mActive = false;
        }
    }

private:
    VulkanResourceCounters& mCounters;
    bool mActive = true;
};

template <typename Handle>
std::uint64_t handleValue(Handle handle) noexcept {
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

std::string sanitizeLineValue(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(std::min<std::size_t>(value.size(), 1024));
    for (const unsigned char character : value) {
        if (sanitized.size() >= 1024) {
            break;
        }
        if (character == '\n' || character == '\r' || character == '\t') {
            sanitized.push_back(' ');
        } else if (character < 0x20 || character >= 0x7F) {
            sanitized.push_back('?');
        } else {
            sanitized.push_back(static_cast<char>(character));
        }
    }
    return sanitized;
}

std::string hexadecimal(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << std::setw(8)
           << std::setfill('0') << value;
    return stream.str();
}

bool hasExtension(const std::vector<VkExtensionProperties>& extensions,
        const char* requiredName) {
    return std::any_of(extensions.begin(), extensions.end(),
            [requiredName](const VkExtensionProperties& extension) {
                return std::string_view(extension.extensionName) == requiredName;
            });
}

bool hasLayer(const std::vector<VkLayerProperties>& layers,
        const char* requiredName) {
    return std::any_of(layers.begin(), layers.end(),
            [requiredName](const VkLayerProperties& layer) {
                return std::string_view(layer.layerName) == requiredName;
            });
}

VkResult enumerateInstanceExtensions(
        std::vector<VkExtensionProperties>& extensions) {
    std::uint32_t count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(
            nullptr, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    extensions.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkEnumerateInstanceExtensionProperties(
            nullptr, &count, extensions.data());
    extensions.resize(count);
    return result;
}

VkResult enumerateInstanceLayers(std::vector<VkLayerProperties>& layers) {
    std::uint32_t count = 0;
    VkResult result = vkEnumerateInstanceLayerProperties(&count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    layers.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkEnumerateInstanceLayerProperties(&count, layers.data());
    layers.resize(count);
    return result;
}

VkResult enumeratePhysicalDevices(VkInstance instance,
        std::vector<VkPhysicalDevice>& devices) {
    std::uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    devices.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    devices.resize(count);
    return result;
}

VkResult enumerateDeviceExtensions(VkPhysicalDevice device,
        std::vector<VkExtensionProperties>& extensions) {
    std::uint32_t count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(
            device, nullptr, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    extensions.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkEnumerateDeviceExtensionProperties(
            device, nullptr, &count, extensions.data());
    extensions.resize(count);
    return result;
}

VkResult enumerateSurfaceFormats(VkPhysicalDevice device, VkSurfaceKHR surface,
        std::vector<VkSurfaceFormatKHR>& formats) {
    std::uint32_t count = 0;
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    formats.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &count, formats.data());
    formats.resize(count);
    return result;
}

VkResult enumeratePresentModes(VkPhysicalDevice device, VkSurfaceKHR surface,
        std::vector<VkPresentModeKHR>& presentModes) {
    std::uint32_t count = 0;
    VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    presentModes.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &count, presentModes.data());
    presentModes.resize(count);
    return result;
}

std::optional<VkSurfaceFormatKHR> selectSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) {
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
        return VkSurfaceFormatKHR{
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        };
    }
    constexpr std::array<VkFormat, 2> candidates = {
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8A8_UNORM
    };
    for (const VkFormat candidate : candidates) {
        const auto match = std::find_if(formats.begin(), formats.end(),
                [candidate](const VkSurfaceFormatKHR& format) {
                    return format.format == candidate
                            && format.colorSpace
                            == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                });
        if (match != formats.end()) {
            return *match;
        }
    }
    return std::nullopt;
}

VkFormat selectDepthFormat(VkPhysicalDevice device) {
    constexpr std::array<VkFormat, 4> candidates = {
            VK_FORMAT_D16_UNORM,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT
    };
    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device, candidate, &properties);
        if ((properties.optimalTilingFeatures
                    & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                != 0) {
            return candidate;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

bool formatHasStencil(VkFormat format) noexcept {
    return format == VK_FORMAT_D24_UNORM_S8_UINT
            || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkCompositeAlphaFlagBitsKHR selectCompositeAlpha(
        VkCompositeAlphaFlagsKHR supported) {
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> candidates = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };
    for (const VkCompositeAlphaFlagBitsKHR candidate : candidates) {
        if ((supported & candidate) != 0) {
            return candidate;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

VkExtent2D selectExtent(const VkSurfaceCapabilitiesKHR& capabilities,
        int width, int height) {
    if (capabilities.currentExtent.width
            != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    const std::uint32_t requestedWidth = static_cast<std::uint32_t>(
            std::max(width, 1));
    const std::uint32_t requestedHeight = static_cast<std::uint32_t>(
            std::max(height, 1));
    return VkExtent2D{
            std::clamp(requestedWidth,
                    capabilities.minImageExtent.width,
                    capabilities.maxImageExtent.width),
            std::clamp(requestedHeight,
                    capabilities.minImageExtent.height,
                    capabilities.maxImageExtent.height)
    };
}

bool surfaceTransformSwapsAxes(
        VkSurfaceTransformFlagBitsKHR transform) noexcept {
    return transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR
            || transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR
            || transform
            == VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR
            || transform
            == VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR;
}

VkExtent2D identityExtent(VkExtent2D displayExtent,
        VkSurfaceTransformFlagBitsKHR transform) noexcept {
    if (surfaceTransformSwapsAxes(transform)) {
        std::swap(displayExtent.width, displayExtent.height);
    }
    return displayExtent;
}

constexpr VkOffset2D topLeftIdentityOffset(VkExtent2D targetExtent,
        VkExtent2D presentationExtent,
        VkSurfaceTransformFlagBitsKHR transform) noexcept {
    const std::int32_t trailingX = static_cast<std::int32_t>(
            targetExtent.width - presentationExtent.width);
    const std::int32_t trailingY = static_cast<std::int32_t>(
            targetExtent.height - presentationExtent.height);
    switch (transform) {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR:
            return {trailingX, 0};
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR:
            return {trailingX, trailingY};
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR:
            return {0, trailingY};
        case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR:
        case VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR:
        default:
            return {0, 0};
    }
}

constexpr bool topLeftIdentityOffsetsAreCorrect() noexcept {
    constexpr VkExtent2D target{100, 80};
    constexpr VkExtent2D presentation{30, 40};
    const auto offsetFor = [=](VkSurfaceTransformFlagBitsKHR transform) {
        return topLeftIdentityOffset(target, presentation, transform);
    };
    const auto identity = offsetFor(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
    const auto rotate90 = offsetFor(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR);
    const auto rotate180 = offsetFor(VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR);
    const auto rotate270 = offsetFor(VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
    const auto mirror = offsetFor(VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR);
    const auto mirror90 = offsetFor(
            VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR);
    const auto mirror180 = offsetFor(
            VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR);
    const auto mirror270 = offsetFor(
            VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR);
    return identity.x == 0 && identity.y == 0
            && rotate90.x == 70 && rotate90.y == 0
            && rotate180.x == 70 && rotate180.y == 40
            && rotate270.x == 0 && rotate270.y == 40
            && mirror.x == 70 && mirror.y == 0
            && mirror90.x == 0 && mirror90.y == 0
            && mirror180.x == 0 && mirror180.y == 40
            && mirror270.x == 70 && mirror270.y == 40;
}

static_assert(topLeftIdentityOffsetsAreCorrect());

std::array<float, 4> preRotationMatrix(
        VkSurfaceTransformFlagBitsKHR transform) noexcept {
    // Column-major matrices. Mirrored transforms apply the horizontal mirror
    // first and the clockwise display rotation second, matching Vulkan's
    // VkSurfaceTransformFlagBitsKHR definition.
    switch (transform) {
        case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
            return {0.0F, 1.0F, -1.0F, 0.0F};
        case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
            return {-1.0F, 0.0F, 0.0F, -1.0F};
        case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
            return {0.0F, -1.0F, 1.0F, 0.0F};
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR:
            return {-1.0F, 0.0F, 0.0F, 1.0F};
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR:
            return {0.0F, -1.0F, -1.0F, 0.0F};
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR:
            return {1.0F, 0.0F, 0.0F, -1.0F};
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR:
            return {0.0F, 1.0F, 1.0F, 0.0F};
        case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:
        case VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR:
        default:
            return {1.0F, 0.0F, 0.0F, 1.0F};
    }
}

const char* statusName(std::string_view status) {
    return status.empty() ? "unknown" : status.data();
}

cellularity::microscope::MicroscopeDebugHudTelemetry demoHudTelemetry() {
    cellularity::microscope::MicroscopeDebugHudTelemetry telemetry;
    telemetry.rateSampleSequence = 1;
    telemetry.actualRateValid = true;
    telemetry.actualCompletedUps = 2'400.0;
    telemetry.stageTimings.sequence = 1;
    double totalNanos = 0.0;
    for (std::size_t stage = 0;
            stage < telemetry.stageTimings.stageNanos.size(); ++stage) {
        const double stageNanos =
                8'000.0 + static_cast<double>(stage) * 1'250.0;
        telemetry.stageTimings.stageNanos[stage] = stageNanos;
        totalNanos += stageNanos;
    }
    telemetry.stageTimings.totalNanos = totalNanos;
    return telemetry;
}

} // namespace

struct AndroidVulkanClearFrameSession::Impl final {
    struct PendingGenomeLayer final {
        std::size_t geometrySlot = 0;
        RenderPresentationState presentation{};
        GenomePreviewDrawOptions options{};
        std::array<float, 16> treeMatrix{};
        std::vector<float> treeVertices;
        bool tree = false;
    };

    struct PendingGenomeFrame final {
        bool active = false;
        bool ready = false;
        ClearFrameResult outcome = ClearFrameResult::Deferred;
        bool fullyTransparentClear = false;
        int width = 0;
        int height = 0;
        std::optional<GenomePreviewGridCommand> grid;
        std::vector<PendingGenomeLayer> layers;

        void reset() {
            active = false;
            ready = false;
            outcome = ClearFrameResult::Deferred;
            fullyTransparentClear = false;
            width = 0;
            height = 0;
            grid.reset();
            layers.clear();
        }
    };

    struct OffscreenTargetResources final {
        VulkanImage colorImage;
        VulkanImageView colorImageView;
        VulkanImage depthImage;
        VulkanImageView depthImageView;
        VulkanFramebuffer framebuffer;
        VulkanBuffer readbackBuffer;
        VkDeviceSize rowPitch = 0;
        int width = 0;
        int height = 0;
        bool rendered = false;

        [[nodiscard]] bool matches(int expectedWidth,
                int expectedHeight) const noexcept {
            return width == expectedWidth && height == expectedHeight
                    && colorImage.get() != VK_NULL_HANDLE
                    && colorImageView && depthImage.get() != VK_NULL_HANDLE
                    && depthImageView && framebuffer
                    && readbackBuffer.get() != VK_NULL_HANDLE;
        }

        void reset() noexcept {
            framebuffer.reset();
            depthImageView.reset();
            depthImage.reset();
            colorImageView.reset();
            colorImage.reset();
            readbackBuffer.reset();
            rowPitch = 0;
            width = 0;
            height = 0;
            rendered = false;
        }
    };

    struct RenderDestination final {
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D extent{};
        VkPipeline backgroundPipeline = VK_NULL_HANDLE;
        VulkanPipelineTarget pipelineTarget = VulkanPipelineTarget::Surface;
        OffscreenTargetResources* readbackTarget = nullptr;
    };

    Impl(std::vector<std::uint32_t> vertexSpirv,
            std::vector<std::uint32_t> fragmentSpirv,
            std::vector<std::uint32_t> densityVertexSpirv,
            std::vector<std::uint32_t> densityFragmentSpirv,
            std::array<VulkanCellShaderSpirv,
                    kVulkanBasicCellProgramCount>
                    basicCellSpirv,
            std::array<VulkanCellShaderSpirv, kVulkanWorldProgramCount>
                    worldSpirv,
            NativeRenderTexturePayload dotTexture,
            NativeRenderTexturePayload besselTexture)
            : ownerThread(std::this_thread::get_id()),
              backgroundVertexSpirv(std::move(vertexSpirv)),
              backgroundFragmentSpirv(std::move(fragmentSpirv)),
              densityFieldVertexSpirv(std::move(densityVertexSpirv)),
              densityFieldFragmentSpirv(std::move(densityFragmentSpirv)),
              cellShaderSpirv(std::move(basicCellSpirv)),
              worldShaderSpirv(std::move(worldSpirv)),
              dotTexturePayload(std::move(dotTexture)),
              besselTexturePayload(std::move(besselTexture)),
              demoFrame(makeVulkanBasicCellDemoFrame()),
              demoTelemetry(demoHudTelemetry()) {
        pendingGenomeFrame.layers.reserve(2);
    }

    ~Impl() {
        destroyAll();
    }

    std::thread::id ownerThread;
    std::vector<std::uint32_t> backgroundVertexSpirv;
    std::vector<std::uint32_t> backgroundFragmentSpirv;
    std::vector<std::uint32_t> densityFieldVertexSpirv;
    std::vector<std::uint32_t> densityFieldFragmentSpirv;
    std::array<VulkanCellShaderSpirv, kVulkanBasicCellProgramCount>
            cellShaderSpirv;
    std::array<VulkanCellShaderSpirv, kVulkanWorldProgramCount>
            worldShaderSpirv;
    NativeRenderTexturePayload dotTexturePayload;
    NativeRenderTexturePayload besselTexturePayload;
    PreparedRenderFrame demoFrame;
    cellularity::microscope::MicroscopeDebugHud debugHud;
    cellularity::microscope::MicroscopeDebugHudTelemetry demoTelemetry;
    bool demoPerformanceHudEnabled = false;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkDevice device = VK_NULL_HANDLE;
    std::shared_ptr<VulkanResourceCounters> resourceCounters =
            std::make_shared<VulkanResourceCounters>();
    std::shared_ptr<VulkanDeviceContext> deviceContext;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily = kMissingQueueFamily;
    std::uint32_t presentQueueFamily = kMissingQueueFamily;
    std::uint32_t graphicsTimestampValidBits = 0;
    VulkanCommandPool commandPool;
    VulkanQueryPool performanceQueryPool;
    VulkanShaderModule backgroundVertexShader;
    VulkanShaderModule backgroundFragmentShader;
    VulkanShaderModule densityFieldVertexShader;
    VulkanShaderModule densityFieldFragmentShader;
    VulkanDescriptorSetLayout backgroundDescriptorSetLayout;
    VulkanDescriptorPool descriptorPool;
    VulkanPipelineLayout backgroundPipelineLayout;
    VulkanBasicCellRenderer basicCellRenderer;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    PFN_vkSetDebugUtilsObjectNameEXT setDebugObjectName = nullptr;

    ANativeWindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VulkanSwapchain swapchain;
    VkFormat surfaceFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR surfaceColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D displayExtent{};
    VkExtent2D surfaceExtent{};
    VkSurfaceTransformFlagBitsKHR surfaceTransform =
            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkSurfaceTransformFlagBitsKHR swapchainPreTransform =
            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    std::vector<VkImage> swapchainImages;
    std::vector<VulkanImageView> swapchainImageViews;
    std::vector<VulkanImage> depthImages;
    std::vector<VulkanImageView> depthImageViews;
    VulkanRenderPass renderPass;
    VulkanPipeline backgroundPipeline;
    VulkanPipeline densityFieldPipeline;
    std::vector<VulkanFramebuffer> framebuffers;
    VulkanRenderPass offscreenRenderPass;
    VulkanPipeline offscreenBackgroundPipeline;
    std::array<OffscreenTargetResources, 2> offscreenTargets;
    std::optional<OffscreenRenderTarget> activeOffscreenTarget;
    // Present completion is associated with a swapchain image, not with the
    // frame slot that happened to render it. Reacquiring that image proves its
    // semaphore is safe to signal again.
    std::vector<VulkanSemaphore> renderFinishedSemaphores;
    std::array<FrameResources, kFramesInFlight> frames{};
    std::vector<VkFence> imageFences;
    std::uint32_t currentFrame = 0;
    std::uint64_t nextSubmissionSerial = 1;
    std::uint64_t completedSubmissionSerial = 0;

    int requestedWidth = 0;
    int requestedHeight = 0;
    bool swapchainRecreatePending = false;
    bool validationRequested = false;
    bool validationLayerAvailable = false;
    bool validationEnabled = false;
    bool debugUtilsAvailable = false;
    bool debugUtilsEnabled = false;
    std::uint64_t frameCount = 0;
    std::uint64_t suboptimalFrameCount = 0;
    std::uint64_t instanceCreationCount = 0;
    std::uint64_t deviceCreationCount = 0;
    std::uint64_t swapchainCreationCount = 0;
    std::uint64_t densityFieldPipelineCreationCount = 0;
    std::uint64_t offscreenRenderPassCreationCount = 0;
    std::uint64_t offscreenTargetCreationCount = 0;
    std::uint64_t offscreenCaptureCount = 0;
    std::uint64_t offscreenReadbackBytes = 0;
    std::uint64_t surfaceGeneration = 0;
    bool performanceProfilingEnabled = false;
    std::uint64_t performanceProfileGeneration = 0;
    std::uint64_t profiledPresentedFrameCount = 0;
    std::uint64_t profiledAcquireDeferredCount = 0;
    std::uint64_t gpuTimestampReadFailureCount = 0;
    TimingDistribution cpuFrameTiming;
    TimingDistribution cpuFenceWaitTiming;
    TimingDistribution cpuUploadTiming;
    TimingDistribution cpuAcquireTiming;
    TimingDistribution cpuUniformTiming;
    TimingDistribution cpuCommandRecordTiming;
    TimingDistribution cpuSubmitTiming;
    TimingDistribution cpuPresentTiming;
    TimingDistribution gpuFrameTiming;
    std::array<TimingDistribution, kVulkanGpuPassCount> gpuPassTimings;
    VulkanProfileCounterBaseline performanceCounterBaseline;
    std::string status = "new";
    std::string lastResult = "VK_SUCCESS";
    std::string error;
    bool lastFrameWasPresented = false;
    PendingGenomeFrame pendingGenomeFrame;

    mutable std::mutex validationMutex;
    std::uint64_t validationWarningCount = 0;
    std::uint64_t validationErrorCount = 0;
    std::string lastValidationMessage;

    [[nodiscard]] OffscreenTargetResources& offscreenTarget(
            OffscreenRenderTarget target) noexcept {
        return offscreenTargets[offscreenTargetIndex(target)];
    }

    [[nodiscard]] const OffscreenTargetResources& offscreenTarget(
            OffscreenRenderTarget target) const noexcept {
        return offscreenTargets[offscreenTargetIndex(target)];
    }

    [[nodiscard]] bool offscreenCaptureActive(
            OffscreenRenderTarget target) const noexcept {
        return activeOffscreenTarget.has_value()
                && *activeOffscreenTarget == target;
    }

    [[nodiscard]] VkSurfaceTransformFlagBitsKHR renderPreTransform()
            const noexcept {
        return activeOffscreenTarget.has_value()
                ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                : swapchainPreTransform;
    }

    [[nodiscard]] bool checkThread(const char* operation) {
        if (std::this_thread::get_id() == ownerThread) {
            return true;
        }
        status = "failed";
        lastResult = "THREAD_CONFINEMENT_VIOLATION";
        error = std::string(operation)
                + " was called outside the Vulkan render thread";
        return false;
    }

    void setFailure(const char* operation, VkResult result) {
        status = "failed";
        lastResult = resultName(result);
        error = resultDescription(operation, result);
    }

    void setFailure(std::string message) {
        status = "failed";
        lastResult = "VK_RESULT_UNKNOWN";
        error = std::move(message);
    }

    void resetPerformanceProfile() noexcept {
        ++performanceProfileGeneration;
        profiledPresentedFrameCount = 0;
        profiledAcquireDeferredCount = 0;
        gpuTimestampReadFailureCount = 0;
        cpuFrameTiming.reset();
        cpuFenceWaitTiming.reset();
        cpuUploadTiming.reset();
        cpuAcquireTiming.reset();
        cpuUniformTiming.reset();
        cpuCommandRecordTiming.reset();
        cpuSubmitTiming.reset();
        cpuPresentTiming.reset();
        gpuFrameTiming.reset();
        for (TimingDistribution& timing : gpuPassTimings) {
            timing.reset();
        }
        performanceCounterBaseline = {
                swapchainCreationCount,
                basicCellRenderer.geometryUploadCount(),
                basicCellRenderer.geometryUploadSkipCount(),
                basicCellRenderer.geometryUploadBytes(),
                basicCellRenderer.geometryBufferGrowthCount(),
                basicCellRenderer.textGeometryUploadCount(),
                basicCellRenderer.textGeometryUploadSkipCount(),
                basicCellRenderer.textGeometryUploadBytes(),
                basicCellRenderer.textGeometryBufferGrowthCount(),
                basicCellRenderer.basicCellDrawCount(),
                basicCellRenderer.arrowDrawCount(),
                basicCellRenderer.foodDrawCount(),
                basicCellRenderer.linkDrawCount(),
                basicCellRenderer.deadDrawCount(),
                basicCellRenderer.outlineDrawCount(),
                basicCellRenderer.markerDrawCount(),
                basicCellRenderer.genomeGridDrawCount(),
                basicCellRenderer.genomeTreeDrawCount(),
                basicCellRenderer.textDrawCount(),
                basicCellRenderer.pipelineBindCount(),
                resourceCounters->uploadBytes,
                resourceCounters->uploadFlushCount,
                resourceCounters->uploadArenaGrowthCount,
                resourceCounters->commandBufferRecordCount,
                resourceCounters->descriptorBindCount,
                resourceCounters->descriptorUpdateCount,
                resourceCounters->queueSubmissionCount,
                resourceCounters->fenceWaitCount,
                resourceCounters->blockingFenceWaitCount,
                resourceCounters->deviceWaitIdleCount,
                resourceCounters->steadyStateResourceCreateCount,
                resourceCounters->controlledCapacityResourceCreateCount,
                resourceCounters->memoryAllocationCount,
                resourceCounters->memoryFreeCount,
                resourceCounters->resourceCreateCount,
                resourceCounters->resourceDestroyCount,
        };
    }

    void setPerformanceProfilingEnabled(bool enabled) {
        if (!checkThread("setPerformanceProfilingEnabled")) {
            return;
        }
#if CELLULARITY_ENABLE_VULKAN_PROFILING
        if (enabled && !performanceProfilingEnabled) {
            resetPerformanceProfile();
        }
        performanceProfilingEnabled = enabled;
#else
        static_cast<void>(enabled);
        performanceProfilingEnabled = false;
#endif
    }

    void recordCpuPerformance(
            const VulkanFrameCpuSample& sample) noexcept {
        const std::uint64_t finishedAtNanos = performanceNowNanos();
        cpuFrameTiming.record(static_cast<double>(
                finishedAtNanos - sample.startedAtNanos));
        cpuFenceWaitTiming.record(
                static_cast<double>(sample.fenceWaitNanos));
        cpuUploadTiming.record(static_cast<double>(sample.uploadNanos));
        cpuAcquireTiming.record(static_cast<double>(sample.acquireNanos));
        cpuUniformTiming.record(static_cast<double>(sample.uniformNanos));
        cpuCommandRecordTiming.record(
                static_cast<double>(sample.commandRecordNanos));
        cpuSubmitTiming.record(static_cast<double>(sample.submitNanos));
        cpuPresentTiming.record(static_cast<double>(sample.presentNanos));
        ++profiledPresentedFrameCount;
    }

    void collectGpuTimestamps(FrameResources& frame) noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
        if (!frame.gpuTimestampPending || !performanceQueryPool
                || device == VK_NULL_HANDLE) {
            frame.gpuTimestampPending = false;
            return;
        }
        std::array<std::uint64_t, kGpuTimestampQueryCount> timestamps{};
        const VkResult result = vkGetQueryPoolResults(device,
                performanceQueryPool.get(), frame.gpuTimestampFirstQuery,
                kGpuTimestampQueryCount, sizeof(timestamps),
                timestamps.data(), sizeof(std::uint64_t),
                VK_QUERY_RESULT_64_BIT);
        frame.gpuTimestampPending = false;
        if (result != VK_SUCCESS) {
            ++gpuTimestampReadFailureCount;
            return;
        }
        if (frame.gpuProfileGeneration != performanceProfileGeneration) {
            return;
        }
        const double period = physicalDeviceProperties.limits.timestampPeriod;
        gpuFrameTiming.record(static_cast<double>(timestampDelta(
                                      timestamps.front(), timestamps.back(),
                                      graphicsTimestampValidBits))
                * period);
        for (std::size_t pass = 0; pass < gpuPassTimings.size(); ++pass) {
            gpuPassTimings[pass].record(
                    static_cast<double>(timestampDelta(timestamps[pass],
                            timestamps[pass + 1],
                            graphicsTimestampValidBits))
                    * period);
        }
#else
        frame.gpuTimestampPending = false;
#endif
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void* userData) {
        auto* self = static_cast<Impl*>(userData);
        if (self == nullptr) {
            return VK_FALSE;
        }
        std::lock_guard lock(self->validationMutex);
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
            ++self->validationErrorCount;
        } else if ((severity
                           & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                != 0) {
            ++self->validationWarningCount;
        }
        if (callbackData != nullptr && callbackData->pMessage != nullptr) {
            self->lastValidationMessage =
                    sanitizeLineValue(callbackData->pMessage);
            __android_log_print(
                    (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                                    != 0
                            ? ANDROID_LOG_ERROR
                            : ANDROID_LOG_WARN,
                    "CellularityVulkanValidation", "%s",
                    callbackData->pMessage);
        }
        return VK_FALSE;
    }

    [[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo() {
        VkDebugUtilsMessengerCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = &Impl::debugCallback;
        info.pUserData = this;
        return info;
    }

    [[nodiscard]] bool createInstance() {
        if (instance != VK_NULL_HANDLE) {
            return true;
        }

        std::vector<VkExtensionProperties> extensions;
        VkResult result = enumerateInstanceExtensions(extensions);
        if (result != VK_SUCCESS) {
            setFailure("vkEnumerateInstanceExtensionProperties", result);
            return false;
        }
        if (!hasExtension(extensions, VK_KHR_SURFACE_EXTENSION_NAME)
                || !hasExtension(
                        extensions, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
            setFailure("Required Vulkan Android surface extensions are unavailable");
            return false;
        }

        std::vector<const char*> enabledExtensions = {
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
        };
        debugUtilsAvailable = hasExtension(
                extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

#if defined(CELLULARITY_VULKAN_VALIDATION)
        validationRequested = true;
#endif
        if (validationRequested) {
            std::vector<VkLayerProperties> layers;
            result = enumerateInstanceLayers(layers);
            if (result != VK_SUCCESS) {
                setFailure("vkEnumerateInstanceLayerProperties", result);
                return false;
            }
            validationLayerAvailable = hasLayer(
                    layers, "VK_LAYER_KHRONOS_validation");
        }
        validationEnabled = validationRequested
                && validationLayerAvailable;
        debugUtilsEnabled = validationRequested && debugUtilsAvailable;
        if (debugUtilsEnabled) {
            enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        const VkApplicationInfo applicationInfo{
                VK_STRUCTURE_TYPE_APPLICATION_INFO,
                nullptr,
                "Cellularity Vulkan prepared-frame test",
                1,
                "Cellularity",
                1,
                kRequestedApiVersion
        };
        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        VkDebugUtilsMessengerCreateInfoEXT debugInfo = debugMessengerInfo();
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pNext = debugUtilsEnabled ? &debugInfo : nullptr;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount =
                static_cast<std::uint32_t>(enabledExtensions.size());
        createInfo.ppEnabledExtensionNames = enabledExtensions.data();
        createInfo.enabledLayerCount = validationEnabled ? 1U : 0U;
        createInfo.ppEnabledLayerNames =
                validationEnabled ? &validationLayer : nullptr;

        result = vkCreateInstance(&createInfo, nullptr, &instance);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateInstance", result);
            return false;
        }
        ++instanceCreationCount;

        if (debugUtilsEnabled) {
            const auto createDebugMessenger =
                    reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                            vkGetInstanceProcAddr(instance,
                                    "vkCreateDebugUtilsMessengerEXT"));
            if (createDebugMessenger != nullptr) {
                result = createDebugMessenger(
                        instance, &debugInfo, nullptr, &debugMessenger);
                if (result != VK_SUCCESS) {
                    lastResult = resultName(result);
                    error = resultDescription(
                            "vkCreateDebugUtilsMessengerEXT", result);
                    debugUtilsEnabled = false;
                }
            } else {
                debugUtilsEnabled = false;
            }
        }
        return true;
    }

    [[nodiscard]] bool createSurface() {
        if (window == nullptr || instance == VK_NULL_HANDLE) {
            setFailure("Cannot create a Vulkan surface without a native window");
            return false;
        }
        const VkAndroidSurfaceCreateInfoKHR createInfo{
                VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
                nullptr,
                0,
                window
        };
        const VkResult result = vkCreateAndroidSurfaceKHR(
                instance, &createInfo, nullptr, &surface);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateAndroidSurfaceKHR", result);
            return false;
        }
        ++surfaceGeneration;
        return true;
    }

    [[nodiscard]] QueueFamilies findQueueFamilies(
            VkPhysicalDevice candidate) const {
        QueueFamilies selected;
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        if (count > 0) {
            vkGetPhysicalDeviceQueueFamilyProperties(
                    candidate, &count, properties.data());
            properties.resize(count);
        }

        for (std::uint32_t index = 0; index < count; ++index) {
            if ((properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0
                    && selected.graphics == kMissingQueueFamily) {
                selected.graphics = index;
            }
            VkBool32 presentSupported = VK_FALSE;
            const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
                    candidate, index, surface, &presentSupported);
            if (result == VK_SUCCESS && presentSupported == VK_TRUE
                    && selected.present == kMissingQueueFamily) {
                selected.present = index;
            }
            if ((properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0
                    && result == VK_SUCCESS
                    && presentSupported == VK_TRUE) {
                selected.graphics = index;
                selected.present = index;
                break;
            }
        }
        return selected;
    }

    [[nodiscard]] bool deviceSupportsSurface(
            VkPhysicalDevice candidate, QueueFamilies& queues,
            VkFormat& candidateDepthFormat) const {
        queues = findQueueFamilies(candidate);
        if (!queues.complete()) {
            return false;
        }

        std::vector<VkExtensionProperties> extensions;
        if (enumerateDeviceExtensions(candidate, extensions) != VK_SUCCESS
                || !hasExtension(
                        extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats;
        if (enumerateSurfaceFormats(candidate, surface, formats) != VK_SUCCESS
                || !selectSurfaceFormat(formats).has_value()) {
            return false;
        }
        std::vector<VkPresentModeKHR> presentModes;
        if (enumeratePresentModes(candidate, surface, presentModes) != VK_SUCCESS
                || std::find(presentModes.begin(), presentModes.end(),
                           VK_PRESENT_MODE_FIFO_KHR)
                        == presentModes.end()) {
            return false;
        }
        candidateDepthFormat = selectDepthFormat(candidate);
        return candidateDepthFormat != VK_FORMAT_UNDEFINED;
    }

    [[nodiscard]] bool currentDeviceSupportsSurface() const {
        if (physicalDevice == VK_NULL_HANDLE
                || presentQueueFamily == kMissingQueueFamily) {
            return false;
        }
        VkBool32 supported = VK_FALSE;
        const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
                physicalDevice, presentQueueFamily, surface, &supported);
        if (result != VK_SUCCESS || supported != VK_TRUE) {
            return false;
        }
        std::vector<VkSurfaceFormatKHR> formats;
        if (enumerateSurfaceFormats(
                    physicalDevice, surface, formats)
                        != VK_SUCCESS
                || !selectSurfaceFormat(formats).has_value()) {
            return false;
        }
        std::vector<VkPresentModeKHR> presentModes;
        return enumeratePresentModes(
                       physicalDevice, surface, presentModes)
                == VK_SUCCESS
                && std::find(presentModes.begin(), presentModes.end(),
                           VK_PRESENT_MODE_FIFO_KHR)
                != presentModes.end();
    }

    [[nodiscard]] bool createDeviceForSurface() {
        std::vector<VkPhysicalDevice> devices;
        VkResult result = enumeratePhysicalDevices(instance, devices);
        if (result != VK_SUCCESS) {
            setFailure("vkEnumeratePhysicalDevices", result);
            return false;
        }

        VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
        QueueFamilies selectedQueues;
        VkFormat selectedDepthFormat = VK_FORMAT_UNDEFINED;
        for (const VkPhysicalDevice candidate : devices) {
            QueueFamilies queues;
            VkFormat candidateDepthFormat = VK_FORMAT_UNDEFINED;
            if (deviceSupportsSurface(
                        candidate, queues, candidateDepthFormat)) {
                selectedDevice = candidate;
                selectedQueues = queues;
                selectedDepthFormat = candidateDepthFormat;
                break;
            }
        }
        if (selectedDevice == VK_NULL_HANDLE) {
            setFailure("No physical device satisfies the Vulkan 1.0 background-pipeline requirements");
            return false;
        }

        const std::set<std::uint32_t> uniqueQueueFamilies = {
                selectedQueues.graphics,
                selectedQueues.present
        };
        constexpr float queuePriority = 1.0F;
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueQueueFamilies.size());
        for (const std::uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        const char* deviceExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        const VkPhysicalDeviceFeatures features{};
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount =
                static_cast<std::uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = &deviceExtension;
        createInfo.pEnabledFeatures = &features;

        result = vkCreateDevice(
                selectedDevice, &createInfo, nullptr, &device);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateDevice", result);
            return false;
        }
        ++deviceCreationCount;

        physicalDevice = selectedDevice;
        graphicsQueueFamily = selectedQueues.graphics;
        presentQueueFamily = selectedQueues.present;
        depthFormat = selectedDepthFormat;
        vkGetPhysicalDeviceProperties(
                physicalDevice, &physicalDeviceProperties);
        std::uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(
                physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(
                queueFamilyCount);
        if (queueFamilyCount > 0) {
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,
                    &queueFamilyCount, queueFamilyProperties.data());
            queueFamilyProperties.resize(queueFamilyCount);
        }
        graphicsTimestampValidBits =
                graphicsQueueFamily < queueFamilyProperties.size()
                ? queueFamilyProperties[graphicsQueueFamily]
                          .timestampValidBits
                : 0;
        vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);
        deviceContext = std::make_shared<VulkanDeviceContext>(
                physicalDevice, device, resourceCounters);

        VkCommandPoolCreateInfo commandPoolInfo{};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = graphicsQueueFamily;
        result = VulkanCommandPool::create(
                deviceContext, commandPoolInfo, commandPool);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateCommandPool", result);
            destroyDevice();
            return false;
        }

        // Object names are useful only when a validation consumer is present.
        // Some Android emulator loaders advertise debug-utils but abort inside
        // vkSetDebugUtilsObjectNameEXT when no validation layer consumes names.
        if (validationEnabled && debugUtilsEnabled) {
            setDebugObjectName =
                    reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
                            vkGetDeviceProcAddr(
                                    device, "vkSetDebugUtilsObjectNameEXT"));
        }
        nameObject(VK_OBJECT_TYPE_COMMAND_POOL,
                commandPool.get(), "Cellularity Vulkan command pool");
        if (!createBackgroundShaderResources()) {
            destroyDevice();
            return false;
        }
        std::string basicCellError;
        if (!basicCellRenderer.initialize(deviceContext, commandPool,
                    graphicsQueue, setDebugObjectName, cellShaderSpirv,
                    worldShaderSpirv, std::move(dotTexturePayload),
                    std::move(besselTexturePayload), basicCellError)) {
            setFailure(std::move(basicCellError));
            destroyDevice();
            return false;
        }
        // Persistent VkImage resources now own the content. Explicitly drop
        // any moved-from capacity so the session retains no decoded CPU copy.
        dotTexturePayload = {};
        besselTexturePayload = {};
        for (std::size_t index = 0; index < frames.size(); ++index) {
            basicCellRenderer.updateFrameDescriptor(
                    index, frames[index].uploadArena.buffer());
        }
        return true;
    }

    [[nodiscard]] bool ensureDeviceForSurface() {
        if (device != VK_NULL_HANDLE && currentDeviceSupportsSurface()) {
            return true;
        }
        if (device != VK_NULL_HANDLE) {
            destroyDevice();
        }
        return createDeviceForSurface();
    }

    template <typename Handle>
    void nameObject(VkObjectType objectType, Handle handle,
            const char* name) const {
        if (setDebugObjectName == nullptr || handle == VK_NULL_HANDLE) {
            return;
        }
        const VkDebugUtilsObjectNameInfoEXT info{
                VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                nullptr,
                objectType,
                handleValue(handle),
                name
        };
        setDebugObjectName(device, &info);
    }

    [[nodiscard]] bool createShaderModule(
            const std::vector<std::uint32_t>& spirv,
            const char* operation, VulkanShaderModule& shaderModule) {
        constexpr std::uint32_t kSpirvMagic = 0x07230203U;
        if (spirv.empty() || spirv.front() != kSpirvMagic) {
            setFailure(std::string(operation) + " received invalid SPIR-V");
            return false;
        }
        const VkShaderModuleCreateInfo createInfo{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                nullptr,
                0,
                spirv.size() * sizeof(std::uint32_t),
                spirv.data()
        };
        VkShaderModule handle = VK_NULL_HANDLE;
        const VkResult result = vkCreateShaderModule(
                device, &createInfo, nullptr, &handle);
        if (result != VK_SUCCESS) {
            setFailure(operation, result);
            return false;
        }
        shaderModule = VulkanShaderModule(deviceContext, handle);
        return true;
    }

    [[nodiscard]] bool createBackgroundShaderResources() {
        if (backgroundVertexShader && backgroundFragmentShader
                && densityFieldVertexShader
                && densityFieldFragmentShader
                && backgroundDescriptorSetLayout
                && descriptorPool.get() != VK_NULL_HANDLE
                && backgroundPipelineLayout) {
            return true;
        }
        if (physicalDeviceProperties.limits.maxUniformBufferRange
                        < sizeof(EnvironmentFieldUniformData)
                || physicalDeviceProperties.limits
                                .maxDescriptorSetUniformBuffersDynamic
                        < 1) {
            setFailure("Vulkan device does not provide the required dynamic "
                       "uniform-buffer range");
            return false;
        }
        if (!createShaderModule(backgroundVertexSpirv,
                    "vkCreateShaderModule(background vertex)",
                    backgroundVertexShader)
                || !createShaderModule(backgroundFragmentSpirv,
                        "vkCreateShaderModule(background fragment)",
                        backgroundFragmentShader)
                || !createShaderModule(densityFieldVertexSpirv,
                        "vkCreateShaderModule(density-field vertex)",
                        densityFieldVertexShader)
                || !createShaderModule(densityFieldFragmentSpirv,
                        "vkCreateShaderModule(density-field fragment)",
                        densityFieldFragmentShader)) {
            return false;
        }

        const VkDescriptorSetLayoutBinding uniformBinding{
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                1,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr
        };
        const VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                nullptr,
                0,
                1,
                &uniformBinding
        };
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkResult result = vkCreateDescriptorSetLayout(device,
                &descriptorLayoutInfo, nullptr, &descriptorSetLayout);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateDescriptorSetLayout(background)", result);
            return false;
        }
        backgroundDescriptorSetLayout = VulkanDescriptorSetLayout(
                deviceContext, descriptorSetLayout);

        const VkDescriptorPoolSize poolSize{
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                kFramesInFlight
        };
        const VkDescriptorPoolCreateInfo poolInfo{
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                nullptr,
                VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                kFramesInFlight,
                1,
                &poolSize
        };
        result = VulkanDescriptorPool::create(
                deviceContext, poolInfo, descriptorPool);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateDescriptorPool(background)", result);
            return false;
        }

        const VkDescriptorSetLayout layout =
                backgroundDescriptorSetLayout.get();
        const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
                VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                nullptr,
                0,
                1,
                &layout,
                0,
                nullptr
        };
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        result = vkCreatePipelineLayout(
                device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
        if (result != VK_SUCCESS) {
            setFailure("vkCreatePipelineLayout(background)", result);
            return false;
        }
        backgroundPipelineLayout = VulkanPipelineLayout(
                deviceContext, pipelineLayout);

        nameObject(VK_OBJECT_TYPE_SHADER_MODULE, backgroundVertexShader.get(),
                "Cellularity Vulkan background vertex shader");
        nameObject(VK_OBJECT_TYPE_SHADER_MODULE, backgroundFragmentShader.get(),
                "Cellularity Vulkan background fragment shader");
        nameObject(VK_OBJECT_TYPE_SHADER_MODULE,
                densityFieldVertexShader.get(),
                "Cellularity Vulkan density-field vertex shader");
        nameObject(VK_OBJECT_TYPE_SHADER_MODULE,
                densityFieldFragmentShader.get(),
                "Cellularity Vulkan density-field fragment shader");
        nameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                backgroundDescriptorSetLayout.get(),
                "Cellularity Vulkan background descriptor layout");
        nameObject(VK_OBJECT_TYPE_DESCRIPTOR_POOL, descriptorPool.get(),
                "Cellularity Vulkan fixed descriptor pool");
        nameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                backgroundPipelineLayout.get(),
                "Cellularity Vulkan background pipeline layout");
        return createFrameResources();
    }

    void destroyBackgroundShaderResources() noexcept {
        destroyFrameResources();
        backgroundPipelineLayout.reset();
        descriptorPool.reset();
        backgroundDescriptorSetLayout.reset();
        densityFieldFragmentShader.reset();
        densityFieldVertexShader.reset();
        backgroundFragmentShader.reset();
        backgroundVertexShader.reset();
    }

    [[nodiscard]] bool createDepthTargets() {
        depthImages.clear();
        depthImageViews.clear();
        depthImages.reserve(swapchainImages.size());
        depthImageViews.reserve(swapchainImages.size());

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = depthFormat;
        imageInfo.extent = {
                surfaceExtent.width,
                surfaceExtent.height,
                1
        };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        for (std::size_t index = 0; index < swapchainImages.size(); ++index) {
            VulkanImage depthImage;
            VkResult result = VulkanImage::create(deviceContext, imageInfo,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, depthImage);
            if (result != VK_SUCCESS) {
                setFailure("vkCreateImage(depth)", result);
                return false;
            }

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = depthImage.get();
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (formatHasStencil(depthFormat)) {
                viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            VkImageView imageView = VK_NULL_HANDLE;
            result = vkCreateImageView(
                    device, &viewInfo, nullptr, &imageView);
            if (result != VK_SUCCESS) {
                setFailure("vkCreateImageView(depth)", result);
                return false;
            }
            VulkanImageView depthImageView(deviceContext, imageView);

            const std::string suffix = std::to_string(index);
            nameObject(VK_OBJECT_TYPE_IMAGE, depthImage.get(),
                    ("Cellularity Vulkan depth image " + suffix).c_str());
            nameObject(VK_OBJECT_TYPE_DEVICE_MEMORY, depthImage.memory(),
                    ("Cellularity Vulkan depth memory " + suffix).c_str());
            nameObject(VK_OBJECT_TYPE_IMAGE_VIEW, depthImageView.get(),
                    ("Cellularity Vulkan depth image view " + suffix).c_str());
            depthImages.push_back(std::move(depthImage));
            depthImageViews.push_back(std::move(depthImageView));
        }
        return true;
    }

    [[nodiscard]] bool createRenderPass() {
        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = surfaceFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = formatHasStencil(depthFormat)
                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout =
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        const VkAttachmentReference colorReference{
                0,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        const VkAttachmentReference depthReference{
                1,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = dependency.srcStageMask;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const VkRenderPassCreateInfo createInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                nullptr,
                0,
                static_cast<std::uint32_t>(attachments.size()),
                attachments.data(),
                1,
                &subpass,
                1,
                &dependency
        };
        VkRenderPass handle = VK_NULL_HANDLE;
        const VkResult result = vkCreateRenderPass(
                device, &createInfo, nullptr, &handle);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateRenderPass", result);
            return false;
        }
        renderPass = VulkanRenderPass(deviceContext, handle);
        nameObject(VK_OBJECT_TYPE_RENDER_PASS,
                renderPass.get(), "Cellularity Vulkan background render pass");
        return true;
    }

    [[nodiscard]] bool createFullscreenPipeline(
            VkRenderPass targetRenderPass, VkShaderModule vertexShader,
            VkShaderModule fragmentShader,
            VkBool32 blendEnabled, const char* operation,
            const char* debugName, VulkanPipeline& output) {
        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {{
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        nullptr,
                        0,
                        VK_SHADER_STAGE_VERTEX_BIT,
                        vertexShader,
                        "main",
                        nullptr},
                {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        nullptr,
                        0,
                        VK_SHADER_STAGE_FRAGMENT_BIT,
                        fragmentShader,
                        "main",
                        nullptr},
        }};

        const VkPipelineVertexInputStateCreateInfo vertexInput{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                nullptr,
                0,
                0,
                nullptr,
                0,
                nullptr
        };
        const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                nullptr,
                0,
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                VK_FALSE
        };
        const VkPipelineViewportStateCreateInfo viewportState{
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                nullptr,
                0,
                1,
                nullptr,
                1,
                nullptr
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
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = blendEnabled;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor =
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor =
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT;
        const VkPipelineColorBlendStateCreateInfo colorBlending{
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                nullptr,
                0,
                VK_FALSE,
                VK_LOGIC_OP_COPY,
                1,
                &colorBlendAttachment,
                {0.0F, 0.0F, 0.0F, 0.0F}
        };

        constexpr std::array<VkDynamicState, 2> dynamicStates = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
        };
        const VkPipelineDynamicStateCreateInfo dynamicState{
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                nullptr,
                0,
                static_cast<std::uint32_t>(dynamicStates.size()),
                dynamicStates.data()
        };

        VkGraphicsPipelineCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.stageCount =
                static_cast<std::uint32_t>(shaderStages.size());
        createInfo.pStages = shaderStages.data();
        createInfo.pVertexInputState = &vertexInput;
        createInfo.pInputAssemblyState = &inputAssembly;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterization;
        createInfo.pMultisampleState = &multisampling;
        createInfo.pDepthStencilState = &depthStencil;
        createInfo.pColorBlendState = &colorBlending;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = backgroundPipelineLayout.get();
        createInfo.renderPass = targetRenderPass;
        createInfo.subpass = 0;

        VkPipeline handle = VK_NULL_HANDLE;
        const VkResult result = vkCreateGraphicsPipelines(device,
                VK_NULL_HANDLE, 1, &createInfo, nullptr,
                &handle);
        if (result != VK_SUCCESS) {
            setFailure(operation, result);
            return false;
        }
        output = VulkanPipeline(deviceContext, handle);
        nameObject(VK_OBJECT_TYPE_PIPELINE, output.get(), debugName);
        return true;
    }

    [[nodiscard]] bool createBackgroundPipeline() {
        if (!renderPass || !backgroundVertexShader
                || !backgroundFragmentShader
                || !backgroundPipelineLayout) {
            setFailure("Cannot create the Vulkan background pipeline without shader resources");
            return false;
        }
        return createFullscreenPipeline(renderPass.get(),
                backgroundVertexShader.get(),
                backgroundFragmentShader.get(), VK_FALSE,
                "vkCreateGraphicsPipelines(background)",
                "Cellularity Vulkan production background pipeline",
                backgroundPipeline);
    }

    [[nodiscard]] bool createDensityFieldPipeline() {
        if (!renderPass || !densityFieldVertexShader
                || !densityFieldFragmentShader
                || !backgroundPipelineLayout) {
            setFailure("Cannot create the Vulkan density-field pipeline without shader resources");
            return false;
        }
        if (!createFullscreenPipeline(renderPass.get(),
                    densityFieldVertexShader.get(),
                    densityFieldFragmentShader.get(), VK_TRUE,
                    "vkCreateGraphicsPipelines(density field)",
                    "Cellularity Vulkan density-field preview pipeline",
                    densityFieldPipeline)) {
            return false;
        }
        ++densityFieldPipelineCreationCount;
        return true;
    }

    [[nodiscard]] bool createOffscreenRenderResources() {
        if (offscreenRenderPass && offscreenBackgroundPipeline
                && basicCellRenderer.ready(
                        VulkanPipelineTarget::Offscreen)) {
            return true;
        }
        if (device == VK_NULL_HANDLE || !deviceContext
                || depthFormat == VK_FORMAT_UNDEFINED) {
            setFailure("Cannot create off-screen Vulkan resources without a device");
            return false;
        }

        VkImageFormatProperties colorProperties{};
        const VkResult formatResult = vkGetPhysicalDeviceImageFormatProperties(
                physicalDevice, kOffscreenColorFormat, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                0, &colorProperties);
        if (formatResult != VK_SUCCESS) {
            setFailure("Vulkan device cannot render and copy an RGBA8 off-screen image");
            return false;
        }

        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = kOffscreenColorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = formatHasStencil(depthFormat)
                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout =
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        const VkAttachmentReference colorReference{
                0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        const VkAttachmentReference depthReference{
                1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = dependencies[0].srcStageMask;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        const VkRenderPassCreateInfo createInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                nullptr,
                0,
                static_cast<std::uint32_t>(attachments.size()),
                attachments.data(),
                1,
                &subpass,
                static_cast<std::uint32_t>(dependencies.size()),
                dependencies.data(),
        };
        VkRenderPass handle = VK_NULL_HANDLE;
        VkResult result = vkCreateRenderPass(
                device, &createInfo, nullptr, &handle);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateRenderPass(off-screen RGBA8)", result);
            return false;
        }
        offscreenRenderPass = VulkanRenderPass(deviceContext, handle);
        nameObject(VK_OBJECT_TYPE_RENDER_PASS, offscreenRenderPass.get(),
                "Cellularity Vulkan off-screen RGBA8 render pass");

        if (!createFullscreenPipeline(offscreenRenderPass.get(),
                    backgroundVertexShader.get(),
                    backgroundFragmentShader.get(), VK_FALSE,
                    "vkCreateGraphicsPipelines(off-screen background)",
                    "Cellularity Vulkan off-screen background pipeline",
                    offscreenBackgroundPipeline)) {
            destroyOffscreenRenderResources();
            return false;
        }
        std::string pipelineError;
        if (!basicCellRenderer.createPipelines(offscreenRenderPass.get(),
                    pipelineError, VulkanPipelineTarget::Offscreen)) {
            if (!pipelineError.empty()) {
                setFailure(std::move(pipelineError));
            }
            destroyOffscreenRenderResources();
            return false;
        }
        ++offscreenRenderPassCreationCount;
        return true;
    }

    [[nodiscard]] bool ensureOffscreenTarget(
            OffscreenRenderTarget target, int width, int height) {
        if (width <= 0 || height <= 0) {
            setFailure("Off-screen Vulkan dimensions must be positive");
            return false;
        }
        const std::uint32_t unsignedWidth =
                static_cast<std::uint32_t>(width);
        const std::uint32_t unsignedHeight =
                static_cast<std::uint32_t>(height);
        if (unsignedWidth > physicalDeviceProperties.limits.maxImageDimension2D
                || unsignedHeight
                        > physicalDeviceProperties.limits.maxImageDimension2D) {
            setFailure("Off-screen Vulkan dimensions exceed the device limit");
            return false;
        }
        if (!createOffscreenRenderResources()) {
            return false;
        }

        OffscreenTargetResources& existing = offscreenTarget(target);
        if (existing.matches(width, height)) {
            existing.rendered = false;
            return true;
        }

        const VkDeviceSize packedRowBytes =
                static_cast<VkDeviceSize>(unsignedWidth)
                * kRgbaChannelCount;
        const VkDeviceSize rowAlignment = std::max<VkDeviceSize>(
                physicalDeviceProperties.limits
                        .optimalBufferCopyRowPitchAlignment,
                kRgbaChannelCount);
        if (packedRowBytes
                > std::numeric_limits<VkDeviceSize>::max()
                        - (rowAlignment - 1)) {
            setFailure("Off-screen Vulkan row pitch overflows");
            return false;
        }
        const VkDeviceSize rowPitch =
                ((packedRowBytes + rowAlignment - 1) / rowAlignment)
                * rowAlignment;
        if (rowPitch % kRgbaChannelCount != 0
                || rowPitch / kRgbaChannelCount
                        > std::numeric_limits<std::uint32_t>::max()
                || static_cast<VkDeviceSize>(unsignedHeight)
                        > std::numeric_limits<VkDeviceSize>::max()
                                / rowPitch) {
            setFailure("Off-screen Vulkan readback size overflows");
            return false;
        }
        const VkDeviceSize readbackBytes =
                rowPitch * static_cast<VkDeviceSize>(unsignedHeight);

        OffscreenTargetResources candidate;
        VkImageCreateInfo colorInfo{};
        colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        colorInfo.imageType = VK_IMAGE_TYPE_2D;
        colorInfo.format = kOffscreenColorFormat;
        colorInfo.extent = {unsignedWidth, unsignedHeight, 1};
        colorInfo.mipLevels = 1;
        colorInfo.arrayLayers = 1;
        colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult result = VulkanImage::create(deviceContext, colorInfo,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                candidate.colorImage);
        if (result != VK_SUCCESS) {
            setFailure("create off-screen RGBA8 image", result);
            return false;
        }

        const auto createView = [this](VkImage image, VkFormat format,
                                        VkImageAspectFlags aspect,
                                        const char* operation,
                                        VulkanImageView& output) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.components = {
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
            };
            viewInfo.subresourceRange.aspectMask = aspect;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            VkImageView handle = VK_NULL_HANDLE;
            const VkResult viewResult = vkCreateImageView(
                    device, &viewInfo, nullptr, &handle);
            if (viewResult != VK_SUCCESS) {
                setFailure(operation, viewResult);
                return false;
            }
            output = VulkanImageView(deviceContext, handle);
            return true;
        };
        if (!createView(candidate.colorImage.get(), kOffscreenColorFormat,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    "vkCreateImageView(off-screen color)",
                    candidate.colorImageView)) {
            return false;
        }

        VkImageCreateInfo depthInfo = colorInfo;
        depthInfo.format = depthFormat;
        depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        result = VulkanImage::create(deviceContext, depthInfo,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                candidate.depthImage);
        if (result != VK_SUCCESS) {
            setFailure("create off-screen depth image", result);
            return false;
        }
        VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (formatHasStencil(depthFormat)) {
            depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        if (!createView(candidate.depthImage.get(), depthFormat,
                    depthAspect, "vkCreateImageView(off-screen depth)",
                    candidate.depthImageView)) {
            return false;
        }

        const std::array<VkImageView, 2> attachments = {
                candidate.colorImageView.get(),
                candidate.depthImageView.get(),
        };
        const VkFramebufferCreateInfo framebufferInfo{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                nullptr,
                0,
                offscreenRenderPass.get(),
                static_cast<std::uint32_t>(attachments.size()),
                attachments.data(),
                unsignedWidth,
                unsignedHeight,
                1,
        };
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        result = vkCreateFramebuffer(
                device, &framebufferInfo, nullptr, &framebuffer);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateFramebuffer(off-screen RGBA8)", result);
            return false;
        }
        candidate.framebuffer = VulkanFramebuffer(
                deviceContext, framebuffer);

        result = VulkanBuffer::create(deviceContext, readbackBytes,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true,
                candidate.readbackBuffer);
        if (result != VK_SUCCESS) {
            setFailure("create off-screen readback buffer", result);
            return false;
        }
        candidate.rowPitch = rowPitch;
        candidate.width = width;
        candidate.height = height;

        const std::string prefix = "Cellularity Vulkan "
                + std::string(offscreenTargetName(target));
        nameObject(VK_OBJECT_TYPE_IMAGE, candidate.colorImage.get(),
                (prefix + " RGBA8 image").c_str());
        nameObject(VK_OBJECT_TYPE_IMAGE_VIEW,
                candidate.colorImageView.get(),
                (prefix + " RGBA8 image view").c_str());
        nameObject(VK_OBJECT_TYPE_IMAGE, candidate.depthImage.get(),
                (prefix + " depth image").c_str());
        nameObject(VK_OBJECT_TYPE_IMAGE_VIEW,
                candidate.depthImageView.get(),
                (prefix + " depth image view").c_str());
        nameObject(VK_OBJECT_TYPE_FRAMEBUFFER, candidate.framebuffer.get(),
                (prefix + " framebuffer").c_str());
        nameObject(VK_OBJECT_TYPE_BUFFER, candidate.readbackBuffer.get(),
                (prefix + " readback buffer").c_str());
        nameObject(VK_OBJECT_TYPE_DEVICE_MEMORY,
                candidate.readbackBuffer.memory(),
                (prefix + " readback memory").c_str());

        existing.reset();
        existing = std::move(candidate);
        ++offscreenTargetCreationCount;
        return true;
    }

    void destroyOffscreenRenderResources() noexcept {
        activeOffscreenTarget.reset();
        for (OffscreenTargetResources& target : offscreenTargets) {
            target.reset();
        }
        basicCellRenderer.destroyPipelines(
                VulkanPipelineTarget::Offscreen);
        offscreenBackgroundPipeline.reset();
        offscreenRenderPass.reset();
    }

    [[nodiscard]] bool createFramebuffers() {
        framebuffers.clear();
        framebuffers.reserve(swapchainImageViews.size());
        for (std::size_t index = 0;
                index < swapchainImageViews.size(); ++index) {
            const std::array<VkImageView, 2> attachments = {
                    swapchainImageViews[index].get(),
                    depthImageViews[index].get()
            };
            const VkFramebufferCreateInfo createInfo{
                    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                    nullptr,
                    0,
                    renderPass.get(),
                    static_cast<std::uint32_t>(attachments.size()),
                    attachments.data(),
                    surfaceExtent.width,
                    surfaceExtent.height,
                    1
            };
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            const VkResult result = vkCreateFramebuffer(
                    device, &createInfo, nullptr, &framebuffer);
            if (result != VK_SUCCESS) {
                setFailure("vkCreateFramebuffer", result);
                return false;
            }
            VulkanFramebuffer ownedFramebuffer(
                    deviceContext, framebuffer);
            const std::string name = "Cellularity Vulkan framebuffer "
                    + std::to_string(index);
            nameObject(VK_OBJECT_TYPE_FRAMEBUFFER,
                    ownedFramebuffer.get(), name.c_str());
            framebuffers.push_back(std::move(ownedFramebuffer));
        }
        return true;
    }

    [[nodiscard]] bool createFrameResources() {
        if (frames.front().commandBuffer.get() != VK_NULL_HANDLE) {
            return true;
        }

        std::vector<VulkanCommandBuffer> commandBuffers;
        VkResult result = commandPool.allocatePrimary(
                kFramesInFlight, commandBuffers);
        if (result != VK_SUCCESS) {
            setFailure("vkAllocateCommandBuffers", result);
            return false;
        }

        const std::vector<VkDescriptorSetLayout> layouts(
                kFramesInFlight, backgroundDescriptorSetLayout.get());
        std::vector<VulkanDescriptorSet> descriptorSets;
        result = descriptorPool.allocate(layouts, descriptorSets);
        if (result != VK_SUCCESS) {
            setFailure("vkAllocateDescriptorSets(background)", result);
            return false;
        }

        const VkSemaphoreCreateInfo semaphoreInfo{
                VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                nullptr,
                0
        };
        const VkFenceCreateInfo fenceInfo{
                VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                nullptr,
                VK_FENCE_CREATE_SIGNALED_BIT
        };
        performanceQueryPool.reset();
#if CELLULARITY_ENABLE_VULKAN_PROFILING
        if (graphicsTimestampValidBits > 0
                && physicalDeviceProperties.limits.timestampPeriod > 0.0F) {
            const VkQueryPoolCreateInfo queryPoolInfo{
                    VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                    nullptr,
                    0,
                    VK_QUERY_TYPE_TIMESTAMP,
                    kFramesInFlight * kGpuTimestampQueryCount,
                    0,
            };
            VkQueryPool queryPool = VK_NULL_HANDLE;
            if (vkCreateQueryPool(device, &queryPoolInfo, nullptr,
                        &queryPool)
                    == VK_SUCCESS) {
                performanceQueryPool = VulkanQueryPool(
                        deviceContext, queryPool);
                nameObject(VK_OBJECT_TYPE_QUERY_POOL,
                        performanceQueryPool.get(),
                        "Cellularity Vulkan performance timestamps");
            }
        }
#endif
        for (std::size_t index = 0; index < frames.size(); ++index) {
            FrameResources& frame = frames[index];
            frame.deferredReleases = VulkanDeferredReleaseQueue(
                    resourceCounters);
            frame.gpuTimestampFirstQuery = static_cast<std::uint32_t>(index)
                    * kGpuTimestampQueryCount;
            frame.gpuProfileGeneration = 0;
            frame.gpuTimestampRecorded = false;
            frame.gpuTimestampPending = false;
            frame.commandBuffer = std::move(commandBuffers[index]);
            frame.backgroundDescriptorSet = std::move(descriptorSets[index]);

            VkSemaphore imageAvailable = VK_NULL_HANDLE;
            result = vkCreateSemaphore(device, &semaphoreInfo,
                    nullptr, &imageAvailable);
            if (result == VK_SUCCESS) {
                frame.imageAvailable = VulkanSemaphore(
                        deviceContext, imageAvailable);
                VkFence inFlight = VK_NULL_HANDLE;
                result = vkCreateFence(device, &fenceInfo,
                        nullptr, &inFlight);
                if (result == VK_SUCCESS) {
                    frame.inFlight = VulkanFence(deviceContext, inFlight);
                }
            }
            if (result != VK_SUCCESS) {
                setFailure("create frame-in-flight synchronization", result);
                destroyFrameResources();
                return false;
            }
            result = frame.uploadArena.initialize(
                    deviceContext, kInitialUploadArenaBytes);
            if (result != VK_SUCCESS) {
                setFailure("create persistently mapped upload arena", result);
                destroyFrameResources();
                return false;
            }

            updateBackgroundDescriptor(frame);

            const std::string suffix = std::to_string(index);
            nameObject(VK_OBJECT_TYPE_COMMAND_BUFFER,
                    frame.commandBuffer.get(),
                    ("Cellularity Vulkan command buffer " + suffix).c_str());
            nameObject(VK_OBJECT_TYPE_SEMAPHORE,
                    frame.imageAvailable.get(),
                    ("Cellularity Vulkan image-available semaphore "
                            + suffix)
                            .c_str());
            nameObject(VK_OBJECT_TYPE_FENCE,
                    frame.inFlight.get(),
                    ("Cellularity Vulkan in-flight fence " + suffix).c_str());
            nameObject(VK_OBJECT_TYPE_BUFFER,
                    frame.uploadArena.buffer(),
                    ("Cellularity Vulkan upload arena " + suffix).c_str());
            nameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET,
                    frame.backgroundDescriptorSet.get(),
                    ("Cellularity Vulkan background descriptor set "
                            + suffix)
                            .c_str());
        }
        currentFrame = 0;
        return true;
    }

    [[nodiscard]] bool createSwapchain() {
        if (surface == VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
            destroySwapchain();
            status = "waiting_for_surface";
            return false;
        }
        if (requestedWidth <= 0 || requestedHeight <= 0) {
            destroySwapchain();
            status = "waiting_for_nonzero_extent";
            lastResult = "VK_SUCCESS";
            error.clear();
            swapchainRecreatePending = true;
            return false;
        }

        VkSurfaceCapabilitiesKHR capabilities{};
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice, surface, &capabilities);
        if (result != VK_SUCCESS) {
            setFailure("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
            return false;
        }
        if ((capabilities.supportedUsageFlags
                    & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
                == 0) {
            setFailure("Android surface does not support color-attachment swapchain images");
            return false;
        }

        std::vector<VkSurfaceFormatKHR> formats;
        result = enumerateSurfaceFormats(physicalDevice, surface, formats);
        if (result != VK_SUCCESS) {
            setFailure("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
            return false;
        }
        const auto selectedFormat = selectSurfaceFormat(formats);
        if (!selectedFormat.has_value()) {
            setFailure("Required RGBA8/BGRA8 UNORM SRGB surface format is unavailable");
            return false;
        }

        std::vector<VkPresentModeKHR> presentModes;
        result = enumeratePresentModes(
                physicalDevice, surface, presentModes);
        if (result != VK_SUCCESS) {
            setFailure("vkGetPhysicalDeviceSurfacePresentModesKHR", result);
            return false;
        }
        if (std::find(presentModes.begin(), presentModes.end(),
                    VK_PRESENT_MODE_FIFO_KHR)
                == presentModes.end()) {
            setFailure("Required VK_PRESENT_MODE_FIFO_KHR is unavailable");
            return false;
        }

        const VkFormat nextSurfaceFormat = selectedFormat->format;
        const VkColorSpaceKHR nextSurfaceColorSpace =
                selectedFormat->colorSpace;
        const VkExtent2D nextDisplayExtent = selectExtent(
                capabilities, requestedWidth, requestedHeight);
        const VkSurfaceTransformFlagBitsKHR nextSurfaceTransform =
                capabilities.currentTransform;
        const VkSurfaceTransformFlagBitsKHR nextSwapchainPreTransform =
                nextSurfaceTransform;
        const VkExtent2D nextSurfaceExtent = identityExtent(
                nextDisplayExtent, nextSurfaceTransform);
        if (nextSurfaceExtent.width == 0 || nextSurfaceExtent.height == 0) {
            status = "waiting_for_nonzero_extent";
            swapchainRecreatePending = true;
            return false;
        }

        // Surface extent is dynamic state. Keep the compatible render pass and
        // pipelines alive instead of recompiling every program while a view is
        // interactively resized.
        const bool reuseSurfaceRenderResources = swapchain && renderPass
                && surfaceFormat == nextSurfaceFormat;
        waitForSwapchainResourcesIdle();
        VulkanSwapchain retiredSwapchain = std::move(swapchain);
        destroySwapchainExtentResources();
        if (!reuseSurfaceRenderResources) {
            destroySurfaceRenderResources();
        }
        surfaceFormat = nextSurfaceFormat;
        surfaceColorSpace = nextSurfaceColorSpace;
        displayExtent = nextDisplayExtent;
        surfaceExtent = nextSurfaceExtent;
        surfaceTransform = nextSurfaceTransform;
        swapchainPreTransform = nextSwapchainPreTransform;

        std::uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            imageCount = std::min(imageCount, capabilities.maxImageCount);
        }
        const std::array<std::uint32_t, 2> queueFamilies = {
                graphicsQueueFamily,
                presentQueueFamily
        };
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat;
        createInfo.imageColorSpace = surfaceColorSpace;
        createInfo.imageExtent = surfaceExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (graphicsQueueFamily != presentQueueFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount =
                    static_cast<std::uint32_t>(queueFamilies.size());
            createInfo.pQueueFamilyIndices = queueFamilies.data();
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        createInfo.preTransform = swapchainPreTransform;
        createInfo.compositeAlpha = selectCompositeAlpha(
                capabilities.supportedCompositeAlpha);
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = retiredSwapchain.get();

        VkSwapchainKHR swapchainHandle = VK_NULL_HANDLE;
        result = vkCreateSwapchainKHR(
                device, &createInfo, nullptr, &swapchainHandle);
        if (result != VK_SUCCESS) {
            setFailure("vkCreateSwapchainKHR", result);
            return false;
        }
        swapchain = VulkanSwapchain(deviceContext, swapchainHandle);
        retiredSwapchain.reset();
        nameObject(VK_OBJECT_TYPE_SWAPCHAIN_KHR,
                swapchain.get(), "Cellularity Vulkan FIFO swapchain");

        imageCount = 0;
        result = vkGetSwapchainImagesKHR(
                device, swapchain.get(), &imageCount, nullptr);
        if (result != VK_SUCCESS || imageCount == 0) {
            if (result == VK_SUCCESS) {
                setFailure("Vulkan swapchain contains no images");
            } else {
                setFailure("vkGetSwapchainImagesKHR(count)", result);
            }
            destroySwapchain();
            return false;
        }
        swapchainImages.resize(imageCount);
        result = vkGetSwapchainImagesKHR(
                device, swapchain.get(), &imageCount, swapchainImages.data());
        swapchainImages.resize(imageCount);
        if (result != VK_SUCCESS) {
            setFailure("vkGetSwapchainImagesKHR(images)", result);
            destroySwapchain();
            return false;
        }

        swapchainImageViews.clear();
        renderFinishedSemaphores.clear();
        swapchainImageViews.reserve(swapchainImages.size());
        renderFinishedSemaphores.reserve(swapchainImages.size());
        const VkSemaphoreCreateInfo semaphoreInfo{
                VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                nullptr,
                0
        };
        for (std::size_t index = 0;
                index < swapchainImages.size(); ++index) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = swapchainImages[index];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = surfaceFormat;
            viewInfo.components = {
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY
            };
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            VkImageView imageView = VK_NULL_HANDLE;
            result = vkCreateImageView(device, &viewInfo,
                    nullptr, &imageView);
            if (result != VK_SUCCESS) {
                setFailure("vkCreateImageView(swapchain)", result);
                destroySwapchain();
                return false;
            }
            VulkanImageView ownedImageView(deviceContext, imageView);

            VkSemaphore renderFinished = VK_NULL_HANDLE;
            result = vkCreateSemaphore(device, &semaphoreInfo,
                    nullptr, &renderFinished);
            if (result != VK_SUCCESS) {
                setFailure("vkCreateSemaphore(render finished)", result);
                destroySwapchain();
                return false;
            }
            VulkanSemaphore ownedRenderFinished(
                    deviceContext, renderFinished);

            const std::string name = "Cellularity Vulkan swapchain image view "
                    + std::to_string(index);
            nameObject(VK_OBJECT_TYPE_IMAGE_VIEW,
                    ownedImageView.get(), name.c_str());
            nameObject(VK_OBJECT_TYPE_SEMAPHORE,
                    ownedRenderFinished.get(),
                    ("Cellularity Vulkan image render-finished semaphore "
                            + std::to_string(index))
                            .c_str());
            swapchainImageViews.push_back(std::move(ownedImageView));
            renderFinishedSemaphores.push_back(
                    std::move(ownedRenderFinished));
        }

        std::string basicCellError;
        if (!createDepthTargets()
                || (!reuseSurfaceRenderResources
                        && (!createRenderPass()
                                || !createBackgroundPipeline()
                                || !basicCellRenderer.createPipelines(
                                        renderPass.get(), basicCellError)))
                || !createFramebuffers()) {
            if (!basicCellError.empty()) {
                setFailure(std::move(basicCellError));
            }
            destroySwapchain();
            return false;
        }

        imageFences.assign(swapchainImages.size(), VK_NULL_HANDLE);
        ++swapchainCreationCount;
        swapchainRecreatePending = false;
        status = "ready";
        lastResult = "VK_SUCCESS";
        error.clear();
        return true;
    }

    [[nodiscard]] BackgroundUniformData backgroundUniformData(
            const PreparedRenderFrameView& frame,
            const RenderPresentationState& presentation) const {
        BackgroundUniformData uniforms;
        uniforms.mvpMatrix = presentation.viewport.mvpMatrix;
        uniforms.lightDirectionRangeIntensity = {
                static_cast<float>(std::cos(frame.environment.lightAngle)),
                static_cast<float>(std::sin(frame.environment.lightAngle)),
                static_cast<float>(frame.environment.lightRange),
                static_cast<float>(frame.environment.lightIntensity),
        };
        uniforms.backgroundColorRadius = {
                presentation.backgroundColor[0],
                presentation.backgroundColor[1],
                presentation.backgroundColor[2],
                static_cast<float>(frame.environment.dishRadius),
        };
        const double innerRatio = frame.environment.dishInnerRadius / frame.environment.dishRadius;
        uniforms.geometry[0] = static_cast<float>(innerRatio * innerRatio);
        const std::array<float, 4> rotation =
                preRotationMatrix(renderPreTransform());
        uniforms.preRotation = {
                rotation[0], rotation[1], 0.0F, 0.0F,
                rotation[2], rotation[3], 0.0F, 0.0F
        };
        return uniforms;
    }

    [[nodiscard]] CellUniformData cellUniformData(
            const RenderPresentationState& presentation) const {
        CellUniformData uniforms;
        uniforms.mvpMatrix = presentation.viewport.mvpMatrix;
        const std::array<float, 4> rotation =
                preRotationMatrix(renderPreTransform());
        uniforms.preRotation = {
                rotation[0],
                rotation[1],
                0.0F,
                0.0F,
                rotation[2],
                rotation[3],
                0.0F,
                0.0F,
        };
        return uniforms;
    }

    [[nodiscard]] BackgroundUniformData environmentPreviewBackgroundUniformData(
            const EnvironmentFieldPreviewCommand& command) const {
        BackgroundUniformData uniforms;
        uniforms.mvpMatrix = command.matrix;
        uniforms.lightDirectionRangeIntensity = {
                0.0F,
                1.0F,
                0.75F,
                0.18F,
        };
        uniforms.backgroundColorRadius = {
                0.78F,
                0.80F,
                0.83F,
                static_cast<float>(command.dishRadius),
        };
        const std::array<float, 4> rotation =
                preRotationMatrix(renderPreTransform());
        uniforms.preRotation = {
                rotation[0],
                rotation[1],
                0.0F,
                0.0F,
                rotation[2],
                rotation[3],
                0.0F,
                0.0F,
        };
        return uniforms;
    }

    [[nodiscard]] EnvironmentFieldUniformData environmentFieldUniformData(
            const EnvironmentFieldPreviewCommand& command) const {
        EnvironmentFieldUniformData uniforms;
        uniforms.mvpMatrix = command.matrix;
        uniforms.field = {
                static_cast<float>(command.fieldType),
                static_cast<float>(command.fieldBase),
                static_cast<float>(command.fieldStrength),
                static_cast<float>(command.fieldAngle),
        };
        uniforms.fieldExtra = {
                static_cast<float>(command.fieldOffset),
                static_cast<float>(command.fieldSoftness),
                static_cast<float>(command.displayMin),
                static_cast<float>(command.displayMax),
        };
        uniforms.preview = {
                static_cast<float>(command.dishRadius),
                command.vectorPreview ? 1.0F : 0.0F,
                static_cast<float>(command.gravityFieldType),
                static_cast<float>(command.gravityFieldAngle),
        };
        const std::array<float, 4> rotation =
                preRotationMatrix(renderPreTransform());
        uniforms.preRotation = {
                rotation[0],
                rotation[1],
                0.0F,
                0.0F,
                rotation[2],
                rotation[3],
                0.0F,
                0.0F,
        };
        return uniforms;
    }

    void updateBackgroundDescriptor(const FrameResources& frame) const {
        const VkDescriptorBufferInfo bufferInfo{
                frame.uploadArena.buffer(),
                0,
                sizeof(EnvironmentFieldUniformData)
        };
        const VkWriteDescriptorSet descriptorWrite{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                nullptr,
                frame.backgroundDescriptorSet.get(),
                0,
                0,
                1,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                nullptr,
                &bufferInfo,
                nullptr
        };
        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
        ++resourceCounters->descriptorUpdateCount;
    }

    [[nodiscard]] bool prepareFrameUniforms(FrameResources& frame,
            std::size_t frameIndex,
            const PreparedRenderFrameView& preparedFrame,
            const RenderPresentationState& presentation,
            std::uint32_t& backgroundDynamicOffset,
            std::uint32_t& cellDynamicOffset) {
        frame.deferredReleases.releaseAll();
        const VkDeviceSize alignment = std::max<VkDeviceSize>(
                physicalDeviceProperties.limits
                        .minUniformBufferOffsetAlignment,
                alignof(BackgroundUniformData));
        const VkDeviceSize requiredCapacity = sizeof(BackgroundUniformData)
                + alignment - 1 + sizeof(CellUniformData);
        bool bufferChanged = false;
        VkResult result = frame.uploadArena.beginFrame(
                requiredCapacity,
                frame.deferredReleases, bufferChanged);
        if (result != VK_SUCCESS) {
            setFailure("grow frame upload arena", result);
            return false;
        }
        if (bufferChanged) {
            updateBackgroundDescriptor(frame);
            basicCellRenderer.updateFrameDescriptor(
                    frameIndex, frame.uploadArena.buffer());
        }

        const BackgroundUniformData background =
                backgroundUniformData(preparedFrame, presentation);
        const auto backgroundSlice = frame.uploadArena.write(
                &background, sizeof(background), alignment, result);
        if (!backgroundSlice.has_value()) {
            setFailure("write background uniform data", result);
            return false;
        }
        const CellUniformData cells = cellUniformData(presentation);
        const auto cellSlice = frame.uploadArena.write(
                &cells, sizeof(cells), alignment, result);
        if (!cellSlice.has_value()) {
            setFailure("write basic-cell uniform data", result);
            return false;
        }
        if (backgroundSlice->offset
                        > std::numeric_limits<std::uint32_t>::max()
                || cellSlice->offset
                        > std::numeric_limits<std::uint32_t>::max()) {
            setFailure("Background dynamic uniform offset exceeds Vulkan 1.0 limits");
            return false;
        }
        backgroundDynamicOffset =
                static_cast<std::uint32_t>(backgroundSlice->offset);
        cellDynamicOffset =
                static_cast<std::uint32_t>(cellSlice->offset);
        return true;
    }

    [[nodiscard]] bool recordPreparedFrameCommand(
            FrameResources& frame, std::size_t frameIndex,
            std::uint32_t backgroundDynamicOffset,
            std::uint32_t cellDynamicOffset, bool drawText,
            const RenderDestination& destination) {
        const VkCommandBuffer commandBuffer = frame.commandBuffer.get();
        const VkCommandBufferBeginInfo beginInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                nullptr,
                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                nullptr
        };
        VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            setFailure("vkBeginCommandBuffer", result);
            return false;
        }
        frame.gpuTimestampRecorded = false;
        VulkanGpuTimestampWriter timestampWriter;
#if CELLULARITY_ENABLE_VULKAN_PROFILING
        if (performanceProfilingEnabled && performanceQueryPool
                && destination.pipelineTarget
                        == VulkanPipelineTarget::Surface) {
            vkCmdResetQueryPool(commandBuffer, performanceQueryPool.get(),
                    frame.gpuTimestampFirstQuery,
                    kGpuTimestampQueryCount);
            vkCmdWriteTimestamp(commandBuffer,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    performanceQueryPool.get(),
                    frame.gpuTimestampFirstQuery);
            timestampWriter = {
                    performanceQueryPool.get(),
                    frame.gpuTimestampFirstQuery + 1,
                    frame.gpuTimestampFirstQuery
                            + kGpuTimestampQueryCount,
            };
        }
#endif

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.31372549019F,
                0.31372549019F,
                0.31372549019F,
                1.0F}};
        clearValues[1].depthStencil = {1.0F, 0};
        const VkRenderPassBeginInfo renderPassInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                nullptr,
                destination.renderPass,
                destination.framebuffer,
                {{0, 0}, destination.extent},
                static_cast<std::uint32_t>(clearValues.size()),
                clearValues.data()
        };
        vkCmdBeginRenderPass(
                commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                destination.backgroundPipeline);
        const VkViewport viewport{
                0.0F,
                0.0F,
                static_cast<float>(destination.extent.width),
                static_cast<float>(destination.extent.height),
                0.0F,
                1.0F
        };
        const VkRect2D scissor{{0, 0}, destination.extent};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        const VkDescriptorSet descriptorSet =
                frame.backgroundDescriptorSet.get();
        ++resourceCounters->descriptorBindCount;
        vkCmdBindDescriptorSets(commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                backgroundPipelineLayout.get(), 0, 1, &descriptorSet,
                1, &backgroundDynamicOffset);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
        if (timestampWriter.queryPool != VK_NULL_HANDLE) {
            timestampWriter.checkpoint(commandBuffer);
        }
        basicCellRenderer.record(commandBuffer, frameIndex,
                cellDynamicOffset, drawText,
                destination.pipelineTarget,
                timestampWriter.queryPool != VK_NULL_HANDLE
                        ? &timestampWriter
                        : nullptr);
        frame.gpuTimestampRecorded =
                timestampWriter.queryPool != VK_NULL_HANDLE
                && timestampWriter.nextQuery == timestampWriter.endQuery;
        vkCmdEndRenderPass(commandBuffer);
        if (destination.readbackTarget != nullptr) {
            const OffscreenTargetResources& readback =
                    *destination.readbackTarget;
            VkBufferImageCopy copy{};
            copy.bufferRowLength = static_cast<std::uint32_t>(
                    readback.rowPitch / kRgbaChannelCount);
            copy.bufferImageHeight =
                    static_cast<std::uint32_t>(readback.height);
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {
                    static_cast<std::uint32_t>(readback.width),
                    static_cast<std::uint32_t>(readback.height),
                    1,
            };
            vkCmdCopyImageToBuffer(commandBuffer,
                    readback.colorImage.get(),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    readback.readbackBuffer.get(), 1, &copy);
        }
        result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) {
            setFailure("vkEndCommandBuffer", result);
            return false;
        }
        ++resourceCounters->commandBufferRecordCount;
        return true;
    }

    [[nodiscard]] ClearFrameResult handleSurfaceLoss(VkResult result) {
        lastResult = resultName(result);
        error = resultDescription("Vulkan surface operation", result);
        status = "surface_lost";
        destroySwapchain();
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        if (window != nullptr) {
            ANativeWindow_release(window);
            window = nullptr;
        }
        return ClearFrameResult::SurfaceLost;
    }

    [[nodiscard]] bool waitForFence(
            VkFence fence, const char* operation) {
        ++resourceCounters->fenceWaitCount;
        VkResult result = vkWaitForFences(
                device, 1, &fence, VK_TRUE, 0);
        if (result == VK_TIMEOUT) {
            ++resourceCounters->blockingFenceWaitCount;
            result = vkWaitForFences(device, 1, &fence, VK_TRUE,
                    std::numeric_limits<std::uint64_t>::max());
        }
        if (result != VK_SUCCESS) {
            setFailure(operation, result);
            return false;
        }
        return true;
    }

    void markFrameComplete(FrameResources& frame) noexcept {
        if (frame.submissionPending) {
            completedSubmissionSerial = std::max(
                    completedSubmissionSerial, frame.submissionSerial);
            frame.submissionPending = false;
        }
    }

    [[nodiscard]] bool waitForFrameCompletion(
            FrameResources& frame, const char* operation) {
        if (!frame.submissionPending) {
            return true;
        }
        if (!waitForFence(frame.inFlight.get(), operation)) {
            return false;
        }
        collectGpuTimestamps(frame);
        markFrameComplete(frame);
        return true;
    }

    [[nodiscard]] bool waitForSubmissionSerial(
            std::uint64_t submissionSerial) {
        if (submissionSerial <= completedSubmissionSerial) {
            return true;
        }
        for (FrameResources& frame : frames) {
            if (frame.submissionPending
                    && frame.submissionSerial == submissionSerial) {
                return waitForFrameCompletion(
                        frame, "vkWaitForFences(cell geometry)");
            }
        }
        ++resourceCounters->synchronizationReuseViolationCount;
        setFailure("Vulkan basic-cell geometry refers to an unknown submission");
        return false;
    }

    [[nodiscard]] bool waitForImageFence(
            VkFence fence, FrameResources& currentFrameResources) {
        if (fence == VK_NULL_HANDLE
                || fence == currentFrameResources.inFlight.get()) {
            return true;
        }
        for (FrameResources& frame : frames) {
            if (frame.inFlight.get() == fence) {
                return waitForFrameCompletion(
                        frame, "vkWaitForFences(image)");
            }
        }
        ++resourceCounters->synchronizationReuseViolationCount;
        setFailure("Swapchain image refers to an unknown in-flight fence");
        return false;
    }

    void markAllFrameSubmissionsComplete() noexcept {
        for (FrameResources& frame : frames) {
            collectGpuTimestamps(frame);
            markFrameComplete(frame);
        }
    }

    [[nodiscard]] ClearFrameResult renderPreparedFrameOffscreen(
            const PreparedRenderFrameView& preparedFrame,
            const RenderPresentationState& presentation) {
        if (!offscreenCaptureActive(
                    OffscreenRenderTarget::SubstratePreview)) {
            setFailure("A substrate frame was submitted to the wrong off-screen target");
            return ClearFrameResult::Failed;
        }
        OffscreenTargetResources& target = offscreenTarget(
                OffscreenRenderTarget::SubstratePreview);
        target.rendered = false;
        if (!target.matches(presentation.viewport.surfaceWidth,
                    presentation.viewport.surfaceHeight)) {
            setFailure("Substrate preview dimensions do not match its off-screen target");
            return ClearFrameResult::Failed;
        }

        FrameResources& frame = frames[currentFrame];
        if (!waitForFrameCompletion(
                    frame, "vkWaitForFences(off-screen substrate preview)")) {
            return ClearFrameResult::Failed;
        }
        if (basicCellRenderer.needsGeometryUpload(preparedFrame)) {
            while (const auto requiredSerial =
                            basicCellRenderer.requiredGeometryWaitSerial(
                                    completedSubmissionSerial)) {
                if (!waitForSubmissionSerial(*requiredSerial)) {
                    return ClearFrameResult::Failed;
                }
            }
        }
        std::string geometryError;
        if (!basicCellRenderer.uploadGeometry(preparedFrame,
                    completedSubmissionSerial, geometryError)) {
            setFailure(std::move(geometryError));
            return ClearFrameResult::Failed;
        }

        std::uint32_t backgroundDynamicOffset = 0;
        std::uint32_t cellDynamicOffset = 0;
        if (!prepareFrameUniforms(frame, currentFrame, preparedFrame,
                    presentation, backgroundDynamicOffset,
                    cellDynamicOffset)) {
            return ClearFrameResult::Failed;
        }

        VkResult result = vkResetCommandBuffer(
                frame.commandBuffer.get(), 0);
        if (result != VK_SUCCESS) {
            setFailure("vkResetCommandBuffer(off-screen substrate preview)",
                    result);
            return ClearFrameResult::Failed;
        }
        const RenderDestination destination{
                offscreenRenderPass.get(),
                target.framebuffer.get(),
                {static_cast<std::uint32_t>(target.width),
                        static_cast<std::uint32_t>(target.height)},
                offscreenBackgroundPipeline.get(),
                VulkanPipelineTarget::Offscreen,
                &target,
        };
        if (!recordPreparedFrameCommand(frame, currentFrame,
                    backgroundDynamicOffset, cellDynamicOffset, false,
                    destination)) {
            return ClearFrameResult::Failed;
        }

        const VkFence inFlightFence = frame.inFlight.get();
        result = vkResetFences(device, 1, &inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkResetFences(off-screen substrate preview)", result);
            return ClearFrameResult::Failed;
        }
        const VkCommandBuffer commandBuffer = frame.commandBuffer.get();
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        result = vkQueueSubmit(
                graphicsQueue, 1, &submitInfo, inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkQueueSubmit(off-screen substrate preview)", result);
            return ClearFrameResult::Failed;
        }
        frame.submissionPending = true;
        frame.submissionSerial = nextSubmissionSerial++;
        frame.gpuTimestampPending = frame.gpuTimestampRecorded;
        frame.gpuProfileGeneration = performanceProfileGeneration;
        basicCellRenderer.markActiveGeometryUsed(frame.submissionSerial);
        ++resourceCounters->queueSubmissionCount;
        if (!waitForFrameCompletion(frame,
                    "vkWaitForFences(off-screen substrate readback)")) {
            return ClearFrameResult::Failed;
        }

        target.rendered = true;
        status = "ready";
        lastResult = "VK_SUCCESS";
        error.clear();
        return ClearFrameResult::Presented;
    }

    [[nodiscard]] ClearFrameResult renderFrame() {
        return renderFrame(true);
    }

    [[nodiscard]] ClearFrameResult renderFrame(bool performanceHudEnabled) {
        demoPerformanceHudEnabled = performanceHudEnabled;
        RenderPresentationState presentation =
                makeVulkanBasicCellDemoPresentation(
                        requestedWidth, requestedHeight);
        return renderFrame(makePreparedRenderFrameView(demoFrame), presentation,
                performanceHudEnabled ? &demoTelemetry : nullptr);
    }

    [[nodiscard]] ClearFrameResult renderFrame(
            const PreparedRenderFrameView& preparedFrame,
            const RenderPresentationState& presentation,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry) {
        lastFrameWasPresented = false;
        if (!checkThread("renderFrame")) {
            return ClearFrameResult::Failed;
        }
        if (status == "failed") {
            return ClearFrameResult::Failed;
        }
        if (!preparedFrame.valid || !preparedFrame.geometryReady
                || preparedFrame.layout.version
                        != kPreparedRenderFrameLayoutVersion) {
            setFailure(
                    "Vulkan renderFrame requires a published compatible PreparedRenderFrame");
            return ClearFrameResult::Failed;
        }
        if (activeOffscreenTarget.has_value()) {
            if (*activeOffscreenTarget
                    != OffscreenRenderTarget::SubstratePreview) {
                setFailure("A microscope frame cannot render into the active Genome Preview target");
                return ClearFrameResult::Failed;
            }
            return renderPreparedFrameOffscreen(
                    preparedFrame, presentation);
        }
        if (surface == VK_NULL_HANDLE
                || requestedWidth <= 0 || requestedHeight <= 0) {
            return ClearFrameResult::Deferred;
        }
        if (!swapchain || swapchainRecreatePending) {
            if (!createSwapchain()) {
                return status == "waiting_for_nonzero_extent"
                        ? ClearFrameResult::Deferred
                        : ClearFrameResult::Failed;
            }
        }

#if CELLULARITY_ENABLE_VULKAN_PROFILING
        const bool profileFrame = performanceProfilingEnabled;
#else
        constexpr bool profileFrame = false;
#endif
        VulkanFrameCpuSample cpuSample;
        if (profileFrame) {
            cpuSample.startedAtNanos = performanceNowNanos();
        }

        const PixelTextRenderer* textGeometry = nullptr;
        if (debugHudTelemetry != nullptr) {
            textGeometry = &debugHud.prepareGeometry(
                    static_cast<int>(surfaceExtent.width),
                    static_cast<int>(surfaceExtent.height),
                    *debugHudTelemetry);
        }

        SteadyStateFrameScope steadyStateScope(*resourceCounters);
        FrameResources& frame = frames[currentFrame];
        std::uint64_t phaseStartedAtNanos =
                profileFrame ? performanceNowNanos() : 0;
        if (!waitForFrameCompletion(frame, "vkWaitForFences(frame)")) {
            return ClearFrameResult::Failed;
        }
        if (basicCellRenderer.needsGeometryUpload(preparedFrame)) {
            while (const auto requiredSerial =
                            basicCellRenderer.requiredGeometryWaitSerial(
                                    completedSubmissionSerial)) {
                if (!waitForSubmissionSerial(*requiredSerial)) {
                    return ClearFrameResult::Failed;
                }
            }
        }
        if (profileFrame) {
            cpuSample.fenceWaitNanos +=
                    performanceNowNanos() - phaseStartedAtNanos;
            phaseStartedAtNanos = performanceNowNanos();
        }
        std::string geometryError;
        if (!basicCellRenderer.uploadGeometry(preparedFrame,
                    completedSubmissionSerial, geometryError)) {
            setFailure(std::move(geometryError));
            return ClearFrameResult::Failed;
        }
        if (profileFrame) {
            cpuSample.uploadNanos +=
                    performanceNowNanos() - phaseStartedAtNanos;
        }
        if (textGeometry != nullptr) {
            if (basicCellRenderer.needsTextGeometryUpload(*textGeometry)) {
                if (profileFrame) {
                    phaseStartedAtNanos = performanceNowNanos();
                }
                while (const auto requiredSerial = basicCellRenderer
                                .requiredTextGeometryWaitSerial(
                                        completedSubmissionSerial)) {
                    if (!waitForSubmissionSerial(*requiredSerial)) {
                        return ClearFrameResult::Failed;
                    }
                }
                if (profileFrame) {
                    cpuSample.fenceWaitNanos +=
                            performanceNowNanos() - phaseStartedAtNanos;
                }
            }
            if (profileFrame) {
                phaseStartedAtNanos = performanceNowNanos();
            }
            if (!basicCellRenderer.uploadTextGeometry(*textGeometry,
                        completedSubmissionSerial, geometryError)) {
                setFailure(std::move(geometryError));
                return ClearFrameResult::Failed;
            }
            if (profileFrame) {
                cpuSample.uploadNanos +=
                        performanceNowNanos() - phaseStartedAtNanos;
            }
        }

        std::uint32_t imageIndex = 0;
        if (profileFrame) {
            phaseStartedAtNanos = performanceNowNanos();
        }
        VkResult result = vkAcquireNextImageKHR(
                device, swapchain.get(), kAcquireTimeoutNanoseconds,
                frame.imageAvailable.get(), VK_NULL_HANDLE, &imageIndex);
        if (profileFrame) {
            cpuSample.acquireNanos =
                    performanceNowNanos() - phaseStartedAtNanos;
        }
        if (result == VK_TIMEOUT || result == VK_NOT_READY) {
            if (profileFrame) {
                ++profiledAcquireDeferredCount;
            }
            lastResult = resultName(result);
            return ClearFrameResult::Deferred;
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainRecreatePending = true;
            steadyStateScope.end();
            return createSwapchain()
                    ? ClearFrameResult::SwapchainRecreated
                    : ClearFrameResult::Failed;
        }
        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            return handleSurfaceLoss(result);
        }
        const bool acquireSuboptimal = result == VK_SUBOPTIMAL_KHR;
        if (result != VK_SUCCESS && !acquireSuboptimal) {
            setFailure("vkAcquireNextImageKHR", result);
            return ClearFrameResult::Failed;
        }

        if (imageIndex >= imageFences.size()) {
            setFailure("vkAcquireNextImageKHR returned an invalid image index");
            return ClearFrameResult::Failed;
        }
        if (profileFrame) {
            phaseStartedAtNanos = performanceNowNanos();
        }
        if (!waitForImageFence(imageFences[imageIndex], frame)) {
            return ClearFrameResult::Failed;
        }
        if (profileFrame) {
            cpuSample.fenceWaitNanos +=
                    performanceNowNanos() - phaseStartedAtNanos;
            phaseStartedAtNanos = performanceNowNanos();
        }

        std::uint32_t backgroundDynamicOffset = 0;
        std::uint32_t cellDynamicOffset = 0;
        if (!prepareFrameUniforms(frame, currentFrame, preparedFrame,
                    presentation, backgroundDynamicOffset,
                    cellDynamicOffset)) {
            return ClearFrameResult::Failed;
        }
        if (profileFrame) {
            cpuSample.uniformNanos =
                    performanceNowNanos() - phaseStartedAtNanos;
            phaseStartedAtNanos = performanceNowNanos();
        }

        result = vkResetCommandBuffer(frame.commandBuffer.get(), 0);
        if (result != VK_SUCCESS) {
            setFailure("vkResetCommandBuffer", result);
            return ClearFrameResult::Failed;
        }
        const RenderDestination destination{
                renderPass.get(),
                framebuffers[imageIndex].get(),
                surfaceExtent,
                backgroundPipeline.get(),
                VulkanPipelineTarget::Surface,
                nullptr,
        };
        if (!recordPreparedFrameCommand(frame, currentFrame,
                    backgroundDynamicOffset, cellDynamicOffset,
                    textGeometry != nullptr, destination)) {
            return ClearFrameResult::Failed;
        }
        if (profileFrame) {
            cpuSample.commandRecordNanos =
                    performanceNowNanos() - phaseStartedAtNanos;
            phaseStartedAtNanos = performanceNowNanos();
        }
        const VkFence inFlightFence = frame.inFlight.get();
        result = vkResetFences(device, 1, &inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkResetFences", result);
            return ClearFrameResult::Failed;
        }

        constexpr VkPipelineStageFlags waitStage =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSemaphore imageAvailable = frame.imageAvailable.get();
        const VkSemaphore renderFinished =
                renderFinishedSemaphores[imageIndex].get();
        const VkCommandBuffer commandBuffer = frame.commandBuffer.get();
        const VkSubmitInfo submitInfo{
                VK_STRUCTURE_TYPE_SUBMIT_INFO,
                nullptr,
                1,
                &imageAvailable,
                &waitStage,
                1,
                &commandBuffer,
                1,
                &renderFinished
        };
        result = vkQueueSubmit(
                graphicsQueue, 1, &submitInfo, inFlightFence);
        if (profileFrame) {
            cpuSample.submitNanos =
                    performanceNowNanos() - phaseStartedAtNanos;
        }
        if (result != VK_SUCCESS) {
            setFailure("vkQueueSubmit", result);
            return ClearFrameResult::Failed;
        }
        frame.submissionPending = true;
        frame.submissionSerial = nextSubmissionSerial++;
        frame.gpuTimestampPending = frame.gpuTimestampRecorded;
        frame.gpuProfileGeneration = performanceProfileGeneration;
        basicCellRenderer.markActiveGeometryUsed(frame.submissionSerial);
        if (textGeometry != nullptr) {
            basicCellRenderer.markActiveTextGeometryUsed(
                    frame.submissionSerial);
        }
        ++resourceCounters->queueSubmissionCount;
        imageFences[imageIndex] = inFlightFence;

        const VkSwapchainKHR swapchainHandle = swapchain.get();
        const VkPresentInfoKHR presentInfo{
                VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                nullptr,
                1,
                &renderFinished,
                1,
                &swapchainHandle,
                &imageIndex,
                nullptr
        };
        if (profileFrame) {
            phaseStartedAtNanos = performanceNowNanos();
        }
        result = vkQueuePresentKHR(presentQueue, &presentInfo);
        if (profileFrame) {
            cpuSample.presentNanos =
                    performanceNowNanos() - phaseStartedAtNanos;
        }
        currentFrame = (currentFrame + 1) % kFramesInFlight;
        lastFrameWasPresented = result == VK_SUCCESS
                || result == VK_SUBOPTIMAL_KHR;
        if (profileFrame && lastFrameWasPresented) {
            recordCpuPerformance(cpuSample);
        }

        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            return handleSurfaceLoss(result);
        }
        if (result != VK_SUCCESS
                && result != VK_SUBOPTIMAL_KHR
                && result != VK_ERROR_OUT_OF_DATE_KHR) {
            setFailure("vkQueuePresentKHR", result);
            return ClearFrameResult::Failed;
        }

        if (result != VK_ERROR_OUT_OF_DATE_KHR) {
            ++frameCount;
        }
        const bool mustRecreate = acquireSuboptimal
                || result == VK_SUBOPTIMAL_KHR
                || result == VK_ERROR_OUT_OF_DATE_KHR;
        if (mustRecreate) {
            if (acquireSuboptimal || result == VK_SUBOPTIMAL_KHR) {
                ++suboptimalFrameCount;
            }
            swapchainRecreatePending = true;
            lastResult = resultName(
                    result == VK_SUCCESS ? VK_SUBOPTIMAL_KHR : result);
            steadyStateScope.end();
            return createSwapchain()
                    ? ClearFrameResult::SwapchainRecreated
                    : ClearFrameResult::Failed;
        }

        status = "ready";
        lastResult = "VK_SUCCESS";
        error.clear();
        return ClearFrameResult::Presented;
    }

    [[nodiscard]] ClearFrameResult renderClear(
            const RenderViewportState& viewport,
            const std::array<float, 4>& color) {
        lastFrameWasPresented = false;
        if (!checkThread("renderClear")) {
            return ClearFrameResult::Failed;
        }
        if (status == "failed") {
            return ClearFrameResult::Failed;
        }
        if (viewport.surfaceWidth <= 0 || viewport.surfaceHeight <= 0
                || surface == VK_NULL_HANDLE || requestedWidth <= 0
                || requestedHeight <= 0) {
            return ClearFrameResult::Deferred;
        }
        if (!swapchain || swapchainRecreatePending) {
            if (!createSwapchain()) {
                return status == "waiting_for_nonzero_extent"
                        ? ClearFrameResult::Deferred
                        : ClearFrameResult::Failed;
            }
        }
        SteadyStateFrameScope steadyStateScope(*resourceCounters);
        FrameResources& frame = frames[currentFrame];
        if (!waitForFrameCompletion(frame, "vkWaitForFences(clear frame)")) {
            return ClearFrameResult::Failed;
        }

        std::uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(
                device, swapchain.get(), kAcquireTimeoutNanoseconds,
                frame.imageAvailable.get(), VK_NULL_HANDLE, &imageIndex);
        if (result == VK_TIMEOUT || result == VK_NOT_READY) {
            lastResult = resultName(result);
            return ClearFrameResult::Deferred;
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainRecreatePending = true;
            steadyStateScope.end();
            return createSwapchain()
                    ? ClearFrameResult::SwapchainRecreated
                    : ClearFrameResult::Failed;
        }
        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            return handleSurfaceLoss(result);
        }
        const bool acquireSuboptimal = result == VK_SUBOPTIMAL_KHR;
        if (result != VK_SUCCESS && !acquireSuboptimal) {
            setFailure("vkAcquireNextImageKHR(clear frame)", result);
            return ClearFrameResult::Failed;
        }
        if (imageIndex >= imageFences.size()) {
            setFailure("vkAcquireNextImageKHR(clear frame) returned an invalid image index");
            return ClearFrameResult::Failed;
        }
        if (!waitForImageFence(imageFences[imageIndex], frame)) {
            return ClearFrameResult::Failed;
        }

        result = vkResetCommandBuffer(frame.commandBuffer.get(), 0);
        if (result != VK_SUCCESS) {
            setFailure("vkResetCommandBuffer(clear frame)", result);
            return ClearFrameResult::Failed;
        }
        const VkCommandBufferBeginInfo beginInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                nullptr,
                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                nullptr
        };
        result = vkBeginCommandBuffer(frame.commandBuffer.get(), &beginInfo);
        if (result != VK_SUCCESS) {
            setFailure("vkBeginCommandBuffer(clear frame)", result);
            return ClearFrameResult::Failed;
        }
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{color[0], color[1], color[2], color[3]}};
        clearValues[1].depthStencil = {1.0F, 0};
        const VkRenderPassBeginInfo renderPassInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                nullptr,
                renderPass.get(),
                framebuffers[imageIndex].get(),
                {{0, 0}, surfaceExtent},
                static_cast<std::uint32_t>(clearValues.size()),
                clearValues.data()
        };
        vkCmdBeginRenderPass(frame.commandBuffer.get(), &renderPassInfo,
                VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(frame.commandBuffer.get());
        result = vkEndCommandBuffer(frame.commandBuffer.get());
        if (result != VK_SUCCESS) {
            setFailure("vkEndCommandBuffer(clear frame)", result);
            return ClearFrameResult::Failed;
        }
        ++resourceCounters->commandBufferRecordCount;

        const VkFence inFlightFence = frame.inFlight.get();
        result = vkResetFences(device, 1, &inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkResetFences(clear frame)", result);
            return ClearFrameResult::Failed;
        }
        constexpr VkPipelineStageFlags waitStage =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSemaphore imageAvailable = frame.imageAvailable.get();
        const VkSemaphore renderFinished =
                renderFinishedSemaphores[imageIndex].get();
        const VkCommandBuffer commandBuffer = frame.commandBuffer.get();
        const VkSubmitInfo submitInfo{
                VK_STRUCTURE_TYPE_SUBMIT_INFO,
                nullptr,
                1,
                &imageAvailable,
                &waitStage,
                1,
                &commandBuffer,
                1,
                &renderFinished
        };
        result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkQueueSubmit(clear frame)", result);
            return ClearFrameResult::Failed;
        }
        frame.submissionPending = true;
        frame.submissionSerial = nextSubmissionSerial++;
        ++resourceCounters->queueSubmissionCount;
        imageFences[imageIndex] = inFlightFence;

        const VkSwapchainKHR swapchainHandle = swapchain.get();
        const VkPresentInfoKHR presentInfo{
                VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                nullptr,
                1,
                &renderFinished,
                1,
                &swapchainHandle,
                &imageIndex,
                nullptr
        };
        result = vkQueuePresentKHR(presentQueue, &presentInfo);
        currentFrame = (currentFrame + 1) % kFramesInFlight;
        lastFrameWasPresented = result == VK_SUCCESS
                || result == VK_SUBOPTIMAL_KHR;
        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            return handleSurfaceLoss(result);
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR
                && result != VK_ERROR_OUT_OF_DATE_KHR) {
            setFailure("vkQueuePresentKHR(clear frame)", result);
            return ClearFrameResult::Failed;
        }
        if (result != VK_ERROR_OUT_OF_DATE_KHR) {
            ++frameCount;
        }
        const bool mustRecreate = acquireSuboptimal
                || result == VK_SUBOPTIMAL_KHR
                || result == VK_ERROR_OUT_OF_DATE_KHR;
        if (mustRecreate) {
            if (acquireSuboptimal || result == VK_SUBOPTIMAL_KHR) {
                ++suboptimalFrameCount;
            }
            swapchainRecreatePending = true;
            lastResult = resultName(
                    result == VK_SUCCESS ? VK_SUBOPTIMAL_KHR : result);
            steadyStateScope.end();
            return createSwapchain()
                    ? ClearFrameResult::SwapchainRecreated
                    : ClearFrameResult::Failed;
        }
        status = "ready";
        lastResult = "VK_SUCCESS";
        error.clear();
        return ClearFrameResult::Presented;
    }

    [[nodiscard]] ClearFrameResult renderEnvironmentFieldPreview(
            const EnvironmentFieldPreviewCommand& command) {
        lastFrameWasPresented = false;
        if (!checkThread("renderEnvironmentFieldPreview")) {
            return ClearFrameResult::Failed;
        }
        if (status == "failed") {
            return ClearFrameResult::Failed;
        }
        const bool matrixFinite = std::all_of(command.matrix.begin(),
                command.matrix.end(), [](float value) {
                    return std::isfinite(value);
                });
        const bool scalarValuesFinite = std::isfinite(command.dishRadius)
                && std::isfinite(command.fieldBase)
                && std::isfinite(command.fieldStrength)
                && std::isfinite(command.fieldAngle)
                && std::isfinite(command.fieldOffset)
                && std::isfinite(command.fieldSoftness)
                && std::isfinite(command.displayMin)
                && std::isfinite(command.displayMax)
                && std::isfinite(command.gravityFieldAngle);
        const bool typeValid = command.vectorPreview
                ? command.gravityFieldType >= 0
                        && command.gravityFieldType <= 2
                : command.fieldType >= 0 && command.fieldType <= 2;
        if (!matrixFinite || !scalarValuesFinite || !typeValid
                || command.dishRadius <= 0.0
                || command.surfaceWidth <= 0
                || command.surfaceHeight <= 0) {
            setFailure("Environment Field Preview command is invalid");
            return ClearFrameResult::Failed;
        }
        if (surface == VK_NULL_HANDLE || requestedWidth <= 0
                || requestedHeight <= 0) {
            return ClearFrameResult::Deferred;
        }
        if (!swapchain || swapchainRecreatePending) {
            if (!createSwapchain()) {
                return status == "waiting_for_nonzero_extent"
                        ? ClearFrameResult::Deferred
                        : ClearFrameResult::Failed;
            }
        }
        // Keep the preview-only pipeline out of ordinary microscope and
        // Genome Preview swapchain creation. Subsequent field updates reuse it.
        if (!densityFieldPipeline && !createDensityFieldPipeline()) {
            return ClearFrameResult::Failed;
        }

        SteadyStateFrameScope steadyStateScope(*resourceCounters);
        FrameResources& frame = frames[currentFrame];
        if (!waitForFrameCompletion(
                    frame, "vkWaitForFences(Environment Field Preview)")) {
            return ClearFrameResult::Failed;
        }

        std::uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(
                device, swapchain.get(), kAcquireTimeoutNanoseconds,
                frame.imageAvailable.get(), VK_NULL_HANDLE, &imageIndex);
        if (result == VK_TIMEOUT || result == VK_NOT_READY) {
            lastResult = resultName(result);
            return ClearFrameResult::Deferred;
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainRecreatePending = true;
            steadyStateScope.end();
            return createSwapchain()
                    ? ClearFrameResult::SwapchainRecreated
                    : ClearFrameResult::Failed;
        }
        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            return handleSurfaceLoss(result);
        }
        const bool acquireSuboptimal = result == VK_SUBOPTIMAL_KHR;
        if (result != VK_SUCCESS && !acquireSuboptimal) {
            setFailure("vkAcquireNextImageKHR(Environment Field Preview)",
                    result);
            return ClearFrameResult::Failed;
        }
        if (imageIndex >= imageFences.size()) {
            setFailure("vkAcquireNextImageKHR(Environment Field Preview) returned an invalid image index");
            return ClearFrameResult::Failed;
        }
        if (!waitForImageFence(imageFences[imageIndex], frame)) {
            return ClearFrameResult::Failed;
        }

        frame.deferredReleases.releaseAll();
        const VkDeviceSize uniformAlignment = std::max<VkDeviceSize>(
                physicalDeviceProperties.limits
                        .minUniformBufferOffsetAlignment,
                alignof(EnvironmentFieldUniformData));
        const VkDeviceSize requiredCapacity = sizeof(BackgroundUniformData)
                + uniformAlignment - 1
                + sizeof(EnvironmentFieldUniformData);
        bool uploadBufferChanged = false;
        result = frame.uploadArena.beginFrame(requiredCapacity,
                frame.deferredReleases, uploadBufferChanged);
        if (result != VK_SUCCESS) {
            setFailure("grow Environment Field Preview upload arena", result);
            return ClearFrameResult::Failed;
        }
        if (uploadBufferChanged) {
            updateBackgroundDescriptor(frame);
            basicCellRenderer.updateFrameDescriptor(
                    currentFrame, frame.uploadArena.buffer());
        }

        const BackgroundUniformData background =
                environmentPreviewBackgroundUniformData(command);
        const auto backgroundSlice = frame.uploadArena.write(
                &background, sizeof(background), uniformAlignment, result);
        if (!backgroundSlice.has_value()) {
            setFailure("write Environment Field Preview background uniform",
                    result);
            return ClearFrameResult::Failed;
        }
        const EnvironmentFieldUniformData field =
                environmentFieldUniformData(command);
        const auto fieldSlice = frame.uploadArena.write(
                &field, sizeof(field), uniformAlignment, result);
        if (!fieldSlice.has_value()) {
            setFailure("write Environment Field Preview field uniform",
                    result);
            return ClearFrameResult::Failed;
        }
        if (backgroundSlice->offset
                        > std::numeric_limits<std::uint32_t>::max()
                || fieldSlice->offset
                        > std::numeric_limits<std::uint32_t>::max()) {
            setFailure("Environment Field Preview dynamic uniform offset exceeds Vulkan 1.0 limits");
            return ClearFrameResult::Failed;
        }
        const std::uint32_t backgroundDynamicOffset =
                static_cast<std::uint32_t>(backgroundSlice->offset);
        const std::uint32_t fieldDynamicOffset =
                static_cast<std::uint32_t>(fieldSlice->offset);

        result = vkResetCommandBuffer(frame.commandBuffer.get(), 0);
        if (result != VK_SUCCESS) {
            setFailure("vkResetCommandBuffer(Environment Field Preview)",
                    result);
            return ClearFrameResult::Failed;
        }
        const VkCommandBufferBeginInfo beginInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                nullptr,
                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                nullptr,
        };
        result = vkBeginCommandBuffer(frame.commandBuffer.get(), &beginInfo);
        if (result != VK_SUCCESS) {
            setFailure("vkBeginCommandBuffer(Environment Field Preview)",
                    result);
            return ClearFrameResult::Failed;
        }
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{
                26.0F / 255.0F,
                28.0F / 255.0F,
                31.0F / 255.0F,
                1.0F,
        }};
        clearValues[1].depthStencil = {1.0F, 0};
        const VkRenderPassBeginInfo renderPassInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                nullptr,
                renderPass.get(),
                framebuffers[imageIndex].get(),
                {{0, 0}, surfaceExtent},
                static_cast<std::uint32_t>(clearValues.size()),
                clearValues.data(),
        };
        const VkCommandBuffer commandBuffer = frame.commandBuffer.get();
        vkCmdBeginRenderPass(
                commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{
                0.0F,
                0.0F,
                static_cast<float>(surfaceExtent.width),
                static_cast<float>(surfaceExtent.height),
                0.0F,
                1.0F,
        };
        const VkRect2D scissor{{0, 0}, surfaceExtent};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        const VkDescriptorSet descriptorSet =
                frame.backgroundDescriptorSet.get();
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                backgroundPipeline.get());
        ++resourceCounters->descriptorBindCount;
        vkCmdBindDescriptorSets(commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                backgroundPipelineLayout.get(), 0, 1, &descriptorSet,
                1, &backgroundDynamicOffset);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                densityFieldPipeline.get());
        ++resourceCounters->descriptorBindCount;
        vkCmdBindDescriptorSets(commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                backgroundPipelineLayout.get(), 0, 1, &descriptorSet,
                1, &fieldDynamicOffset);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
        result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) {
            setFailure("vkEndCommandBuffer(Environment Field Preview)",
                    result);
            return ClearFrameResult::Failed;
        }
        ++resourceCounters->commandBufferRecordCount;

        const VkFence inFlightFence = frame.inFlight.get();
        result = vkResetFences(device, 1, &inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkResetFences(Environment Field Preview)", result);
            return ClearFrameResult::Failed;
        }
        constexpr VkPipelineStageFlags waitStage =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSemaphore imageAvailable = frame.imageAvailable.get();
        const VkSemaphore renderFinished =
                renderFinishedSemaphores[imageIndex].get();
        const VkSubmitInfo submitInfo{
                VK_STRUCTURE_TYPE_SUBMIT_INFO,
                nullptr,
                1,
                &imageAvailable,
                &waitStage,
                1,
                &commandBuffer,
                1,
                &renderFinished,
        };
        result = vkQueueSubmit(
                graphicsQueue, 1, &submitInfo, inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkQueueSubmit(Environment Field Preview)", result);
            return ClearFrameResult::Failed;
        }
        frame.submissionPending = true;
        frame.submissionSerial = nextSubmissionSerial++;
        ++resourceCounters->queueSubmissionCount;
        imageFences[imageIndex] = inFlightFence;

        const VkSwapchainKHR swapchainHandle = swapchain.get();
        const VkPresentInfoKHR presentInfo{
                VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                nullptr,
                1,
                &renderFinished,
                1,
                &swapchainHandle,
                &imageIndex,
                nullptr,
        };
        result = vkQueuePresentKHR(presentQueue, &presentInfo);
        currentFrame = (currentFrame + 1) % kFramesInFlight;
        lastFrameWasPresented = result == VK_SUCCESS
                || result == VK_SUBOPTIMAL_KHR;
        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            return handleSurfaceLoss(result);
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR
                && result != VK_ERROR_OUT_OF_DATE_KHR) {
            setFailure("vkQueuePresentKHR(Environment Field Preview)",
                    result);
            return ClearFrameResult::Failed;
        }
        if (result != VK_ERROR_OUT_OF_DATE_KHR) {
            ++frameCount;
        }
        const bool mustRecreate = acquireSuboptimal
                || result == VK_SUBOPTIMAL_KHR
                || result == VK_ERROR_OUT_OF_DATE_KHR;
        if (mustRecreate) {
            if (acquireSuboptimal || result == VK_SUBOPTIMAL_KHR) {
                ++suboptimalFrameCount;
            }
            swapchainRecreatePending = true;
            lastResult = resultName(
                    result == VK_SUCCESS ? VK_SUBOPTIMAL_KHR : result);
            steadyStateScope.end();
            return createSwapchain()
                    ? ClearFrameResult::SwapchainRecreated
                    : ClearFrameResult::Failed;
        }
        status = "ready";
        lastResult = "VK_SUCCESS";
        error.clear();
        return ClearFrameResult::Presented;
    }

    void beginOffscreenRgba(
            OffscreenRenderTarget target, int width, int height) {
        if (!checkThread("beginOffscreenRgba")) {
            throw std::logic_error(error);
        }
        if (status == "failed") {
            throw std::runtime_error(error.empty()
                            ? "Vulkan session is unavailable"
                            : error);
        }
        if (activeOffscreenTarget.has_value()) {
            throw std::logic_error(
                    "An off-screen Vulkan capture is already active");
        }
        if (pendingGenomeFrame.active) {
            throw std::logic_error(
                    "An interactive Genome Preview frame is still active");
        }
        if (device == VK_NULL_HANDLE || !deviceContext) {
            throw std::logic_error(
                    "Off-screen Vulkan capture requires an initialized render session");
        }
        if (!ensureOffscreenTarget(target, width, height)) {
            throw std::runtime_error(error.empty()
                            ? "Could not create the off-screen Vulkan target"
                            : error);
        }
        activeOffscreenTarget = target;
        offscreenTarget(target).rendered = false;
        lastFrameWasPresented = false;
    }

    [[nodiscard]] std::vector<std::uint8_t> finishOffscreenRgba(
            OffscreenRenderTarget target) {
        if (!checkThread("finishOffscreenRgba")) {
            throw std::logic_error(error);
        }
        if (!offscreenCaptureActive(target)) {
            throw std::logic_error(
                    "Off-screen Vulkan target is not active");
        }
        OffscreenTargetResources& resources = offscreenTarget(target);
        if (!resources.rendered) {
            throw std::logic_error(
                    "Off-screen Vulkan target has no completed frame");
        }
        const VkDeviceSize mappedBytes = resources.rowPitch
                * static_cast<VkDeviceSize>(resources.height);
        const VkResult result = resources.readbackBuffer.invalidate(
                0, mappedBytes);
        if (result != VK_SUCCESS) {
            setFailure("vkInvalidateMappedMemoryRanges(off-screen readback)",
                    result);
            throw std::runtime_error(error);
        }
        const auto* mapped = static_cast<const std::uint8_t*>(
                resources.readbackBuffer.mappedData());
        if (mapped == nullptr) {
            setFailure("Off-screen Vulkan readback buffer is not mapped");
            throw std::runtime_error(error);
        }

        const std::size_t packedRowBytes =
                static_cast<std::size_t>(resources.width)
                * static_cast<std::size_t>(kRgbaChannelCount);
        const std::size_t height =
                static_cast<std::size_t>(resources.height);
        if (packedRowBytes > std::numeric_limits<std::size_t>::max()
                        / height) {
            setFailure("Off-screen Vulkan output size overflows host memory");
            throw std::length_error(error);
        }
        std::vector<std::uint8_t> rgba(packedRowBytes * height);
        for (std::size_t row = 0; row < height; ++row) {
            std::memcpy(rgba.data() + row * packedRowBytes,
                    mapped + row * static_cast<std::size_t>(resources.rowPitch),
                    packedRowBytes);
        }

        resources.rendered = false;
        activeOffscreenTarget.reset();
        ++offscreenCaptureCount;
        offscreenReadbackBytes += rgba.size();
        return rgba;
    }

    void cancelOffscreenRgba(OffscreenRenderTarget target) noexcept {
        if (!offscreenCaptureActive(target)) {
            return;
        }
        offscreenTarget(target).rendered = false;
        activeOffscreenTarget.reset();
        if (target == OffscreenRenderTarget::GenomePreview) {
            pendingGenomeFrame.reset();
        }
    }

    void beginGenomePreviewFrame(
            int width, int height, bool fullyTransparentClear) {
        pendingGenomeFrame.reset();
        pendingGenomeFrame.active = true;
        pendingGenomeFrame.width = width;
        pendingGenomeFrame.height = height;
        pendingGenomeFrame.fullyTransparentClear = fullyTransparentClear;
        lastFrameWasPresented = false;
        pendingGenomeFrame.outcome = ClearFrameResult::Deferred;

        if (!checkThread("beginGenomePreviewFrame")) {
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        if (status == "failed") {
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        if (width <= 0 || height <= 0) {
            setFailure("Genome Preview surface dimensions must be positive");
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        if (activeOffscreenTarget.has_value()) {
            if (!offscreenCaptureActive(
                        OffscreenRenderTarget::GenomePreview)) {
                setFailure("Genome Preview cannot render into the active Substrate Preview target");
                pendingGenomeFrame.outcome = ClearFrameResult::Failed;
                return;
            }
            if (!offscreenTarget(OffscreenRenderTarget::GenomePreview)
                            .matches(width, height)) {
                setFailure("Genome Preview dimensions do not match its off-screen target");
                pendingGenomeFrame.outcome = ClearFrameResult::Failed;
                return;
            }
        } else {
            if (surface == VK_NULL_HANDLE || requestedWidth <= 0
                    || requestedHeight <= 0) {
                return;
            }
            if ((!swapchain || swapchainRecreatePending)
                    && !createSwapchain()) {
                pendingGenomeFrame.outcome = status == "waiting_for_nonzero_extent"
                        ? ClearFrameResult::Deferred
                        : ClearFrameResult::Failed;
                return;
            }
        }
        FrameResources& frame = frames[currentFrame];
        if (!waitForFrameCompletion(
                    frame, "vkWaitForFences(Genome Preview frame)")) {
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        pendingGenomeFrame.ready = true;
    }

    void drawGenomePreviewGrid(const GenomePreviewGridCommand& command) {
        if (!pendingGenomeFrame.active || !pendingGenomeFrame.ready) {
            return;
        }
        if (!std::isfinite(command.worldRadius)
                || command.worldRadius <= 0.0 || command.gridSize <= 0) {
            setFailure("Genome Preview grid parameters are invalid");
            pendingGenomeFrame.ready = false;
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        pendingGenomeFrame.grid = command;
    }

    void appendGenomePreviewLayer(
            const RenderPresentationState& presentation,
            const PreparedRenderFrameView& preparedFrame,
            const GenomePreviewDrawOptions& options,
            const std::array<float, 16>* treeMatrix,
            const PreparedFrameSpan<float>* treeVertices) {
        if (!pendingGenomeFrame.active || !pendingGenomeFrame.ready) {
            return;
        }
        if (pendingGenomeFrame.layers.size() >= 2) {
            setFailure("Genome Preview submitted too many cell layers");
            pendingGenomeFrame.ready = false;
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        if (treeVertices != nullptr
                && (treeVertices->size() % 2 != 0
                        || treeVertices->size() / 2
                                > std::numeric_limits<std::uint32_t>::max())) {
            setFailure("Genome Preview tree geometry is invalid");
            pendingGenomeFrame.ready = false;
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        std::uint64_t reservedGeometrySlotMask = 0;
        for (const PendingGenomeLayer& layer : pendingGenomeFrame.layers) {
            if (layer.geometrySlot
                    >= std::numeric_limits<std::uint64_t>::digits) {
                setFailure("Genome Preview geometry slot index is invalid");
                pendingGenomeFrame.ready = false;
                pendingGenomeFrame.outcome = ClearFrameResult::Failed;
                return;
            }
            reservedGeometrySlotMask |=
                    std::uint64_t{1} << layer.geometrySlot;
        }
        while (const auto requiredSerial =
                        basicCellRenderer.requiredGeometryWaitSerial(
                                completedSubmissionSerial,
                                reservedGeometrySlotMask)) {
            if (!waitForSubmissionSerial(*requiredSerial)) {
                pendingGenomeFrame.ready = false;
                pendingGenomeFrame.outcome = ClearFrameResult::Failed;
                return;
            }
        }
        std::string geometryError;
        std::size_t geometrySlot = 0;
        if (!basicCellRenderer.uploadGeometry(preparedFrame,
                    completedSubmissionSerial, geometryError,
                    &geometrySlot, reservedGeometrySlotMask)) {
            setFailure(std::move(geometryError));
            pendingGenomeFrame.ready = false;
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        if (geometrySlot >= std::numeric_limits<std::uint64_t>::digits
                || (reservedGeometrySlotMask
                           & (std::uint64_t{1} << geometrySlot))
                        != 0) {
            setFailure("Genome Preview reused geometry within one frame");
            pendingGenomeFrame.ready = false;
            pendingGenomeFrame.outcome = ClearFrameResult::Failed;
            return;
        }
        PendingGenomeLayer layer;
        layer.geometrySlot = geometrySlot;
        layer.presentation = presentation;
        layer.options = options;
        if (treeMatrix != nullptr && treeVertices != nullptr) {
            layer.tree = true;
            layer.treeMatrix = *treeMatrix;
            layer.treeVertices.assign(treeVertices->data(),
                    treeVertices->data() + treeVertices->size());
        }
        pendingGenomeFrame.layers.push_back(std::move(layer));
    }

    void drawGenomePreviewTree(
            const RenderPresentationState& presentation,
            const PreparedRenderFrameView& preparedFrame,
            const std::array<float, 16>& matrix,
            PreparedFrameSpan<float> lineVertices,
            const GenomePreviewDrawOptions& options) {
        appendGenomePreviewLayer(presentation, preparedFrame, options,
                &matrix, &lineVertices);
    }

    void drawGenomePreview(
            const RenderPresentationState& presentation,
            const PreparedRenderFrameView& preparedFrame,
            const GenomePreviewDrawOptions& options) {
        appendGenomePreviewLayer(presentation, preparedFrame, options,
                nullptr, nullptr);
    }

    [[nodiscard]] ClearFrameResult finishGenomePreviewFrame(
            ClearFrameResult result) {
        pendingGenomeFrame.reset();
        return result;
    }

    [[nodiscard]] ClearFrameResult endGenomePreviewFrame() {
        if (!checkThread("endGenomePreviewFrame")) {
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        if (!pendingGenomeFrame.active) {
            setFailure("Genome Preview frame completion has no matching begin");
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        if (!pendingGenomeFrame.ready) {
            return finishGenomePreviewFrame(pendingGenomeFrame.outcome);
        }
        if (pendingGenomeFrame.layers.empty()) {
            setFailure("Genome Preview frame contains no cell layer");
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }

        const bool offscreen = offscreenCaptureActive(
                OffscreenRenderTarget::GenomePreview);
        OffscreenTargetResources* offscreenTargetResources = offscreen
                ? &offscreenTarget(OffscreenRenderTarget::GenomePreview)
                : nullptr;
        if (offscreenTargetResources != nullptr) {
            offscreenTargetResources->rendered = false;
        }
        std::optional<SteadyStateFrameScope> steadyStateScope;
        if (!offscreen) {
            steadyStateScope.emplace(*resourceCounters);
        }
        FrameResources& frame = frames[currentFrame];
        std::uint32_t imageIndex = 0;
        VkResult result = VK_SUCCESS;
        bool acquireSuboptimal = false;
        if (!offscreen) {
            result = vkAcquireNextImageKHR(device, swapchain.get(),
                    kAcquireTimeoutNanoseconds, frame.imageAvailable.get(),
                    VK_NULL_HANDLE, &imageIndex);
            if (result == VK_TIMEOUT || result == VK_NOT_READY) {
                lastResult = resultName(result);
                return finishGenomePreviewFrame(
                        ClearFrameResult::Deferred);
            }
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                swapchainRecreatePending = true;
                steadyStateScope->end();
                return finishGenomePreviewFrame(createSwapchain()
                                ? ClearFrameResult::SwapchainRecreated
                                : ClearFrameResult::Failed);
            }
            if (result == VK_ERROR_SURFACE_LOST_KHR) {
                return finishGenomePreviewFrame(
                        handleSurfaceLoss(result));
            }
            acquireSuboptimal = result == VK_SUBOPTIMAL_KHR;
            if (result != VK_SUCCESS && !acquireSuboptimal) {
                setFailure("vkAcquireNextImageKHR(Genome Preview)",
                        result);
                return finishGenomePreviewFrame(
                        ClearFrameResult::Failed);
            }
            if (imageIndex >= imageFences.size()) {
                setFailure("vkAcquireNextImageKHR(Genome Preview) returned an invalid image index");
                return finishGenomePreviewFrame(
                        ClearFrameResult::Failed);
            }
            if (!waitForImageFence(imageFences[imageIndex], frame)) {
                return finishGenomePreviewFrame(
                        ClearFrameResult::Failed);
            }
        }

        struct RecordedGenomeLayer final {
            std::uint32_t cellUniformOffset = 0;
            std::uint32_t treeUniformOffset = 0;
            std::optional<VulkanUploadSlice> treeVertices;
        };

        std::vector<RecordedGenomeLayer> recordedLayers(
                pendingGenomeFrame.layers.size());
        std::optional<VulkanUploadSlice> gridVertices;
        std::uint32_t gridUniformOffset = 0;

        const VkDeviceSize uniformAlignment = std::max<VkDeviceSize>(
                physicalDeviceProperties.limits.minUniformBufferOffsetAlignment,
                alignof(CellUniformData));
        std::size_t uniformCount = pendingGenomeFrame.layers.size();
        std::uint64_t vertexBytes = 0;
        if (pendingGenomeFrame.grid.has_value()) {
            ++uniformCount;
            vertexBytes += 24U * sizeof(float);
        }
        for (const PendingGenomeLayer& layer : pendingGenomeFrame.layers) {
            if (layer.tree) {
                ++uniformCount;
                vertexBytes += layer.treeVertices.size() * sizeof(float);
            }
        }
        const std::uint64_t maximumUniformBytes =
                static_cast<std::uint64_t>(uniformCount)
                * (static_cast<std::uint64_t>(uniformAlignment) - 1U
                        + sizeof(CellUniformData));
        if (maximumUniformBytes
                > std::numeric_limits<VkDeviceSize>::max()
                        - vertexBytes) {
            setFailure("Genome Preview transient upload size overflows");
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        frame.deferredReleases.releaseAll();
        bool uploadBufferChanged = false;
        result = frame.uploadArena.beginFrame(
                static_cast<VkDeviceSize>(maximumUniformBytes + vertexBytes),
                frame.deferredReleases, uploadBufferChanged);
        if (result != VK_SUCCESS) {
            setFailure("grow Genome Preview upload arena", result);
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        if (uploadBufferChanged) {
            updateBackgroundDescriptor(frame);
            basicCellRenderer.updateFrameDescriptor(
                    currentFrame, frame.uploadArena.buffer());
        }

        const auto writeUniform = [&](const std::array<float, 16>& matrix,
                                          std::uint32_t& offset) {
            RenderPresentationState presentation;
            presentation.viewport.mvpMatrix = matrix;
            const CellUniformData uniform = cellUniformData(presentation);
            VkResult writeResult = VK_SUCCESS;
            const auto slice = frame.uploadArena.write(&uniform,
                    sizeof(uniform), uniformAlignment, writeResult);
            if (!slice.has_value()
                    || slice->offset
                            > std::numeric_limits<std::uint32_t>::max()) {
                setFailure("write Genome Preview uniform data", writeResult);
                return false;
            }
            offset = static_cast<std::uint32_t>(slice->offset);
            return true;
        };

        if (pendingGenomeFrame.grid.has_value()) {
            const GenomePreviewGridCommand& grid = *pendingGenomeFrame.grid;
            if (!writeUniform(grid.matrix, gridUniformOffset)) {
                return finishGenomePreviewFrame(ClearFrameResult::Failed);
            }
            const float worldRadius = static_cast<float>(grid.worldRadius);
            const float gridSize = static_cast<float>(grid.gridSize);
            const std::array<float, 24> vertices = {
                    grid.maxX,
                    grid.maxY,
                    worldRadius,
                    gridSize,
                    grid.maxX,
                    grid.minY,
                    worldRadius,
                    gridSize,
                    grid.minX,
                    grid.minY,
                    worldRadius,
                    gridSize,
                    grid.minX,
                    grid.minY,
                    worldRadius,
                    gridSize,
                    grid.minX,
                    grid.maxY,
                    worldRadius,
                    gridSize,
                    grid.maxX,
                    grid.maxY,
                    worldRadius,
                    gridSize,
            };
            VkResult writeResult = VK_SUCCESS;
            gridVertices = frame.uploadArena.write(vertices.data(),
                    sizeof(vertices), alignof(float), writeResult);
            if (!gridVertices.has_value()) {
                setFailure("write Genome Preview grid vertices", writeResult);
                return finishGenomePreviewFrame(ClearFrameResult::Failed);
            }
        }

        for (std::size_t index = 0;
                index < pendingGenomeFrame.layers.size(); ++index) {
            const PendingGenomeLayer& layer =
                    pendingGenomeFrame.layers[index];
            RecordedGenomeLayer& recorded = recordedLayers[index];
            if (!writeUniform(layer.presentation.viewport.mvpMatrix,
                        recorded.cellUniformOffset)) {
                return finishGenomePreviewFrame(ClearFrameResult::Failed);
            }
            if (layer.tree) {
                if (!writeUniform(
                            layer.treeMatrix, recorded.treeUniformOffset)) {
                    return finishGenomePreviewFrame(
                            ClearFrameResult::Failed);
                }
                if (!layer.treeVertices.empty()) {
                    VkResult writeResult = VK_SUCCESS;
                    recorded.treeVertices = frame.uploadArena.write(
                            layer.treeVertices.data(),
                            layer.treeVertices.size() * sizeof(float),
                            alignof(float), writeResult);
                    if (!recorded.treeVertices.has_value()) {
                        setFailure("write Genome Preview tree vertices",
                                writeResult);
                        return finishGenomePreviewFrame(
                                ClearFrameResult::Failed);
                    }
                }
            }
        }

        result = vkResetCommandBuffer(frame.commandBuffer.get(), 0);
        if (result != VK_SUCCESS) {
            setFailure("vkResetCommandBuffer(Genome Preview)", result);
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        const VkCommandBufferBeginInfo beginInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                nullptr,
                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                nullptr,
        };
        result = vkBeginCommandBuffer(frame.commandBuffer.get(), &beginInfo);
        if (result != VK_SUCCESS) {
            setFailure("vkBeginCommandBuffer(Genome Preview)", result);
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        const float clearRgb = pendingGenomeFrame.fullyTransparentClear
                ? 0.0F
                : 32.0F / 255.0F;
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{clearRgb, clearRgb, clearRgb, 0.0F}};
        clearValues[1].depthStencil = {1.0F, 0};
        const VkExtent2D targetExtent = offscreen
                ? VkExtent2D{
                          static_cast<std::uint32_t>(
                                  offscreenTargetResources->width),
                          static_cast<std::uint32_t>(
                                  offscreenTargetResources->height),
                  }
                : surfaceExtent;
        const VkExtent2D presentationExtent = offscreen
                ? targetExtent
                : identityExtent(
                          VkExtent2D{
                                  static_cast<std::uint32_t>(
                                          pendingGenomeFrame.width),
                                  static_cast<std::uint32_t>(
                                          pendingGenomeFrame.height),
                          },
                          surfaceTransform);
        if (presentationExtent.width > targetExtent.width
                || presentationExtent.height > targetExtent.height) {
            setFailure("Genome Preview viewport exceeds its backing Surface");
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        const VkOffset2D presentationOffset = offscreen
                ? VkOffset2D{0, 0}
                : topLeftIdentityOffset(targetExtent, presentationExtent,
                          surfaceTransform);
        const VkRenderPass targetRenderPass = offscreen
                ? offscreenRenderPass.get()
                : renderPass.get();
        const VkFramebuffer targetFramebuffer = offscreen
                ? offscreenTargetResources->framebuffer.get()
                : framebuffers[imageIndex].get();
        const VulkanPipelineTarget pipelineTarget = offscreen
                ? VulkanPipelineTarget::Offscreen
                : VulkanPipelineTarget::Surface;
        const VkRenderPassBeginInfo renderPassInfo{
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                nullptr,
                targetRenderPass,
                targetFramebuffer,
                {{0, 0}, targetExtent},
                static_cast<std::uint32_t>(clearValues.size()),
                clearValues.data(),
        };
        const VkCommandBuffer commandBuffer = frame.commandBuffer.get();
        vkCmdBeginRenderPass(
                commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{
                static_cast<float>(presentationOffset.x),
                static_cast<float>(presentationOffset.y),
                static_cast<float>(presentationExtent.width),
                static_cast<float>(presentationExtent.height),
                0.0F,
                1.0F,
        };
        const VkRect2D scissor{presentationOffset, presentationExtent};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        if (pendingGenomeFrame.grid.has_value()) {
            basicCellRenderer.recordGenomeGrid(commandBuffer, currentFrame,
                    gridUniformOffset, gridVertices->buffer,
                    gridVertices->offset, pipelineTarget);
        }
        for (std::size_t index = 0;
                index < pendingGenomeFrame.layers.size(); ++index) {
            const PendingGenomeLayer& layer =
                    pendingGenomeFrame.layers[index];
            const RecordedGenomeLayer& recorded = recordedLayers[index];
            if (layer.tree && recorded.treeVertices.has_value()) {
                basicCellRenderer.recordGenomeTreeLines(commandBuffer,
                        currentFrame, recorded.treeUniformOffset,
                        recorded.treeVertices->buffer,
                        recorded.treeVertices->offset,
                        static_cast<std::uint32_t>(
                                layer.treeVertices.size() / 2),
                        pipelineTarget);
            }
            VulkanBasicCellDrawOptions drawOptions;
            drawOptions.previewOrder = true;
            // GLES replaces basic-cell RGB in Genome Preview. Only its
            // overlays use source-alpha compositing.
            drawOptions.blendBasicCells = false;
            drawOptions.drawDirectionArrow =
                    layer.options.drawDirectionArrow;
            drawOptions.drawMarkers = layer.options.drawMarkers;
            drawOptions.drawLinks = layer.options.drawLinks;
            drawOptions.drawFood = false;
            basicCellRenderer.recordGeometry(commandBuffer, currentFrame,
                    recorded.cellUniformOffset, layer.geometrySlot,
                    drawOptions, pipelineTarget);
        }
        vkCmdEndRenderPass(commandBuffer);
        if (offscreen) {
            VkBufferImageCopy copy{};
            copy.bufferRowLength = static_cast<std::uint32_t>(
                    offscreenTargetResources->rowPitch
                    / kRgbaChannelCount);
            copy.bufferImageHeight = static_cast<std::uint32_t>(
                    offscreenTargetResources->height);
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {
                    static_cast<std::uint32_t>(
                            offscreenTargetResources->width),
                    static_cast<std::uint32_t>(
                            offscreenTargetResources->height),
                    1,
            };
            vkCmdCopyImageToBuffer(commandBuffer,
                    offscreenTargetResources->colorImage.get(),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    offscreenTargetResources->readbackBuffer.get(),
                    1, &copy);
        }
        result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) {
            setFailure("vkEndCommandBuffer(Genome Preview)", result);
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        ++resourceCounters->commandBufferRecordCount;

        const VkFence inFlightFence = frame.inFlight.get();
        result = vkResetFences(device, 1, &inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkResetFences(Genome Preview)", result);
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        constexpr VkPipelineStageFlags waitStage =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSemaphore imageAvailable = frame.imageAvailable.get();
        const VkSemaphore renderFinished = offscreen
                ? VK_NULL_HANDLE
                : renderFinishedSemaphores[imageIndex].get();
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        if (!offscreen) {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &imageAvailable;
            submitInfo.pWaitDstStageMask = &waitStage;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &renderFinished;
        }
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        result = vkQueueSubmit(
                graphicsQueue, 1, &submitInfo, inFlightFence);
        if (result != VK_SUCCESS) {
            setFailure("vkQueueSubmit(Genome Preview)", result);
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        frame.submissionPending = true;
        frame.submissionSerial = nextSubmissionSerial++;
        for (const PendingGenomeLayer& layer : pendingGenomeFrame.layers) {
            basicCellRenderer.markGeometrySlotUsed(
                    layer.geometrySlot, frame.submissionSerial);
        }
        ++resourceCounters->queueSubmissionCount;
        if (offscreen) {
            if (!waitForFrameCompletion(frame,
                        "vkWaitForFences(off-screen Genome Preview readback)")) {
                return finishGenomePreviewFrame(
                        ClearFrameResult::Failed);
            }
            offscreenTargetResources->rendered = true;
            status = "ready";
            lastResult = "VK_SUCCESS";
            error.clear();
            return finishGenomePreviewFrame(
                    ClearFrameResult::Presented);
        }
        imageFences[imageIndex] = inFlightFence;

        const VkSwapchainKHR swapchainHandle = swapchain.get();
        const VkPresentInfoKHR presentInfo{
                VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                nullptr,
                1,
                &renderFinished,
                1,
                &swapchainHandle,
                &imageIndex,
                nullptr,
        };
        result = vkQueuePresentKHR(presentQueue, &presentInfo);
        currentFrame = (currentFrame + 1) % kFramesInFlight;
        lastFrameWasPresented = result == VK_SUCCESS
                || result == VK_SUBOPTIMAL_KHR;
        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            return finishGenomePreviewFrame(handleSurfaceLoss(result));
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR
                && result != VK_ERROR_OUT_OF_DATE_KHR) {
            setFailure("vkQueuePresentKHR(Genome Preview)", result);
            return finishGenomePreviewFrame(ClearFrameResult::Failed);
        }
        if (result != VK_ERROR_OUT_OF_DATE_KHR) {
            ++frameCount;
        }
        const bool mustRecreate = acquireSuboptimal
                || result == VK_SUBOPTIMAL_KHR
                || result == VK_ERROR_OUT_OF_DATE_KHR;
        if (mustRecreate) {
            if (acquireSuboptimal || result == VK_SUBOPTIMAL_KHR) {
                ++suboptimalFrameCount;
            }
            swapchainRecreatePending = true;
            lastResult = resultName(
                    result == VK_SUCCESS ? VK_SUBOPTIMAL_KHR : result);
            steadyStateScope->end();
            return finishGenomePreviewFrame(createSwapchain()
                            ? ClearFrameResult::SwapchainRecreated
                            : ClearFrameResult::Failed);
        }
        status = "ready";
        lastResult = "VK_SUCCESS";
        error.clear();
        return finishGenomePreviewFrame(ClearFrameResult::Presented);
    }

    void cancelGenomePreviewFrame() noexcept {
        pendingGenomeFrame.reset();
    }

    void resizeSurface(int width, int height) {
        if (!checkThread("resizeSurface")) {
            return;
        }
        const int nextWidth = std::max(width, 0);
        const int nextHeight = std::max(height, 0);
        if (nextWidth == requestedWidth && nextHeight == requestedHeight
                && ((nextWidth == 0 || nextHeight == 0)
                        || (swapchain && !swapchainRecreatePending))) {
            return;
        }
        requestedWidth = nextWidth;
        requestedHeight = nextHeight;
        if (requestedWidth == 0 || requestedHeight == 0) {
            destroySwapchain();
            swapchainRecreatePending = true;
            status = "waiting_for_nonzero_extent";
            lastResult = "VK_SUCCESS";
            error.clear();
            return;
        }
        if (surface != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            swapchainRecreatePending = true;
            (void)createSwapchain();
        }
    }

    [[nodiscard]] bool attachWindow(
            ANativeWindow* newWindow, int width, int height) {
        NativeWindowReference incomingWindow(newWindow);
        if (!checkThread("attachWindow")) {
            return false;
        }
        detachSurface();
        window = incomingWindow.release();
        status = "attaching";
        lastResult = "VK_SUCCESS";
        error.clear();
        requestedWidth = std::max(width, 0);
        requestedHeight = std::max(height, 0);
        if (window == nullptr) {
            setFailure("Android native window is unavailable");
            return false;
        }
        if (!createInstance() || !createSurface()
                || !ensureDeviceForSurface()) {
            detachSurface();
            status = "failed";
            return false;
        }
        if (requestedWidth == 0 || requestedHeight == 0) {
            status = "waiting_for_nonzero_extent";
            swapchainRecreatePending = true;
            return true;
        }
        return createSwapchain();
    }

    void destroyFrameResources() noexcept {
        for (FrameResources& frame : frames) {
            frame.submissionPending = false;
            frame.submissionSerial = 0;
            frame.gpuTimestampRecorded = false;
            frame.gpuTimestampPending = false;
            frame.gpuProfileGeneration = 0;
            frame.deferredReleases.releaseAll();
            frame.backgroundDescriptorSet.reset();
            frame.uploadArena.reset();
            frame.inFlight.reset();
            frame.imageAvailable.reset();
            frame.commandBuffer.reset();
        }
        performanceQueryPool.reset();
        imageFences.clear();
    }

    void waitForSwapchainResourcesIdle() noexcept {
        if (device == VK_NULL_HANDLE) {
            return;
        }
        if (swapchain || renderPass
                || !depthImages.empty()) {
            ++resourceCounters->deviceWaitIdleCount;
            (void)vkDeviceWaitIdle(device);
            markAllFrameSubmissionsComplete();
            for (FrameResources& frame : frames) {
                frame.deferredReleases.releaseAll();
            }
        }
    }

    void destroySwapchainExtentResources() noexcept {
        framebuffers.clear();
        depthImageViews.clear();
        depthImages.clear();
        swapchainImageViews.clear();
        renderFinishedSemaphores.clear();
        swapchainImages.clear();
        swapchain.reset();
        imageFences.clear();
        displayExtent = {};
        surfaceExtent = {};
        surfaceTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapchainPreTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        currentFrame = 0;
    }

    void destroySurfaceRenderResources() noexcept {
        basicCellRenderer.destroyPipelines(
                VulkanPipelineTarget::Surface);
        densityFieldPipeline.reset();
        backgroundPipeline.reset();
        renderPass.reset();
    }

    void destroySwapchain() noexcept {
        waitForSwapchainResourcesIdle();
        destroySwapchainExtentResources();
        destroySurfaceRenderResources();
    }

    void detachSurface() noexcept {
        if (!checkThread("detachSurface")) {
            return;
        }
        pendingGenomeFrame.reset();
        activeOffscreenTarget.reset();
        destroySwapchain();
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        if (window != nullptr) {
            ANativeWindow_release(window);
            window = nullptr;
        }
        requestedWidth = 0;
        requestedHeight = 0;
        swapchainRecreatePending = false;
        status = "detached";
    }

    void destroyDevice() noexcept {
        destroySwapchain();
        if (device != VK_NULL_HANDLE) {
            ++resourceCounters->deviceWaitIdleCount;
            (void)vkDeviceWaitIdle(device);
            markAllFrameSubmissionsComplete();
            destroyOffscreenRenderResources();
            basicCellRenderer.reset();
            destroyBackgroundShaderResources();
            commandPool.reset();
            deviceContext.reset();
            device = VK_NULL_HANDLE;
        }
        physicalDevice = VK_NULL_HANDLE;
        physicalDeviceProperties = {};
        graphicsQueue = VK_NULL_HANDLE;
        presentQueue = VK_NULL_HANDLE;
        graphicsQueueFamily = kMissingQueueFamily;
        presentQueueFamily = kMissingQueueFamily;
        graphicsTimestampValidBits = 0;
        depthFormat = VK_FORMAT_UNDEFINED;
        setDebugObjectName = nullptr;
    }

    void destroyAll() noexcept {
        destroySwapchain();
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        if (window != nullptr) {
            ANativeWindow_release(window);
            window = nullptr;
        }
        destroyDevice();
        if (debugMessenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            const auto destroyDebugMessenger =
                    reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                            vkGetInstanceProcAddr(instance,
                                    "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyDebugMessenger != nullptr) {
                destroyDebugMessenger(
                        instance, debugMessenger, nullptr);
            }
            debugMessenger = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
    }

    [[nodiscard]] std::string diagnostics() const {
        std::uint64_t warnings = 0;
        std::uint64_t errors = 0;
        std::string validationMessage;
        {
            std::lock_guard lock(validationMutex);
            warnings = validationWarningCount;
            errors = validationErrorCount;
            validationMessage = lastValidationMessage;
        }

        std::ostringstream stream;
        stream << "schema=1\n";
        stream << "status=" << statusName(status) << '\n';
        stream << "requested_api_version=1.0.0\n";
        stream << "owner_thread_respected="
               << (std::this_thread::get_id() == ownerThread
                                  ? "true"
                                  : "false")
               << '\n';
        stream << "validation_requested="
               << (validationRequested ? "true" : "false") << '\n';
        stream << "validation_layer_available="
               << (validationLayerAvailable ? "true" : "false") << '\n';
        stream << "validation_enabled="
               << (validationEnabled ? "true" : "false") << '\n';
        stream << "debug_utils_available="
               << (debugUtilsAvailable ? "true" : "false") << '\n';
        stream << "debug_utils_enabled="
               << (debugUtilsEnabled ? "true" : "false") << '\n';
        stream << "debug_object_names_enabled="
               << (setDebugObjectName != nullptr ? "true" : "false") << '\n';
        stream << "validation_warning_count=" << warnings << '\n';
        stream << "validation_error_count=" << errors << '\n';
        stream << "last_validation_message="
               << sanitizeLineValue(validationMessage) << '\n';
        stream << "device_name="
               << sanitizeLineValue(physicalDeviceProperties.deviceName) << '\n';
        stream << "device_api_version="
               << versionString(physicalDeviceProperties.apiVersion) << '\n';
        stream << "driver_version_hex="
               << hexadecimal(physicalDeviceProperties.driverVersion) << '\n';
        stream << "vendor_id_hex="
               << hexadecimal(physicalDeviceProperties.vendorID) << '\n';
        stream << "device_id_hex="
               << hexadecimal(physicalDeviceProperties.deviceID) << '\n';
        stream << "graphics_queue_family=" << graphicsQueueFamily << '\n';
        stream << "present_queue_family=" << presentQueueFamily << '\n';
        stream << "present_mode=VK_PRESENT_MODE_FIFO_KHR\n";
        stream << "surface_format=" << formatName(surfaceFormat) << '\n';
        stream << "depth_format=" << formatName(depthFormat) << '\n';
        stream << "surface_extent=" << surfaceExtent.width
               << 'x' << surfaceExtent.height << '\n';
        stream << "display_extent=" << displayExtent.width
               << 'x' << displayExtent.height << '\n';
        stream << "surface_transform="
               << static_cast<std::uint32_t>(surfaceTransform) << '\n';
        stream << "swapchain_pre_transform="
               << static_cast<std::uint32_t>(swapchainPreTransform) << '\n';
        stream << "application_prerotation=true\n";
        stream << "swapchain_image_count=" << swapchainImages.size() << '\n';
        stream << "frames_in_flight=" << kFramesInFlight << '\n';
        stream << "performance_profile_enabled="
               << (performanceProfilingEnabled ? "true" : "false") << '\n';
        stream << "performance_profile_available="
               << (CELLULARITY_ENABLE_VULKAN_PROFILING ? "true" : "false")
               << '\n';
        stream << "performance_profile_generation="
               << performanceProfileGeneration << '\n';
        stream << "profiled_presented_frame_count="
               << profiledPresentedFrameCount << '\n';
        stream << "profiled_acquire_deferred_count="
               << profiledAcquireDeferredCount << '\n';
        stream << "gpu_timestamp_valid_bits="
               << graphicsTimestampValidBits << '\n';
        stream << "gpu_timestamp_period_ns="
               << physicalDeviceProperties.limits.timestampPeriod << '\n';
        stream << "gpu_timestamp_query_pool_available="
               << (performanceQueryPool ? "true" : "false") << '\n';
        stream << "gpu_timestamp_read_failure_count="
               << gpuTimestampReadFailureCount << '\n';
        appendTimingDistribution(
                stream, "cpu_frame", cpuFrameTiming);
        appendTimingDistribution(
                stream, "cpu_fence_wait", cpuFenceWaitTiming);
        appendTimingDistribution(
                stream, "cpu_upload", cpuUploadTiming);
        appendTimingDistribution(
                stream, "cpu_acquire", cpuAcquireTiming);
        appendTimingDistribution(
                stream, "cpu_uniform", cpuUniformTiming);
        appendTimingDistribution(
                stream, "cpu_command_record", cpuCommandRecordTiming);
        appendTimingDistribution(
                stream, "cpu_submit", cpuSubmitTiming);
        appendTimingDistribution(
                stream, "cpu_present", cpuPresentTiming);
        appendTimingDistribution(
                stream, "gpu_frame", gpuFrameTiming);
        for (std::size_t pass = 0; pass < gpuPassTimings.size(); ++pass) {
            appendTimingDistribution(stream,
                    std::string("gpu_pass_") + kGpuPassNames[pass],
                    gpuPassTimings[pass]);
        }
        const auto counterDelta = [](std::uint64_t current,
                                          std::uint64_t baseline) {
            return current >= baseline ? current - baseline : 0;
        };
        const std::uint64_t profileBasicCellDraws = counterDelta(
                basicCellRenderer.basicCellDrawCount(),
                performanceCounterBaseline.basicCellDraws);
        const std::uint64_t profileArrowDraws = counterDelta(
                basicCellRenderer.arrowDrawCount(),
                performanceCounterBaseline.arrowDraws);
        const std::uint64_t profileFoodDraws = counterDelta(
                basicCellRenderer.foodDrawCount(),
                performanceCounterBaseline.foodDraws);
        const std::uint64_t profileLinkDraws = counterDelta(
                basicCellRenderer.linkDrawCount(),
                performanceCounterBaseline.linkDraws);
        const std::uint64_t profileDeadDraws = counterDelta(
                basicCellRenderer.deadDrawCount(),
                performanceCounterBaseline.deadDraws);
        const std::uint64_t profileOutlineDraws = counterDelta(
                basicCellRenderer.outlineDrawCount(),
                performanceCounterBaseline.outlineDraws);
        const std::uint64_t profileMarkerDraws = counterDelta(
                basicCellRenderer.markerDrawCount(),
                performanceCounterBaseline.markerDraws);
        const std::uint64_t profileGenomeGridDraws = counterDelta(
                basicCellRenderer.genomeGridDrawCount(),
                performanceCounterBaseline.genomeGridDraws);
        const std::uint64_t profileGenomeTreeDraws = counterDelta(
                basicCellRenderer.genomeTreeDrawCount(),
                performanceCounterBaseline.genomeTreeDraws);
        const std::uint64_t profileTextDraws = counterDelta(
                basicCellRenderer.textDrawCount(),
                performanceCounterBaseline.textDraws);
        stream << "profile_swapchain_recreation_count="
               << counterDelta(swapchainCreationCount,
                          performanceCounterBaseline.swapchainCreations)
               << '\n';
        stream << "profile_geometry_upload_count="
               << counterDelta(basicCellRenderer.geometryUploadCount(),
                          performanceCounterBaseline.geometryUploads)
               << '\n';
        stream << "profile_geometry_upload_skip_count="
               << counterDelta(basicCellRenderer.geometryUploadSkipCount(),
                          performanceCounterBaseline.geometryUploadSkips)
               << '\n';
        stream << "profile_geometry_upload_bytes="
               << counterDelta(basicCellRenderer.geometryUploadBytes(),
                          performanceCounterBaseline.geometryUploadBytes)
               << '\n';
        stream << "profile_geometry_buffer_growth_count="
               << counterDelta(
                          basicCellRenderer.geometryBufferGrowthCount(),
                          performanceCounterBaseline.geometryBufferGrowths)
               << '\n';
        stream << "profile_text_upload_count="
               << counterDelta(
                          basicCellRenderer.textGeometryUploadCount(),
                          performanceCounterBaseline.textUploads)
               << '\n';
        stream << "profile_text_upload_skip_count="
               << counterDelta(
                          basicCellRenderer.textGeometryUploadSkipCount(),
                          performanceCounterBaseline.textUploadSkips)
               << '\n';
        stream << "profile_text_upload_bytes="
               << counterDelta(
                          basicCellRenderer.textGeometryUploadBytes(),
                          performanceCounterBaseline.textUploadBytes)
               << '\n';
        stream << "profile_text_buffer_growth_count="
               << counterDelta(
                          basicCellRenderer.textGeometryBufferGrowthCount(),
                          performanceCounterBaseline.textBufferGrowths)
               << '\n';
        stream << "profile_draw_count="
               << profileBasicCellDraws + profileArrowDraws
                        + profileFoodDraws + profileLinkDraws
                        + profileDeadDraws + profileOutlineDraws
                        + profileMarkerDraws + profileGenomeGridDraws
                        + profileGenomeTreeDraws + profileTextDraws
               << '\n';
        stream << "profile_basic_cell_draw_count="
               << profileBasicCellDraws << '\n';
        stream << "profile_arrow_draw_count=" << profileArrowDraws << '\n';
        stream << "profile_food_draw_count=" << profileFoodDraws << '\n';
        stream << "profile_link_draw_count=" << profileLinkDraws << '\n';
        stream << "profile_dead_draw_count=" << profileDeadDraws << '\n';
        stream << "profile_outline_draw_count="
               << profileOutlineDraws << '\n';
        stream << "profile_marker_draw_count=" << profileMarkerDraws << '\n';
        stream << "profile_genome_grid_draw_count="
               << profileGenomeGridDraws << '\n';
        stream << "profile_genome_tree_draw_count="
               << profileGenomeTreeDraws << '\n';
        stream << "profile_text_draw_count=" << profileTextDraws << '\n';
        stream << "profile_pipeline_bind_count="
               << counterDelta(basicCellRenderer.pipelineBindCount(),
                          performanceCounterBaseline.pipelineBinds)
               << '\n';
        stream << "profile_descriptor_bind_count="
               << counterDelta(resourceCounters->descriptorBindCount,
                          performanceCounterBaseline.descriptorBinds)
               << '\n';
        stream << "profile_descriptor_update_count="
               << counterDelta(resourceCounters->descriptorUpdateCount,
                          performanceCounterBaseline.descriptorUpdates)
               << '\n';
        stream << "profile_upload_bytes="
               << counterDelta(resourceCounters->uploadBytes,
                          performanceCounterBaseline.uploadBytes)
               << '\n';
        stream << "profile_upload_flush_count="
               << counterDelta(resourceCounters->uploadFlushCount,
                          performanceCounterBaseline.uploadFlushes)
               << '\n';
        stream << "profile_upload_arena_growth_count="
               << counterDelta(resourceCounters->uploadArenaGrowthCount,
                          performanceCounterBaseline.uploadArenaGrowths)
               << '\n';
        stream << "profile_command_buffer_record_count="
               << counterDelta(resourceCounters->commandBufferRecordCount,
                          performanceCounterBaseline.commandBufferRecords)
               << '\n';
        stream << "profile_queue_submission_count="
               << counterDelta(resourceCounters->queueSubmissionCount,
                          performanceCounterBaseline.queueSubmissions)
               << '\n';
        stream << "profile_fence_wait_count="
               << counterDelta(resourceCounters->fenceWaitCount,
                          performanceCounterBaseline.fenceWaits)
               << '\n';
        stream << "profile_blocking_fence_wait_count="
               << counterDelta(resourceCounters->blockingFenceWaitCount,
                          performanceCounterBaseline.blockingFenceWaits)
               << '\n';
        stream << "profile_device_wait_idle_count="
               << counterDelta(resourceCounters->deviceWaitIdleCount,
                          performanceCounterBaseline.deviceWaitIdles)
               << '\n';
        stream << "profile_steady_state_resource_create_count="
               << counterDelta(
                          resourceCounters->steadyStateResourceCreateCount,
                          performanceCounterBaseline
                                  .steadyStateResourceCreates)
               << '\n';
        stream << "profile_controlled_capacity_resource_create_count="
               << counterDelta(
                          resourceCounters
                                  ->controlledCapacityResourceCreateCount,
                          performanceCounterBaseline
                                  .controlledCapacityResourceCreates)
               << '\n';
        stream << "profile_memory_allocation_count="
               << counterDelta(resourceCounters->memoryAllocationCount,
                          performanceCounterBaseline.memoryAllocations)
               << '\n';
        stream << "profile_memory_free_count="
               << counterDelta(resourceCounters->memoryFreeCount,
                          performanceCounterBaseline.memoryFrees)
               << '\n';
        stream << "profile_resource_create_count="
               << counterDelta(resourceCounters->resourceCreateCount,
                          performanceCounterBaseline.resourceCreates)
               << '\n';
        stream << "profile_resource_destroy_count="
               << counterDelta(resourceCounters->resourceDestroyCount,
                          performanceCounterBaseline.resourceDestroys)
               << '\n';
        stream << "render_pass=production_microscope_scene_with_overlays\n";
        stream << "offscreen_color_format=VK_FORMAT_R8G8B8A8_UNORM\n";
        stream << "offscreen_output_row_order=top_left\n";
        stream << "offscreen_readback_only_on_capture=true\n";
        stream << "offscreen_capture_active="
               << (activeOffscreenTarget.has_value() ? "true" : "false")
               << '\n';
        const OffscreenTargetResources& genomeTarget = offscreenTarget(
                OffscreenRenderTarget::GenomePreview);
        const OffscreenTargetResources& substrateTarget = offscreenTarget(
                OffscreenRenderTarget::SubstratePreview);
        stream << "offscreen_genome_extent=" << genomeTarget.width << 'x'
               << genomeTarget.height << '\n';
        stream << "offscreen_genome_row_pitch=" << genomeTarget.rowPitch
               << '\n';
        stream << "offscreen_substrate_extent=" << substrateTarget.width
               << 'x' << substrateTarget.height << '\n';
        stream << "offscreen_substrate_row_pitch="
               << substrateTarget.rowPitch << '\n';
        stream << "shader_target_environment=vulkan1.0\n";
        stream << "background_push_constant_bytes=0\n";
        stream << "background_uniform_bytes="
               << sizeof(BackgroundUniformData) << '\n';
        stream << "background_descriptor_set_count=1\n";
        stream << "background_descriptor_binding_count=1\n";
        stream << "background_descriptor_type="
                  "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC\n";
        stream << "background_descriptor_pool_max_sets="
               << kFramesInFlight << '\n';
        stream << "density_field_uniform_bytes="
               << sizeof(EnvironmentFieldUniformData) << '\n';
        stream << "density_field_push_constant_bytes=0\n";
        stream << "basic_cell_program_count="
               << kVulkanBasicCellProgramCount << '\n';
        stream << "world_program_count=" << kVulkanWorldProgramCount
               << '\n';
        stream << "basic_cell_uniform_bytes=" << sizeof(CellUniformData)
               << '\n';
        stream << "basic_cell_descriptor_set_count="
               << VulkanBasicCellRenderer::kFrameDescriptorCount << '\n';
        stream << "basic_cell_descriptor_binding_count=4\n";
        stream << "basic_cell_geometry_slot_count="
               << VulkanBasicCellRenderer::kGeometrySlotCount << '\n';
        stream << "text_geometry_slot_count="
               << VulkanBasicCellRenderer::kTextGeometrySlotCount << '\n';
        stream << "basic_cell_geometry_upload_count="
               << basicCellRenderer.geometryUploadCount() << '\n';
        stream << "basic_cell_geometry_upload_skip_count="
               << basicCellRenderer.geometryUploadSkipCount() << '\n';
        stream << "basic_cell_geometry_upload_bytes="
               << basicCellRenderer.geometryUploadBytes() << '\n';
        stream << "basic_cell_geometry_buffer_growth_count="
               << basicCellRenderer.geometryBufferGrowthCount() << '\n';
        stream << "basic_cell_draw_count="
               << basicCellRenderer.basicCellDrawCount() << '\n';
        stream << "world_arrow_draw_count="
               << basicCellRenderer.arrowDrawCount() << '\n';
        stream << "world_food_draw_count="
               << basicCellRenderer.foodDrawCount() << '\n';
        stream << "world_link_draw_count="
               << basicCellRenderer.linkDrawCount() << '\n';
        stream << "world_dead_draw_count="
               << basicCellRenderer.deadDrawCount() << '\n';
        stream << "world_outline_draw_count="
               << basicCellRenderer.outlineDrawCount() << '\n';
        stream << "world_marker_draw_count="
               << basicCellRenderer.markerDrawCount() << '\n';
        stream << "genome_grid_draw_count="
               << basicCellRenderer.genomeGridDrawCount() << '\n';
        stream << "genome_tree_draw_count="
               << basicCellRenderer.genomeTreeDrawCount() << '\n';
        stream << "overlay_text_draw_count="
               << basicCellRenderer.textDrawCount() << '\n';
        stream << "overlay_text_geometry_upload_count="
               << basicCellRenderer.textGeometryUploadCount() << '\n';
        stream << "overlay_text_geometry_upload_skip_count="
               << basicCellRenderer.textGeometryUploadSkipCount() << '\n';
        stream << "overlay_text_geometry_upload_bytes="
               << basicCellRenderer.textGeometryUploadBytes() << '\n';
        stream << "overlay_text_geometry_buffer_growth_count="
               << basicCellRenderer.textGeometryBufferGrowthCount() << '\n';
        stream << "world_pipeline_bind_count="
               << basicCellRenderer.pipelineBindCount() << '\n';
        stream << "basic_cell_active_capture_sequence="
               << basicCellRenderer.activeCaptureSequence() << '\n';
        stream << "basic_cell_active_geometry_revision="
               << basicCellRenderer.activeGeometryRevision() << '\n';
        stream << "basic_cell_active_instance_revision="
               << basicCellRenderer.activeInstanceRevision() << '\n';
        stream << "basic_cell_dot_texture_extent="
               << basicCellRenderer.dotTextureWidth() << 'x'
               << basicCellRenderer.dotTextureHeight() << '\n';
        stream << "world_bessel_texture_extent="
               << basicCellRenderer.besselTextureWidth() << 'x'
               << basicCellRenderer.besselTextureHeight() << '\n';
        stream << "overlay_glyph_atlas_extent="
               << basicCellRenderer.glyphAtlasWidth() << 'x'
               << basicCellRenderer.glyphAtlasHeight() << '\n';
        stream << "overlay_glyph_atlas_format=VK_FORMAT_R8_UNORM\n";
        stream << "performance_hud_demo_enabled="
               << (demoPerformanceHudEnabled ? "true" : "false") << '\n';
        stream << "decoded_texture_payload_retained=false\n";
        stream << "upload_strategy=per_frame_persistently_mapped_ring\n";
        stream << "non_coherent_upload_flush_supported=true\n";
        stream << "device_local_upload_copy_enabled=false\n";
        stream << "frame_resources_survive_swapchain_recreation=true\n";
        stream << "present_semaphore_ownership=swapchain_image\n";
        stream << "clip_y_flipped=true\n";
        stream << "clip_depth_converted_to_zero_one=true\n";
        stream << "presented_frame_count=" << frameCount << '\n';
        stream << "suboptimal_frame_count=" << suboptimalFrameCount << '\n';
        stream << "instance_creation_count="
               << instanceCreationCount << '\n';
        stream << "device_creation_count="
               << deviceCreationCount << '\n';
        stream << "swapchain_creation_count="
               << swapchainCreationCount << '\n';
        stream << "density_field_pipeline_creation_count="
               << densityFieldPipelineCreationCount << '\n';
        stream << "offscreen_render_pass_creation_count="
               << offscreenRenderPassCreationCount << '\n';
        stream << "offscreen_target_creation_count="
               << offscreenTargetCreationCount << '\n';
        stream << "offscreen_capture_count=" << offscreenCaptureCount
               << '\n';
        stream << "offscreen_readback_bytes=" << offscreenReadbackBytes
               << '\n';
        stream << "surface_generation=" << surfaceGeneration << '\n';
        stream << "next_submission_serial=" << nextSubmissionSerial << '\n';
        stream << "completed_submission_serial="
               << completedSubmissionSerial << '\n';
        std::uint64_t pendingFrameCount = 0;
        VkDeviceSize uploadCapacity = 0;
        VkDeviceSize uploadUsed = 0;
        for (const FrameResources& frame : frames) {
            pendingFrameCount += frame.submissionPending ? 1U : 0U;
            uploadCapacity += frame.uploadArena.capacity();
            uploadUsed += frame.uploadArena.used();
        }
        stream << "pending_frame_count=" << pendingFrameCount << '\n';
        stream << "render_finished_semaphore_count="
               << renderFinishedSemaphores.size() << '\n';
        stream << "upload_capacity_bytes=" << uploadCapacity << '\n';
        stream << "upload_used_bytes=" << uploadUsed << '\n';
        stream << "upload_bytes_total=" << resourceCounters->uploadBytes
               << '\n';
        stream << "upload_flush_count="
               << resourceCounters->uploadFlushCount << '\n';
        stream << "upload_arena_growth_count="
               << resourceCounters->uploadArenaGrowthCount << '\n';
        stream << "command_buffer_record_count="
               << resourceCounters->commandBufferRecordCount << '\n';
        stream << "descriptor_bind_count="
               << resourceCounters->descriptorBindCount << '\n';
        stream << "descriptor_update_count="
               << resourceCounters->descriptorUpdateCount << '\n';
        stream << "queue_submission_count="
               << resourceCounters->queueSubmissionCount << '\n';
        stream << "fence_wait_count="
               << resourceCounters->fenceWaitCount << '\n';
        stream << "blocking_fence_wait_count="
               << resourceCounters->blockingFenceWaitCount << '\n';
        stream << "device_wait_idle_count="
               << resourceCounters->deviceWaitIdleCount << '\n';
        stream << "steady_state_resource_create_count="
               << resourceCounters->steadyStateResourceCreateCount << '\n';
        stream << "controlled_capacity_resource_create_count="
               << resourceCounters->controlledCapacityResourceCreateCount
               << '\n';
        stream << "memory_allocation_count="
               << resourceCounters->memoryAllocationCount << '\n';
        stream << "memory_free_count="
               << resourceCounters->memoryFreeCount << '\n';
        stream << "memory_bytes_allocated_total="
               << resourceCounters->memoryBytesAllocatedTotal << '\n';
        stream << "memory_bytes_live="
               << resourceCounters->memoryBytesLive << '\n';
        stream << "memory_bytes_peak="
               << resourceCounters->memoryBytesPeak << '\n';
        stream << "resource_create_count="
               << resourceCounters->resourceCreateCount << '\n';
        stream << "resource_destroy_count="
               << resourceCounters->resourceDestroyCount << '\n';
        stream << "resource_accounting_violation_count="
               << resourceCounters->resourceAccountingViolationCount << '\n';
        stream << "live_resource_count="
               << resourceCounters->liveResourceCount << '\n';
        stream << "peak_live_resource_count="
               << resourceCounters->peakLiveResourceCount << '\n';
        stream << "deferred_release_retire_count="
               << resourceCounters->deferredReleaseRetireCount << '\n';
        stream << "deferred_release_destroy_count="
               << resourceCounters->deferredReleaseDestroyCount << '\n';
        stream << "deferred_release_live_count="
               << resourceCounters->deferredReleaseLiveCount << '\n';
        stream << "deferred_release_peak_live_count="
               << resourceCounters->deferredReleasePeakLiveCount << '\n';
        stream << "synchronization_reuse_violation_count="
               << resourceCounters->synchronizationReuseViolationCount
               << '\n';
        stream << "device_destruction_count="
               << resourceCounters->deviceDestructionCount << '\n';
        stream << "device_destruction_with_live_resources_count="
               << resourceCounters->deviceDestructionWithLiveResourcesCount
               << '\n';
        bool resourceAccountingConsistent = true;
        for (std::size_t index = 0;
                index < static_cast<std::size_t>(
                        VulkanResourceKind::Count);
                ++index) {
            const auto kind = static_cast<VulkanResourceKind>(index);
            const VulkanResourceKindCounters& counters =
                    resourceCounters->resources[index];
            resourceAccountingConsistent = resourceAccountingConsistent
                    && counters.created >= counters.destroyed
                    && counters.created - counters.destroyed == counters.live;
            stream << "resource_" << resourceKindName(kind)
                   << "_created=" << counters.created << '\n';
            stream << "resource_" << resourceKindName(kind)
                   << "_live=" << counters.live << '\n';
            stream << "resource_" << resourceKindName(kind)
                   << "_peak_live=" << counters.peakLive << '\n';
        }
        resourceAccountingConsistent = resourceAccountingConsistent
                && resourceCounters->resourceAccountingViolationCount == 0;
        stream << "resource_accounting_consistent="
               << (resourceAccountingConsistent ? "true" : "false")
               << '\n';
        stream << "last_result=" << sanitizeLineValue(lastResult) << '\n';
        stream << "last_frame_presented="
               << (lastFrameWasPresented ? "true" : "false") << '\n';
        stream << "error=" << sanitizeLineValue(error) << '\n';
        stream << "live_instance="
               << (instance != VK_NULL_HANDLE ? 1 : 0) << '\n';
        stream << "live_device="
               << (device != VK_NULL_HANDLE ? 1 : 0) << '\n';
        stream << "live_surface="
               << (surface != VK_NULL_HANDLE ? 1 : 0) << '\n';
        stream << "live_swapchain="
               << (swapchain ? 1 : 0) << '\n';
        stream << "live_background_shader_modules="
               << ((backgroundVertexShader && backgroundFragmentShader)
                                  ? 2
                                  : 0)
               << '\n';
        stream << "live_density_field_shader_modules="
               << ((densityFieldVertexShader && densityFieldFragmentShader)
                                  ? 2
                                  : 0)
               << '\n';
        stream << "live_background_pipeline="
               << (backgroundPipeline ? 1 : 0) << '\n';
        stream << "live_density_field_pipeline="
               << (densityFieldPipeline ? 1 : 0) << '\n';
        stream << "live_offscreen_render_pass="
               << (offscreenRenderPass ? 1 : 0) << '\n';
        stream << "live_offscreen_background_pipeline="
               << (offscreenBackgroundPipeline ? 1 : 0) << '\n';
        stream << "live_offscreen_basic_cell_renderer="
               << (basicCellRenderer.ready(VulkanPipelineTarget::Offscreen)
                                  ? 1
                                  : 0)
               << '\n';
        stream << "live_basic_cell_renderer="
               << (basicCellRenderer.ready() ? 1 : 0) << '\n';
        return stream.str();
    }
};

AndroidVulkanClearFrameSession::AndroidVulkanClearFrameSession(
        std::vector<std::uint32_t> backgroundVertexSpirv,
        std::vector<std::uint32_t> backgroundFragmentSpirv,
        std::vector<std::uint32_t> densityFieldVertexSpirv,
        std::vector<std::uint32_t> densityFieldFragmentSpirv,
        std::array<VulkanCellShaderSpirv, kVulkanBasicCellProgramCount>
                cellShaderSpirv,
        std::array<VulkanCellShaderSpirv, kVulkanWorldProgramCount>
                worldShaderSpirv,
        NativeRenderTexturePayload dotTexture,
        NativeRenderTexturePayload besselTexture)
        : mImpl(std::make_unique<Impl>(
                  std::move(backgroundVertexSpirv),
                  std::move(backgroundFragmentSpirv),
                  std::move(densityFieldVertexSpirv),
                  std::move(densityFieldFragmentSpirv),
                  std::move(cellShaderSpirv),
                  std::move(worldShaderSpirv), std::move(dotTexture),
                  std::move(besselTexture))) {}

AndroidVulkanClearFrameSession::~AndroidVulkanClearFrameSession() = default;

bool AndroidVulkanClearFrameSession::attachWindow(
        ANativeWindow* window, int width, int height) {
    return mImpl->attachWindow(window, width, height);
}

void AndroidVulkanClearFrameSession::resizeSurface(int width, int height) {
    mImpl->resizeSurface(width, height);
}

void AndroidVulkanClearFrameSession::detachSurface() {
    mImpl->detachSurface();
}

void AndroidVulkanClearFrameSession::setPerformanceProfilingEnabled(
        bool enabled) {
    mImpl->setPerformanceProfilingEnabled(enabled);
}

RenderFrameResult AndroidVulkanClearFrameSession::renderFrame() {
    return makeRenderFrameResult(
            mImpl->renderFrame(), kPrimaryContentPresented);
}

RenderFrameResult AndroidVulkanClearFrameSession::renderFrame(
        bool performanceHudEnabled) {
    return makeRenderFrameResult(mImpl->renderFrame(performanceHudEnabled),
            kPrimaryContentPresented);
}

RenderFrameResult AndroidVulkanClearFrameSession::renderFrame(
        const PreparedRenderFrameView& frame,
        const RenderPresentationState& presentation) {
    return makeRenderFrameResult(
            mImpl->renderFrame(frame, presentation, nullptr),
            kPrimaryContentPresented);
}

RenderFrameResult AndroidVulkanClearFrameSession::renderFrame(
        const PreparedRenderFrameView& frame,
        const RenderPresentationState& presentation,
        const cellularity::microscope::MicroscopeDebugHudTelemetry*
                debugHudTelemetry) {
    return makeRenderFrameResult(
            mImpl->renderFrame(frame, presentation, debugHudTelemetry),
            kPrimaryContentPresented);
}

RenderPassStatus AndroidVulkanClearFrameSession::drawMicroscopeOffscreenPass(
        const PreparedRenderFrameView& frame,
        const RenderPresentationState& presentation) {
    return mImpl->renderFrame(frame, presentation, nullptr)
                    == RenderFrameOutcome::Presented
            ? RenderPassStatus::Completed
            : RenderPassStatus::Failed;
}

RenderFrameResult AndroidVulkanClearFrameSession::renderClear(
        const RenderViewportState& viewport,
        const std::array<float, 4>& color) {
    return makeRenderFrameResult(
            mImpl->renderClear(viewport, color), kPrimaryContentPresented);
}

RenderFrameResult
AndroidVulkanClearFrameSession::renderEnvironmentFieldPreview(
        const EnvironmentFieldPreviewCommand& command) {
    return makeRenderFrameResult(
            mImpl->renderEnvironmentFieldPreview(command),
            kPrimaryContentPresented);
}

void AndroidVulkanClearFrameSession::beginOffscreenRgba(
        OffscreenRenderTarget target, int width, int height) {
    mImpl->beginOffscreenRgba(target, width, height);
}

std::vector<std::uint8_t>
AndroidVulkanClearFrameSession::finishOffscreenRgba(
        OffscreenRenderTarget target) {
    return mImpl->finishOffscreenRgba(target);
}

void AndroidVulkanClearFrameSession::cancelOffscreenRgba(
        OffscreenRenderTarget target) noexcept {
    mImpl->cancelOffscreenRgba(target);
}

void AndroidVulkanClearFrameSession::beginGenomePreviewFrame(
        int width, int height, bool fullyTransparentClear) {
    mImpl->beginGenomePreviewFrame(
            width, height, fullyTransparentClear);
}

void AndroidVulkanClearFrameSession::drawGenomePreviewGrid(
        const GenomePreviewGridCommand& command) {
    mImpl->drawGenomePreviewGrid(command);
}

void AndroidVulkanClearFrameSession::drawGenomePreviewTree(
        const RenderPresentationState& presentation,
        const PreparedRenderFrameView& frame,
        const std::array<float, 16>& matrix,
        PreparedFrameSpan<float> lineVertices,
        const GenomePreviewDrawOptions& options) {
    mImpl->drawGenomePreviewTree(
            presentation, frame, matrix, lineVertices, options);
}

void AndroidVulkanClearFrameSession::drawGenomePreview(
        const RenderPresentationState& presentation,
        const PreparedRenderFrameView& frame,
        const GenomePreviewDrawOptions& options) {
    mImpl->drawGenomePreview(presentation, frame, options);
}

RenderFrameResult AndroidVulkanClearFrameSession::endGenomePreviewFrame() {
    return makeRenderFrameResult(
            mImpl->endGenomePreviewFrame(), kPrimaryContentPresented);
}

RenderPassStatus
AndroidVulkanClearFrameSession::endGenomePreviewOffscreenPass() {
    return mImpl->endGenomePreviewFrame() == RenderFrameOutcome::Presented
            ? RenderPassStatus::Completed
            : RenderPassStatus::Failed;
}

void AndroidVulkanClearFrameSession::cancelGenomePreviewFrame() noexcept {
    mImpl->cancelGenomePreviewFrame();
}

bool AndroidVulkanClearFrameSession::hasAttachedSurface() const noexcept {
    return mImpl->surface != VK_NULL_HANDLE;
}

std::string AndroidVulkanClearFrameSession::diagnostics() const {
    return mImpl->diagnostics();
}

} // namespace cellularity::render::vulkan
