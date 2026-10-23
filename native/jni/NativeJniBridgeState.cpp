#include "NativeJniBridgeState.h"

#include "NativeJniUtils.h"

std::array<float, 16> copyMatrix(JNIEnv* env, jfloatArray matrix) {
    if (matrix == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, "MVP matrix is null");
        return {};
    }
    if (env->GetArrayLength(matrix) < 16) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, "MVP matrix must contain at least 16 floats");
        return {};
    }

    std::array<float, 16> result{};
    env->GetFloatArrayRegion(matrix, 0, 16, result.data());
    return result;
}

std::array<float, 3> copyColor(JNIEnv* env, jfloatArray color) {
    if (color == nullptr) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, "Background color is null");
        return {};
    }
    if (env->GetArrayLength(color) < 3) {
        throwJavaException(env, JAVA_ILLEGAL_ARGUMENT_EXCEPTION, "Background color must contain at least 3 floats");
        return {};
    }

    std::array<float, 3> result{};
    env->GetFloatArrayRegion(color, 0, 3, result.data());
    return result;
}
