#pragma once

#include <jni.h>

#include <array>

std::array<float, 16> copyMatrix(JNIEnv* env, jfloatArray matrix);
std::array<float, 3> copyColor(JNIEnv* env, jfloatArray color);
