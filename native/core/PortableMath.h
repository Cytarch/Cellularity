#pragma once

// Platform-independent elementary functions for the simulation.
//
// The C library's sin, cos, atan2, exp and log are not specified to the last bit, and
// glibc, Bionic and the Microsoft CRT return different results for some arguments. A
// world simulated on two platforms then drifts apart one ulp at a time. These
// functions follow fdlibm and use only IEEE 754 addition, subtraction, multiplication,
// division and integer bit manipulation, which every supported platform evaluates
// identically (the build disables floating-point contraction, so no operation is fused
// into an FMA). Their accuracy is that of fdlibm: under one ulp.
//
// Simulation code must call these instead of the <cmath> transcendental functions.
// <cmath> functions that IEEE 754 defines exactly (sqrt, floor, ceil, fabs, fmod,
// ldexp) remain fine.

namespace cellularity::portable_math {

double sin(double x);
double cos(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);
double log(double x);

// sqrt(x * x + y * y) with the C library's hypot replaced by exactly rounded steps.
double hypot(double x, double y);

// The double nearest pi, the value std::acos(-1.0) returns.
inline constexpr double kPi = 3.14159265358979323846;

// x * x * x, evaluated left to right.
inline double cube(double x) {
    return x * x * x;
}

}  // namespace cellularity::portable_math
