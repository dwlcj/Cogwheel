// Cogwheel mathematical utilities.
// ----------------------------------------------------------------------------
// Copyright (C) 2015, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ----------------------------------------------------------------------------

#ifndef _COGWHEEL_MATH_UTILS_H_
#define _COGWHEEL_MATH_UTILS_H_

#include <Math/Constants.h>

namespace Cogwheel {
namespace Math {

//*****************************************************************************
// Helper methods
//*****************************************************************************

inline unsigned int compute_ulps(float a, float b) {
    // TODO Use memcpy to move the float bitpattern to an int. See PBRT 3 chapter 7 for why.

    int a_as_int = *(int*)&a;
    // Make a_as_int lexicographically ordered as a twos-complement int
    if (a_as_int < 0)
        a_as_int = int(0x80000000) - a_as_int;

    int b_as_int = *(int*)&b;
    // Make b_as_int lexicographically ordered as a twos-complement int
    if (b_as_int < 0)
        b_as_int = int(0x80000000) - b_as_int;

    return abs(a_as_int - b_as_int);
}

// Floating point almost_equal function.
// http://www.cygnus-software.com/papers/comparingfloats/comparingfloats.htm
inline bool almost_equal(float a, float b, unsigned short max_ulps = 4) {
    return compute_ulps(a, b) <= max_ulps;
}

inline unsigned int ceil_divide(unsigned int a, unsigned int b) {
    return (a / b) + ((a % b) > 0);
}

// Linear interpolation of arbitrary types that implement addition, subtraction and multiplication.
template <typename T>
inline T lerp(const T a, const T b, const T t) {
    return a + t*(b - a);
}

template <typename T>
inline T lerp(const T a, const T b, const float t) {
    return a + (b - a) * t;
}

// Finds the smallest power of 2 greater or equal to x.
inline unsigned int pow2roundup(unsigned int x) {
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

inline float degrees_to_radians(float degress) {
    return degress * (PI<float>() / 180.0f);
}

inline float radians_to_degress(float radians) {
    return radians * (180.0f / PI<float>());
}

} // NS Math
} // NS Cogwheel

#endif // _COGWHEEL_MATH_UTILS_H_