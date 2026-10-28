#include "render/backend/abi/CellularityVulkanBackendAbi.h"

#if defined(__ANDROID__)

#include <jni.h>

#include <cstdint>

// The managed loader asks the DSO for the address of the surface API table and
// installs it into the main library. This lives apart from
// VulkanBackendAbiExports.cpp so that the Windows host build of the backend needs
// no jni.h and no managed layer.
extern "C" CELLULARITY_VULKAN_BACKEND_EXPORT const CellularityVulkanSurfaceApiV1*
cellularity_vulkan_get_surface_api_v1(void);

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanBackendLoader_nativeBackendApiV1Address(
        JNIEnv*, jclass) {
    static_assert(sizeof(std::uintptr_t) <= sizeof(jlong));
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(
            cellularity_vulkan_get_surface_api_v1()));
}

#endif // __ANDROID__
