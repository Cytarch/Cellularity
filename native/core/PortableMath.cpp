#include "core/PortableMath.h"

// The algorithms and coefficients are those of fdlibm (Sun Microsystems, 1993), in the
// form maintained by musl:
//
//   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//   Developed at SunSoft, a Sun Microsystems, Inc. business.
//   Permission to use, copy, modify, and distribute this software is freely granted,
//   provided that this notice is preserved.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cellularity::portable_math {
namespace {

std::uint64_t bitsOf(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double fromBits(std::uint64_t bits) {
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t highWord(double value) {
    return static_cast<std::uint32_t>(bitsOf(value) >> 32U);
}

std::uint32_t lowWord(double value) {
    return static_cast<std::uint32_t>(bitsOf(value));
}

// Kernel sin on [-pi/4, pi/4]; y is the tail of x, and hasTail says whether it is used.
double kernelSin(double x, double y, bool hasTail) {
    constexpr double S1 = -1.66666666666666324348e-01;
    constexpr double S2 = 8.33333333332248946124e-03;
    constexpr double S3 = -1.98412698298579493134e-04;
    constexpr double S4 = 2.75573137070700676789e-06;
    constexpr double S5 = -2.50507602534068634195e-08;
    constexpr double S6 = 1.58969099521155010221e-10;

    const double z = x * x;
    const double w = z * z;
    const double r = S2 + z * (S3 + z * S4) + z * w * (S5 + z * S6);
    const double v = z * x;
    if (!hasTail) {
        return x + v * (S1 + z * r);
    }
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

// Kernel cos on [-pi/4, pi/4]; y is the tail of x.
double kernelCos(double x, double y) {
    constexpr double C1 = 4.16666666666666019037e-02;
    constexpr double C2 = -1.38888888888741095749e-03;
    constexpr double C3 = 2.48015872894767294178e-05;
    constexpr double C4 = -2.75573143513906633035e-07;
    constexpr double C5 = 2.08757232129817482790e-09;
    constexpr double C6 = -1.13596475577881948265e-11;

    const double z = x * x;
    double w = z * z;
    const double r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
    const double hz = 0.5 * z;
    w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (z * r - x * y));
}

// Reduces finite x to y0 + y1 in [-pi/4, pi/4] and returns the quadrant modulo 4.
//
// This is fdlibm's Cody-Waite reduction with pi/2 split into three parts, which is
// accurate for |x| below 2^20 * pi/2. Larger arguments are first brought into range by
// an exact fmod with the double nearest 2 pi. That loses accuracy but not determinism:
// the simulation never needs sines of such angles accurately, only identically on every
// platform, which is why fdlibm's Payne-Hanek path is left out.
int reducePiOverTwo(double x, double& y0, double& y1) {
    if ((highWord(x) & 0x7fffffffU) >= 0x413921fbU) {
        x = std::fmod(x, 6.28318530717958647692);
    }

    constexpr double invpio2 = 6.36619772367581382433e-01;
    constexpr double pio2_1 = 1.57079632673412561417e+00;
    constexpr double pio2_1t = 6.07710050650619224932e-11;
    constexpr double pio2_2 = 6.07710050630396597660e-11;
    constexpr double pio2_2t = 2.02226624879595063154e-21;
    constexpr double pio2_3 = 2.02226624871116645580e-21;
    constexpr double pio2_3t = 8.47842766036889956997e-32;

    const double fn = std::rint(x * invpio2);
    double r = x - fn * pio2_1;
    double w = fn * pio2_1t;
    y0 = r - w;

    const int exponentX = static_cast<int>((highWord(x) >> 20U) & 0x7ffU);
    int exponentY = static_cast<int>((highWord(y0) >> 20U) & 0x7ffU);
    if (exponentX - exponentY > 16) {
        double t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y0 = r - w;
        exponentY = static_cast<int>((highWord(y0) >> 20U) & 0x7ffU);
        if (exponentX - exponentY > 49) {
            t = r;
            w = fn * pio2_3;
            r = t - w;
            w = fn * pio2_3t - ((t - r) - w);
            y0 = r - w;
        }
    }
    y1 = (r - y0) - w;

    // fmod is exact, so the quadrant is right even where fn exceeds the int range.
    double quadrant = std::fmod(fn, 4.0);
    if (quadrant < 0.0) {
        quadrant += 4.0;
    }
    return static_cast<int>(quadrant);
}

}  // namespace

double sin(double x) {
    const std::uint32_t ix = highWord(x) & 0x7fffffffU;
    if (ix <= 0x3fe921fbU) {
        if (ix < 0x3e500000U) {
            return x;
        }
        return kernelSin(x, 0.0, false);
    }
    if (ix >= 0x7ff00000U) {
        return x - x;
    }
    double y0 = 0.0;
    double y1 = 0.0;
    switch (reducePiOverTwo(x, y0, y1)) {
        case 0:
            return kernelSin(y0, y1, true);
        case 1:
            return kernelCos(y0, y1);
        case 2:
            return -kernelSin(y0, y1, true);
        default:
            return -kernelCos(y0, y1);
    }
}

double cos(double x) {
    const std::uint32_t ix = highWord(x) & 0x7fffffffU;
    if (ix <= 0x3fe921fbU) {
        if (ix < 0x3e46a09eU) {
            return 1.0;
        }
        return kernelCos(x, 0.0);
    }
    if (ix >= 0x7ff00000U) {
        return x - x;
    }
    double y0 = 0.0;
    double y1 = 0.0;
    switch (reducePiOverTwo(x, y0, y1)) {
        case 0:
            return kernelCos(y0, y1);
        case 1:
            return -kernelSin(y0, y1, true);
        case 2:
            return -kernelCos(y0, y1);
        default:
            return kernelSin(y0, y1, true);
    }
}

double atan(double x) {
    constexpr double atanhi[] = {
            4.63647609000806093515e-01,
            7.85398163397448278999e-01,
            9.82793723247329054082e-01,
            1.57079632679489655800e+00,
    };
    constexpr double atanlo[] = {
            2.26987774529616870924e-17,
            3.06161699786838301793e-17,
            1.39033110312309984516e-17,
            6.12323399573676603587e-17,
    };
    constexpr double aT[] = {
            3.33333333333329318027e-01,
            -1.99999999998764832476e-01,
            1.42857142725034663711e-01,
            -1.11111104054623557880e-01,
            9.09088713343650656196e-02,
            -7.69187620504482999495e-02,
            6.66107313738753120669e-02,
            -5.83357013379057348645e-02,
            4.97687799461593236017e-02,
            -3.65315727442169155270e-02,
            1.62858201153657823623e-02,
    };

    const std::uint32_t hx = highWord(x);
    const bool negative = (hx >> 31U) != 0U;
    const std::uint32_t ix = hx & 0x7fffffffU;

    if (ix >= 0x44100000U) {
        if (x != x) {
            return x;
        }
        const double z = atanhi[3];
        return negative ? -z : z;
    }

    int id = 0;
    if (ix < 0x3fdc0000U) {
        if (ix < 0x3e400000U) {
            return x;
        }
        id = -1;
    } else {
        x = std::fabs(x);
        if (ix < 0x3ff30000U) {
            if (ix < 0x3fe60000U) {
                id = 0;
                x = (2.0 * x - 1.0) / (2.0 + x);
            } else {
                id = 1;
                x = (x - 1.0) / (x + 1.0);
            }
        } else if (ix < 0x40038000U) {
            id = 2;
            x = (x - 1.5) / (1.0 + 1.5 * x);
        } else {
            id = 3;
            x = -1.0 / x;
        }
    }

    double z = x * x;
    const double w = z * z;
    const double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    const double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) {
        return x - x * (s1 + s2);
    }
    z = atanhi[id] - (x * (s1 + s2) - atanlo[id] - x);
    return negative ? -z : z;
}

double atan2(double y, double x) {
    constexpr double pi = 3.1415926535897931160e+00;
    constexpr double pi_lo = 1.2246467991473531772e-16;

    if (x != x || y != y) {
        return x + y;
    }
    std::uint32_t ix = highWord(x);
    const std::uint32_t lx = lowWord(x);
    std::uint32_t iy = highWord(y);
    const std::uint32_t ly = lowWord(y);
    if (((ix - 0x3ff00000U) | lx) == 0U) {
        return atan(y);
    }
    const unsigned m = ((iy >> 31U) & 1U) | ((ix >> 30U) & 2U);
    ix &= 0x7fffffffU;
    iy &= 0x7fffffffU;

    if ((iy | ly) == 0U) {
        switch (m) {
            case 0:
            case 1:
                return y;
            case 2:
                return pi;
            default:
                return -pi;
        }
    }
    if ((ix | lx) == 0U) {
        return (m & 1U) != 0U ? -pi / 2.0 : pi / 2.0;
    }
    if (ix == 0x7ff00000U) {
        if (iy == 0x7ff00000U) {
            switch (m) {
                case 0:
                    return pi / 4.0;
                case 1:
                    return -pi / 4.0;
                case 2:
                    return 3.0 * pi / 4.0;
                default:
                    return -3.0 * pi / 4.0;
            }
        }
        switch (m) {
            case 0:
                return 0.0;
            case 1:
                return -0.0;
            case 2:
                return pi;
            default:
                return -pi;
        }
    }
    if (ix + (64U << 20U) < iy || iy == 0x7ff00000U) {
        return (m & 1U) != 0U ? -pi / 2.0 : pi / 2.0;
    }

    double z = 0.0;
    if ((m & 2U) != 0U && iy + (64U << 20U) < ix) {
        z = 0.0;
    } else {
        z = atan(std::fabs(y / x));
    }
    switch (m) {
        case 0:
            return z;
        case 1:
            return -z;
        case 2:
            return pi - (z - pi_lo);
        default:
            return (z - pi_lo) - pi;
    }
}

double exp(double x) {
    constexpr double ln2hi = 6.93147180369123816490e-01;
    constexpr double ln2lo = 1.90821492927058770002e-10;
    constexpr double invln2 = 1.44269504088896338700e+00;
    constexpr double P1 = 1.66666666666666019037e-01;
    constexpr double P2 = -2.77777777770155933842e-03;
    constexpr double P3 = 6.61375632143793436117e-05;
    constexpr double P4 = -1.65339022054652515390e-06;
    constexpr double P5 = 4.13813679705723846039e-08;

    std::uint32_t hx = highWord(x);
    const int sign = static_cast<int>(hx >> 31U);
    hx &= 0x7fffffffU;

    if (hx >= 0x4086232bU) {
        if (x != x) {
            return x;
        }
        if (x > 709.782712893383973096) {
            return HUGE_VAL;
        }
        if (x < -745.13321910194110842) {
            return 0.0;
        }
    }

    double hi = 0.0;
    double lo = 0.0;
    int k = 0;
    if (hx > 0x3fd62e42U) {
        if (hx >= 0x3ff0a2b2U) {
            k = static_cast<int>(invln2 * x + (sign != 0 ? -0.5 : 0.5));
        } else {
            k = 1 - sign - sign;
        }
        hi = x - k * ln2hi;
        lo = k * ln2lo;
        x = hi - lo;
    } else if (hx > 0x3e300000U) {
        hi = x;
    } else {
        return 1.0 + x;
    }

    const double xx = x * x;
    const double c = x - xx * (P1 + xx * (P2 + xx * (P3 + xx * (P4 + xx * P5))));
    const double y = 1.0 + (x * c / (2.0 - c) - lo + hi);
    if (k == 0) {
        return y;
    }
    // ldexp scales by a power of two exactly, rounding only on underflow.
    return std::ldexp(y, k);
}

double log(double x) {
    constexpr double ln2_hi = 6.93147180369123816490e-01;
    constexpr double ln2_lo = 1.90821492927058770002e-10;
    constexpr double Lg1 = 6.666666666666735130e-01;
    constexpr double Lg2 = 3.999999999940941908e-01;
    constexpr double Lg3 = 2.857142874366239149e-01;
    constexpr double Lg4 = 2.222219843214978396e-01;
    constexpr double Lg5 = 1.818357216161805012e-01;
    constexpr double Lg6 = 1.531383769920937332e-01;
    constexpr double Lg7 = 1.479819860511658591e-01;

    std::uint64_t bits = bitsOf(x);
    std::uint32_t hx = static_cast<std::uint32_t>(bits >> 32U);
    int k = 0;
    if (hx < 0x00100000U || (hx >> 31U) != 0U) {
        if ((bits << 1U) == 0U) {
            return -HUGE_VAL;
        }
        if ((hx >> 31U) != 0U) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        // Subnormal: scale into the normal range.
        k -= 54;
        x *= 18014398509481984.0;  // 2^54
        bits = bitsOf(x);
        hx = static_cast<std::uint32_t>(bits >> 32U);
    } else if (hx >= 0x7ff00000U) {
        return x;
    } else if (hx == 0x3ff00000U && (bits << 32U) == 0U) {
        return 0.0;
    }

    hx += 0x3ff00000U - 0x3fe6a09eU;
    k += static_cast<int>(hx >> 20U) - 0x3ff;
    hx = (hx & 0x000fffffU) + 0x3fe6a09eU;
    x = fromBits((static_cast<std::uint64_t>(hx) << 32U) | (bits & 0xffffffffULL));

    const double f = x - 1.0;
    const double hfsq = 0.5 * f * f;
    const double s = f / (2.0 + f);
    const double z = s * s;
    const double w = z * z;
    const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    const double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    const double R = t2 + t1;
    const double dk = k;
    return s * (hfsq + R) + dk * ln2_lo - hfsq + f + dk * ln2_hi;
}

double hypot(double x, double y) {
    return std::sqrt(x * x + y * y);
}

}  // namespace cellularity::portable_math
