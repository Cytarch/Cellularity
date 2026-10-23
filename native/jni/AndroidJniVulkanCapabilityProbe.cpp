#include <jni.h>

#include <android/native_window_jni.h>

#include <exception>
#include <string>

#include "platform/android/vulkan/AndroidVulkanCapabilityProbe.h"

namespace {

class NativeWindowReference {
public:
    explicit NativeWindowReference(ANativeWindow* window) : mWindow(window) {}

    ~NativeWindowReference() {
        if (mWindow != nullptr) {
            ANativeWindow_release(mWindow);
        }
    }

    NativeWindowReference(const NativeWindowReference&) = delete;
    NativeWindowReference& operator=(const NativeWindowReference&) = delete;

    [[nodiscard]] ANativeWindow* get() const {
        return mWindow;
    }

private:
    ANativeWindow* mWindow;
};

jbyteArray toJavaUtf8(JNIEnv* env, const std::string& value) {
    jbyteArray result = env->NewByteArray(static_cast<jsize>(value.size()));
    if (result != nullptr && !value.empty()) {
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(value.size()),
                reinterpret_cast<const jbyte*>(value.data()));
    }
    return result;
}

std::string failureReport(const char* message) {
    std::string sanitizedMessage = message != nullptr ? message : "unknown error";
    for (char& character : sanitizedMessage) {
        if (character == '\n' || character == '\r' || character == '\t') {
            character = ' ';
        }
    }
    return std::string("schema=1\n")
            + "status=failed\n"
            + "loader_available=true\n"
            + "requested_api_version=1.0.0\n"
            + "instance_api_version_raw=0\n"
            + "instance_api_version=unknown\n"
            + "instance_version_query_result=VK_RESULT_UNKNOWN\n"
            + "instance_surface_extension=false\n"
            + "android_surface_extension=false\n"
            + "physical_device_count=0\n"
            + "error=" + sanitizedMessage + "\n"
            + "live_vulkan_objects_after_probe=0\n";
}

} // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanCapabilityBridge_nativeProbe(
        JNIEnv* env, jclass, jobject surface) {
    if (surface == nullptr) {
        return toJavaUtf8(env, failureReport("Android surface is null"));
    }

    try {
        NativeWindowReference window(ANativeWindow_fromSurface(env, surface));
        if (window.get() == nullptr) {
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return toJavaUtf8(env,
                    failureReport("Could not acquire Android native window"));
        }
        return toJavaUtf8(env,
                cellularity::android::vulkan::probeCapabilities(window.get()));
    } catch (const std::exception& exception) {
        return toJavaUtf8(env, failureReport(exception.what()));
    } catch (...) {
        return toJavaUtf8(env,
                failureReport("Unexpected native Vulkan probe failure"));
    }
}
