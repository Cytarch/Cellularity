# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# You can edit the include path and order by changing the proguardFiles
# directive in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# Add any project specific keep options here:

-renamesourcefileattribute SourceFile
-keepattributes SourceFile,LineNumberTable

# Constructed directly from JNI when external data cannot be decoded.
-keep class com.cytarch.cellularity.CellularityDataException {
    public <init>(java.lang.String);
    public <init>(java.lang.String, java.lang.String);
    *;
}
-keep enum com.cytarch.cellularity.shared.model.CellularityErrorCode { *; }

# Created by the native owner-publication bridge via its stable binary name.
-keep class com.cytarch.cellularity.NativeMicroscopeRuntimePublication { *; }

# Exact JNI owner for the retained Environment Field Preview state endpoint.
-keep class com.cytarch.cellularity.platform.render.preview.EnvironmentFieldPreviewBridge { *; }

# JNI symbols used by the in-process native crash reporter.
-keep class com.cytarch.cellularity.CrashLogger { native <methods>; }

# JNI symbol names for the lazily loaded optional Vulkan backend.
-keep class com.cytarch.cellularity.platform.render.vulkan.NativeVulkanCapabilityBridge { *; }
-keep class com.cytarch.cellularity.platform.render.vulkan.NativeVulkanBackendLoader { *; }
-keep class com.cytarch.cellularity.platform.render.vulkan.NativeVulkanRenderWorkerJni { native <methods>; }
-keep class com.cytarch.cellularity.platform.render.NativeRenderProfilingBridge { native <methods>; }
-keepclassmembers class com.cytarch.cellularity.platform.render.vulkan.NativeVulkanWorkerStatusListener {
    public int onNativeWorkerStatusSignal(long, long, int);
}

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}
