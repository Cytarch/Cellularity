#include <jni.h>

#ifdef __ANDROID__

#include <csignal>
#include <cstdint>
#include <ctime>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include "diagnostics/NativeCrashReportWriter.h"

#if defined(__arm__)
#include <asm/sigcontext.h>
#endif

namespace {

constexpr size_t PATH_BUFFER_SIZE = 512;
constexpr size_t APP_VERSION_BUFFER_SIZE = 128;
constexpr size_t DEVICE_BUFFER_SIZE = 256;
constexpr size_t ABI_BUFFER_SIZE = 256;

char gCrashDirectory[PATH_BUFFER_SIZE] = {};
char gAppVersion[APP_VERSION_BUFFER_SIZE] = {};
char gDevice[DEVICE_BUFFER_SIZE] = {};
char gSupportedAbis[ABI_BUFFER_SIZE] = {};
struct sigaction gPreviousSigAbrt{};
struct sigaction gPreviousSigSegv{};
struct sigaction gPreviousSigBus{};
struct sigaction gPreviousSigIll{};
struct sigaction gPreviousSigFpe{};
bool gInstalled = false;
volatile sig_atomic_t gHandlingSignal = 0;
volatile sig_atomic_t gRenderBackendDiagnostics = 0;

constexpr sig_atomic_t RENDER_BACKEND_MASK = 0x0f;
constexpr unsigned FALLBACK_REASON_SHIFT = 4;

const char* renderBackendName(sig_atomic_t diagnostics) {
    switch (diagnostics & RENDER_BACKEND_MASK) {
        case 1:
            return "open_gl_es";
        case 2:
            return "vulkan";
        default:
            return "none";
    }
}

const char* renderBackendFallbackReason(sig_atomic_t diagnostics) {
    switch (diagnostics >> FALLBACK_REASON_SHIFT) {
        case 1:
            return "capability_unsupported";
        case 2:
            return "capability_probe_failed";
        case 3:
            return "vulkan_runtime_failed";
        default:
            return "none";
    }
}

void copyString(char* destination, size_t destinationSize, const char* source) {
    if (destinationSize == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < destinationSize && source != nullptr && source[i] != '\0'; i++) {
        destination[i] = source[i];
    }
    destination[i] = '\0';
}

const char* signalName(int signalNumber) {
    switch (signalNumber) {
        case SIGABRT:
            return "SIGABRT";
        case SIGSEGV:
            return "SIGSEGV";
        case SIGBUS:
            return "SIGBUS";
        case SIGILL:
            return "SIGILL";
        case SIGFPE:
            return "SIGFPE";
        default:
            return "UNKNOWN";
    }
}

long currentTid() {
#ifdef SYS_gettid
    return static_cast<long>(syscall(SYS_gettid));
#else
    return static_cast<long>(getpid());
#endif
}

uintptr_t programCounterFromContext(void* context) {
    if (context == nullptr) {
        return 0;
    }
    ucontext_t* ucontext = static_cast<ucontext_t*>(context);
#if defined(__aarch64__)
    return static_cast<uintptr_t>(ucontext->uc_mcontext.pc);
#elif defined(__arm__)
    return static_cast<uintptr_t>(ucontext->uc_mcontext.arm_pc);
#elif defined(__x86_64__)
    return static_cast<uintptr_t>(ucontext->uc_mcontext.gregs[REG_RIP]);
#elif defined(__i386__)
    return static_cast<uintptr_t>(ucontext->uc_mcontext.gregs[REG_EIP]);
#else
    return 0;
#endif
}

void writeNativeCrashReport(int signalNumber, siginfo_t* info, void* context) {
    if (gCrashDirectory[0] == '\0') {
        return;
    }

    struct timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);

    cellularity::diagnostics::NativeCrashReportRecord record;
    record.epochSeconds = static_cast<std::int64_t>(now.tv_sec);
    record.appVersion = gAppVersion;
    record.device = gDevice;
    record.supportedAbis = gSupportedAbis;
    const sig_atomic_t renderDiagnostics = gRenderBackendDiagnostics;
    record.renderBackend = renderBackendName(renderDiagnostics);
    record.renderBackendFallbackReason = renderBackendFallbackReason(renderDiagnostics);
    record.signalNumber = signalNumber;
    record.signalName = signalName(signalNumber);
    record.signalCode = info != nullptr ? info->si_code : 0;
    record.processId = static_cast<long>(getpid());
    record.threadId = currentTid();
    record.faultAddress = info != nullptr ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
    record.programCounter = programCounterFromContext(context);
    cellularity::diagnostics::writeNativeCrashReport(gCrashDirectory, record);
}

const struct sigaction* previousActionForSignal(int signalNumber) {
    switch (signalNumber) {
        case SIGABRT:
            return &gPreviousSigAbrt;
        case SIGSEGV:
            return &gPreviousSigSegv;
        case SIGBUS:
            return &gPreviousSigBus;
        case SIGILL:
            return &gPreviousSigIll;
        case SIGFPE:
            return &gPreviousSigFpe;
        default:
            return nullptr;
    }
}

void nativeCrashHandler(int signalNumber, siginfo_t* info, void* context) {
    if (gHandlingSignal != 0) {
        _exit(128 + signalNumber);
    }
    gHandlingSignal = 1;
    writeNativeCrashReport(signalNumber, info, context);

    const struct sigaction* previous = previousActionForSignal(signalNumber);
    if (previous != nullptr) {
        if ((previous->sa_flags & SA_SIGINFO) != 0 && previous->sa_sigaction != nullptr) {
            previous->sa_sigaction(signalNumber, info, context);
            return;
        }
        if (previous->sa_handler == SIG_IGN) {
            return;
        }
        if (previous->sa_handler != SIG_DFL && previous->sa_handler != nullptr) {
            previous->sa_handler(signalNumber);
            return;
        }
    }

    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
}

void installForSignal(int signalNumber, struct sigaction* previousAction) {
    struct sigaction action{};
    action.sa_sigaction = nativeCrashHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(signalNumber, &action, previousAction);
}

void copyJavaString(JNIEnv* env, jstring value, char* destination, size_t destinationSize) {
    if (value == nullptr) {
        copyString(destination, destinationSize, "unknown");
        return;
    }
    const char* text = env->GetStringUTFChars(value, nullptr);
    if (text == nullptr) {
        copyString(destination, destinationSize, "unknown");
        return;
    }
    copyString(destination, destinationSize, text);
    env->ReleaseStringUTFChars(value, text);
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_CrashLogger_nativeInstallNativeCrashHandler(JNIEnv* env,
        jobject,
        jstring crashDirectoryPath,
        jstring appVersion,
        jstring device,
        jstring supportedAbis) {
    copyJavaString(env, crashDirectoryPath, gCrashDirectory, sizeof(gCrashDirectory));
    copyJavaString(env, appVersion, gAppVersion, sizeof(gAppVersion));
    copyJavaString(env, device, gDevice, sizeof(gDevice));
    copyJavaString(env, supportedAbis, gSupportedAbis, sizeof(gSupportedAbis));

    if (gInstalled) {
        return;
    }
    installForSignal(SIGABRT, &gPreviousSigAbrt);
    installForSignal(SIGSEGV, &gPreviousSigSegv);
    installForSignal(SIGBUS, &gPreviousSigBus);
    installForSignal(SIGILL, &gPreviousSigIll);
    installForSignal(SIGFPE, &gPreviousSigFpe);
    gInstalled = true;
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_CrashLogger_nativeUpdateRenderBackendDiagnostics(
        JNIEnv*, jobject, jint backend, jint fallbackReason) {
    const sig_atomic_t safeBackend = backend >= 0 && backend <= 2 ? backend : 0;
    const sig_atomic_t safeFallbackReason =
            fallbackReason >= 0 && fallbackReason <= 3 ? fallbackReason : 0;
    gRenderBackendDiagnostics =
            safeBackend | (safeFallbackReason << FALLBACK_REASON_SHIFT);
}

#else

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_CrashLogger_nativeInstallNativeCrashHandler(
        JNIEnv*, jobject, jstring, jstring, jstring, jstring) {
}

extern "C" JNIEXPORT void JNICALL
Java_com_cytarch_cellularity_CrashLogger_nativeUpdateRenderBackendDiagnostics(
        JNIEnv*, jobject, jint, jint) {
}

#endif
