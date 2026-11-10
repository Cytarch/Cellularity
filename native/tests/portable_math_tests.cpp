// Fixes the bits of the portable elementary functions.
//
// The simulation is bit-exact across Android, Linux and Windows only if these
// functions return the same bits everywhere. The expected values below were produced
// by this implementation; a platform, compiler or flag that changes any of them would
// make simulations diverge between platforms, so the test fails on that platform.

#include "core/PortableMath.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

namespace pm = cellularity::portable_math;

std::uint64_t bitsOf(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int gFailures = 0;

void expectBits(const char* function, double argument, double actual, std::uint64_t expected) {
    if (bitsOf(actual) != expected) {
        std::fprintf(stderr, "%s(%a) = %a (0x%016llx), expected 0x%016llx\n", function, argument, actual,
                static_cast<unsigned long long>(bitsOf(actual)), static_cast<unsigned long long>(expected));
        ++gFailures;
    }
}

struct SinCosCase {
    double x;
    std::uint64_t sin;
    std::uint64_t cos;
};

struct Atan2Case {
    double y;
    double x;
    std::uint64_t atan2;
};

struct UnaryCase {
    double x;
    std::uint64_t value;
};

constexpr SinCosCase kSinCos[] = {
        {0x1.999999999999ap-4, 0x3fb98eaecb8bcb2cULL, 0x3fefd712f9a817c1ULL},
        {0x1p-1, 0x3fdeaee8744b05f0ULL, 0x3fec1528065b7d50ULL},
        {0x1.921fb54442d18p-1, 0x3fe6a09e667f3bccULL, 0x3fe6a09e667f3bcdULL},
        {0x1p+0, 0x3feaed548f090ceeULL, 0x3fe14a280fb5068cULL},
        {0x1.921fb54442d18p+0, 0x3ff0000000000000ULL, 0x3c91a62633145c07ULL},
        {0x1p+1, 0x3fed18f6ead1b446ULL, 0xbfdaa22657537205ULL},
        {0x1.921fb54442d18p+1, 0x3ca1a62633145c07ULL, 0xbff0000000000000ULL},
        {-0x1.4p+1, 0xbfe326af0dcfcab0ULL, 0xbfe9a2f7ef858b7dULL},
        {0x1.4p+3, 0xbfe1689ef5f34f52ULL, 0xbfead9ac890c6b1fULL},
        {-0x1.edd2f1a9fbe77p+6, 0x3fe9b9dadc41aeb6ULL, 0xbfe307e5980a1558ULL},
        {0x1.f4p+9, 0x3fea75cc150a206bULL, 0x3fe1ff026793f1bcULL},
        {0x1.81cd6c8b43958p+13, 0xbfe687d5890974a5ULL, 0x3fe6b94c3bbe24b8ULL},
        {0x1.e848p+19, 0xbfd6664b2568d867ULL, 0x3fedf9df9906d32cULL},
        {0x1.c9c38p+24, 0x3feeda27c8272ab9ULL, 0xbfd0fccb9df0e4edULL},
        {0x1.c6bf52634p+49, 0x3feacdc9d9884216ULL, 0xbfe17ae632761c3aULL},
        {0x1.7e43c8800759cp+996, 0xbfe52ec076f4df9dULL, 0x3fe7fc3f18dde1daULL},
};

constexpr Atan2Case kAtan2[] = {
        {0x1p+0, 0x1p+0, 0x3fe921fb54442d18ULL},
        {-0x1p+0, 0x1p+0, 0xbfe921fb54442d18ULL},
        {0x1p+0, -0x1p+0, 0x4002d97c7f3321d2ULL},
        {-0x1p+0, -0x1p+0, 0xc002d97c7f3321d2ULL},
        {0x1.3333333333333p-2, 0x1p+1, 0x3fc30ed367d7cd04ULL},
        {-0x1.cp+2, 0x1p-2, 0xbff88fc218ace9dcULL},
        {0x1.4f8b588e368f1p-17, -0x1.8p+1, 0x400921f994dfb705ULL},
        {0x1p+1, 0x1p+0, 0x3ff1b6e192ebbe44ULL},
        {0x0p+0, -0x1p+0, 0x400921fb54442d18ULL},
        {-0x0p+0, -0x1p+0, 0xc00921fb54442d18ULL},
};

constexpr UnaryCase kExp[] = {
        {-0x1.0624dd2f1a9fcp-10, 0x3feff7cfe56f1a9eULL},
        {-0x1p-1, 0x3fe368b2fc6f960aULL},
        {-0x1p+0, 0x3fd78b56362cef38ULL},
        {-0x1.d99999999999ap+1, 0x3f99511fc6871044ULL},
        {-0x1.4p+4, 0x3e21b48655f37267ULL},
        {0x1.3333333333333p-2, 0x3ff599058c8c1a96ULL},
        {0x1p+0, 0x4005bf0a8b14576aULL},
        {0x1.6p+2, 0x406e96244f21bbf6ULL},
        {-0x1.5ep+9, 0x00d14f2b0fb9307fULL},
        {-0x1.72p+9, 0x0000000000000055ULL},
};

constexpr UnaryCase kLog[] = {
        {0x1.56e1fc2f8f359p-997, 0xc085963447f87fb5ULL},
        {0x0.0000000000001p-1022, 0xc0874385446d71c3ULL},
        {0x1.b7cdfd9d7bdbbp-34, 0xc037069e2aa2aa5bULL},
        {0x1.0624dd2f1a9fcp-10, 0xc01ba18a998fffa0ULL},
        {0x1p-2, 0xbff62e42fefa39efULL},
        {0x1p-1, 0xbfe62e42fefa39efULL},
        {0x1.fffffca501acbp-1, 0xbe7ad7f2b1049b9fULL},
        {0x1.8p+0, 0x3fd9f323ecbf984cULL},
        {0x1.5bf0a8b145769p+1, 0x3ff0000000000000ULL},
        {0x1.2a05f2p+33, 0x4037069e2aa2aa5bULL},
};

}  // namespace

int main() {
    // volatile keeps the compiler from evaluating the calls at compile time, so the
    // platform's own arithmetic is what is tested.
    for (const SinCosCase& c : kSinCos) {
        volatile double x = c.x;
        expectBits("sin", c.x, pm::sin(x), c.sin);
        expectBits("cos", c.x, pm::cos(x), c.cos);
        expectBits("sin", -c.x, pm::sin(-x), c.sin ^ 0x8000000000000000ULL);
        expectBits("cos", -c.x, pm::cos(-x), c.cos);
    }
    for (const Atan2Case& c : kAtan2) {
        volatile double y = c.y;
        volatile double x = c.x;
        expectBits("atan2", c.y, pm::atan2(y, x), c.atan2);
    }
    for (const UnaryCase& c : kExp) {
        volatile double x = c.x;
        expectBits("exp", c.x, pm::exp(x), c.value);
    }
    for (const UnaryCase& c : kLog) {
        volatile double x = c.x;
        expectBits("log", c.x, pm::log(x), c.value);
    }
    if (gFailures != 0) {
        std::fprintf(stderr, "%d portable math result(s) differ\n", gFailures);
        return 1;
    }
    std::puts("portable math results match");
    return 0;
}
