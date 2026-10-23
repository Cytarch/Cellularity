#pragma once

#include <jni.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "io/DecodeError.h"

constexpr const char* JAVA_RUNTIME_EXCEPTION = "java/lang/RuntimeException";
constexpr const char* JAVA_ILLEGAL_ARGUMENT_EXCEPTION = "java/lang/IllegalArgumentException";
constexpr const char* JAVA_ILLEGAL_STATE_EXCEPTION = "java/lang/IllegalStateException";
constexpr const char* JAVA_DATA_EXCEPTION = "com/cytarch/cellularity/CellularityDataException";

inline jbyteArray toJavaByteArray(JNIEnv* env, const std::vector<uint8_t>& bytes) {
    jbyteArray result = env->NewByteArray(static_cast<jsize>(bytes.size()));
    if (result != nullptr && !bytes.empty()) {
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
    }
    return result;
}

inline std::vector<uint8_t> readJavaBytes(JNIEnv* env, jbyteArray byteArray, const char* name) {
    if (byteArray == nullptr) {
        throw DecodeError(DecodeErrorCode::InvalidValue,
                std::string(name) + " byte array is null");
    }
    jsize length = env->GetArrayLength(byteArray);
    std::vector<uint8_t> bytes;
    try {
        bytes.resize(static_cast<size_t>(length));
    } catch (const std::bad_alloc&) {
        throw DecodeError(DecodeErrorCode::OutOfMemory,
                std::string("Could not allocate memory for ") + name);
    } catch (const std::length_error&) {
        throw DecodeError(DecodeErrorCode::LimitExceeded,
                std::string(name) + " exceeds the native container size limit");
    }
    if (length > 0) {
        env->GetByteArrayRegion(byteArray, 0, length, reinterpret_cast<jbyte*>(bytes.data()));
    }
    return bytes;
}

inline std::string readJavaString(JNIEnv* env, jstring value, const char* name) {
    if (value == nullptr) {
        throw std::runtime_error(std::string(name) + " string is null");
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        throw std::runtime_error(std::string("Could not read Java string: ") + name);
    }
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

inline void throwJavaException(JNIEnv* env, const char* className, const char* message) {
    jclass exceptionClass = env->FindClass(className);
    if (exceptionClass != nullptr) {
        env->ThrowNew(exceptionClass, message);
        env->DeleteLocalRef(exceptionClass);
    }
}

inline void throwRuntimeException(JNIEnv* env, const std::exception& exception) {
    const auto* decodeError = dynamic_cast<const DecodeError*>(&exception);
    const bool isOutOfMemory = dynamic_cast<const std::bad_alloc*>(&exception) != nullptr;
    if (decodeError == nullptr && !isOutOfMemory) {
        throwJavaException(env, JAVA_RUNTIME_EXCEPTION, exception.what());
        return;
    }
    const DecodeErrorCode code = decodeError != nullptr
            ? decodeError->code()
            : DecodeErrorCode::OutOfMemory;

    jclass exceptionClass = env->FindClass(JAVA_DATA_EXCEPTION);
    if (exceptionClass != nullptr) {
        jmethodID constructor = env->GetMethodID(
                exceptionClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
        if (constructor != nullptr) {
            jstring javaCode = env->NewStringUTF(decodeErrorCodeName(code));
            jstring javaMessage = env->NewStringUTF(exception.what());
            if (javaCode != nullptr && javaMessage != nullptr) {
                jobject javaException = env->NewObject(
                        exceptionClass, constructor, javaCode, javaMessage);
                if (javaException != nullptr) {
                    env->Throw(static_cast<jthrowable>(javaException));
                    env->DeleteLocalRef(javaException);
                }
            }
            if (javaMessage != nullptr) {
                env->DeleteLocalRef(javaMessage);
            }
            if (javaCode != nullptr) {
                env->DeleteLocalRef(javaCode);
            }
        }
        env->DeleteLocalRef(exceptionClass);
        if (env->ExceptionCheck()) {
            return;
        }
    } else {
        env->ExceptionClear();
    }
    throwJavaException(env, JAVA_RUNTIME_EXCEPTION, exception.what());
}

inline void throwUnknownDecodeException(JNIEnv* env) {
    throwJavaException(env, JAVA_DATA_EXCEPTION,
            "Unexpected native failure while decoding snapshot data");
}
